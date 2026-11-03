// Tests for the ~peek and ~poke patcher objects (patcher/sampling/dPeek,
// dPoke), issue #1144. The sample math is DSP::sampleAccess's and
// DSP::sampleIndexWriter's (Tests/dsp/test_sample_access.cpp) and the
// in-place writer is DSP::sampleBufferWriter's (test_sampleBuffer.cpp).
// These show that pins, messages and arguments reach them, alone and in a
// rendered patch beside a ~buffer: ~poke recording a signal that ~peek reads
// back, ~peek writing what a host reads, a control thread peeking while the
// audio thread pokes, a round trip, and allocation-free messages and blocks.
// The `set` section (#1256) covers the shared PATCHER::bufferBinding: ~peek
// and ~poke rebinding, alone, allocation-free and while blocks run.

#include <doctest/doctest.h>
#include <atomic>
#include <memory>
#include <string>
#include <thread>
#include <vector>
#include "dsp/sampling/sampleBuffer.hpp"
#include "headers/constants.hpp"
#include "patcher/io/fileScheduler.h"
#include "patcher/pHandle.hpp"
#include "patcher/pObjectList.hpp"
#include "patcher/pRegistry.h"
#include "patcher/patcher.hpp"
#include "patcher/patcherImplementation.h"
#include "patcher/sampling/bufferBinding.h"
#include "patcher/sampling/dPeek.h"
#include "patcher/sampling/dPoke.h"
#include "support/alloc_probe.hpp"
#include "sinks.hpp"

using TestHelpers::OrderSink;
using YSE::DSP::sampleBuffer;
using YSE::DSP::sampleBufferRegistry;
using YSE::PATCHER::dPeek;
using YSE::PATCHER::dPoke;
using YSE::PATCHER::patcherImplementation;

namespace {

  // A named buffer whose sample n of channel c is (c + 1) * 10 + n.
  std::shared_ptr<sampleBuffer> Counting(const std::string& name, UInt channels, UInt frames) {
    std::shared_ptr<sampleBuffer> b = sampleBufferRegistry::acquire(name);
    b->setSize(frames, static_cast<Int>(channels));
    for (UInt c = 0; c < channels; ++c) {
      std::vector<float> values(frames);
      for (UInt n = 0; n < frames; ++n)
        values[n] = static_cast<float>((c + 1) * 10 + n);
      b->setSamples(c, 0, values.data(), frames);
    }
    return b;
  }

  float At(const sampleBuffer& b, UInt c, UInt frame) {
    float out = 0.f;
    b.getSamples(c, frame, &out, 1);
    return out;
  }

  // A ~peek in a patcher with a sink on its outlet. The sink comes first so
  // it outlives the patcher (issue #967).
  struct PeekRig {
    OrderSink sink;
    std::unique_ptr<YSE::pHandle> sinkHandle;
    patcherImplementation p{1, nullptr};
    YSE::pHandle* handle = nullptr;
    dPeek* peek = nullptr;

    explicit PeekRig(const std::string& args) {
      handle = p.CreateObject(YSE::OBJ::D_PEEK, args);
      REQUIRE(handle != nullptr);
      peek = static_cast<dPeek*>(p.GetObjectFromID(handle->GetID()));
      sinkHandle = std::make_unique<YSE::pHandle>(&sink);
      p.Connect(handle, 0, sinkHandle.get(), 0);
    }

    void Index(int index, YSE::THREAD thread = YSE::T_GUI) {
      peek->GetInlet(0)->SetInt(index, thread);
    }
    void Value(float value, YSE::THREAD thread = YSE::T_GUI) {
      peek->GetInlet(1)->SetFloat(value, thread);
    }
    void Channel(int channel, YSE::THREAD thread = YSE::T_GUI) {
      peek->GetInlet(2)->SetInt(channel, thread);
    }
    void Message(const std::string& text, YSE::THREAD thread = YSE::T_GUI) {
      peek->GetInlet(0)->SetMessage(text, thread);
    }
  };

  YSE::PATCHER::pObject* Object(patcherImplementation& p, YSE::pHandle* h) {
    return p.GetObjectFromID(h->GetID());
  }

  // A ~poke in a patcher, fed by two ~sig: the value and the index (#1256).
  struct PokeRig {
    patcherImplementation p{1, nullptr};
    YSE::pHandle* value = nullptr;
    YSE::pHandle* index = nullptr;
    YSE::pHandle* handle = nullptr;
    dPoke* poke = nullptr;

    PokeRig(const std::string& args, float v, int at) {
      value = p.CreateObject(YSE::OBJ::D_SIG, std::to_string(v));
      index = p.CreateObject(YSE::OBJ::D_SIG, std::to_string(at));
      handle = p.CreateObject(YSE::OBJ::D_POKE, args);
      REQUIRE(handle != nullptr);
      p.Connect(value, 0, handle, 0);
      p.Connect(index, 0, handle, 1);
      poke = static_cast<dPoke*>(Object(p, handle));
    }

    void Message(const std::string& text, YSE::THREAD thread = YSE::T_GUI) {
      poke->GetInlet(0)->SetMessage(text, thread);
    }

    // The bind task runs on the background pool, the next block takes the
    // writers, and the task that frees the old ones runs after it.
    void Settle() {
      YSE::PATCHER::fileScheduler* io = p.FileIO();
      REQUIRE(io != nullptr);
      io->WaitIdle();
      p.Calculate(YSE::T_DSP);
      io->WaitIdle();
    }
  };

} // namespace

TEST_SUITE("patcher") {

  // ─── ~peek ────────────────────────────────────────────────────────────────

  TEST_CASE("~peek: registry and shape (#1144)") {
    YSE::patcher p;
    p.create(1);
    YSE::pHandle* h = p.CreateObject(YSE::OBJ::D_PEEK, "shape_1144");
    REQUIRE(h != nullptr);
    CHECK(std::string(h->Type()) == "~peek");
    CHECK(h->GetInputs() == 3);
    REQUIRE(h->GetOutputs() == 1);
    CHECK(h->OutputDataType(0) == YSE::OUT_TYPE::FLOAT);

    bool listed = false;
    for (const auto& name : YSE::PATCHER::Register().AllNames())
      if (name == YSE::OBJ::D_PEEK) listed = true;
    CHECK(listed);

    dPeek standalone;
    CHECK(standalone.GetCategory() == YSE::PATCHER::pCategory::SAMPLING);
    CHECK(standalone.CurrentChannel() == 1);
    CHECK(standalone.Clipping());
  }

  TEST_CASE("~peek: an index sends the sample there, with the audio stopped (#1144)") {
    auto b = Counting("read_1144", 2, 8);
    PeekRig r("read_1144");
    CHECK(r.peek->Buffer() == b);
    // No block has run: ~peek answers from the message itself.
    r.Index(3);
    CHECK(r.sink.lastKind == OrderSink::FLOAT);
    CHECK(r.sink.lastFloat == 13.f);
    // A float index is truncated.
    r.peek->GetInlet(0)->SetFloat(5.9f, YSE::T_GUI);
    CHECK(r.sink.lastFloat == 15.f);
    CHECK(r.sink.count == 2);

    // Outside the buffer: nothing is sent.
    r.Index(8);
    r.Index(-1);
    CHECK(r.sink.count == 2);
  }

  TEST_CASE("~peek: a value then an index writes, and sends nothing (#1144)") {
    auto b = Counting("write_1144", 1, 8);
    PeekRig r("write_1144");
    r.Value(0.5f);
    r.Index(2);
    CHECK(r.sink.count == 0);
    CHECK(At(*b, 0, 2) == 0.5f);
    // The value is used once: the next index reads again.
    r.Index(2);
    CHECK(r.sink.lastFloat == 0.5f);
    // An int value works too, and an index outside the buffer drops it.
    r.peek->GetInlet(1)->SetInt(1, YSE::T_GUI);
    r.Index(99);
    r.Index(3);
    CHECK(r.sink.lastFloat == 13.f);
  }

  TEST_CASE("~peek: written values are clipped unless clip 0 (#1144)") {
    auto b = Counting("clip_1144", 1, 4);
    PeekRig r("clip_1144");
    r.Value(4.f);
    r.Index(0);
    CHECK(At(*b, 0, 0) == 1.f);
    r.Message("clip 0");
    CHECK_FALSE(r.peek->Clipping());
    r.Value(4.f);
    r.Index(1);
    CHECK(At(*b, 0, 1) == 4.f);
    r.Message("clip 1");
    CHECK(r.peek->Clipping());

    // The third argument starts with clipping off.
    PeekRig off("clip_1144 1 0");
    CHECK_FALSE(off.peek->Clipping());
    off.Value(-3.f);
    off.Index(2);
    CHECK(At(*b, 0, 2) == -3.f);
  }

  TEST_CASE("~peek: a list writes at once; its channel becomes the channel (#1144)") {
    auto b = Counting("list_1144", 2, 4);
    PeekRig r("list_1144");
    r.peek->GetInlet(0)->SetList("1 0.25", YSE::T_GUI);
    CHECK(At(*b, 0, 1) == 0.25f);
    CHECK(r.sink.count == 0);
    r.peek->GetInlet(0)->SetList("2 -0.5 2", YSE::T_GUI);
    CHECK(At(*b, 1, 2) == -0.5f);
    CHECK(r.peek->CurrentChannel() == 2);
    r.Index(2);
    CHECK(r.sink.lastFloat == -0.5f);
    // Too short a list is ignored.
    r.peek->GetInlet(0)->SetList("3", YSE::T_GUI);
    CHECK(At(*b, 1, 3) == 23.f);
  }

  TEST_CASE("~peek: the channel, from 1, by argument and by inlet (#1144)") {
    auto b = Counting("chan_1144", 3, 4);
    PeekRig r("chan_1144 2");
    CHECK(r.peek->CurrentChannel() == 2);
    r.Index(1);
    CHECK(r.sink.lastFloat == 21.f);
    r.Channel(3);
    r.Index(1);
    CHECK(r.sink.lastFloat == 31.f);
    r.peek->GetInlet(2)->SetFloat(1.7f, YSE::T_GUI);
    r.Index(1);
    CHECK(r.sink.lastFloat == 11.f);
    // Below 1 is 1; past the buffer's channels reads nothing.
    r.Channel(0);
    CHECK(r.peek->CurrentChannel() == 1);
    r.Channel(4);
    const int before = r.sink.count;
    r.Index(1);
    CHECK(r.sink.count == before);
  }

  TEST_CASE("~peek: no name, or a buffer filled later (#1144)") {
    PeekRig none("");
    CHECK(none.peek->Buffer() == nullptr);
    none.Index(0);
    CHECK(none.sink.count == 0);

    // A name nobody holds yet makes the buffer, empty until it is filled.
    PeekRig later("later_1144");
    later.Index(0);
    CHECK(later.sink.count == 0);
    sampleBufferRegistry::find("later_1144")->setSize(4);
    sampleBufferRegistry::find("later_1144")->fill(0.75f);
    later.Index(0);
    CHECK(later.sink.lastFloat == 0.75f);
  }

  TEST_CASE("~peek: arguments survive a DumpJSON / ParseJSON round trip (#1144)") {
    std::string dump;
    {
      patcherImplementation src(1, nullptr);
      REQUIRE(src.CreateObject(YSE::OBJ::D_PEEK, "json_peek_1144 2 0") != nullptr);
      dump = src.DumpJSON();
    }
    patcherImplementation dst(1, nullptr);
    dst.ParseJSON(dump);
    CHECK(dst.DumpJSON() == dump);
    CHECK(dump.find("json_peek_1144 2 0") != std::string::npos);
  }

  TEST_CASE("~peek: messages on the audio thread allocate nothing (#1144)") {
    auto b = Counting("rt_peek_1144", 2, 64);
    PeekRig r("rt_peek_1144");
    const std::string list = "4 0.5 2";
    const std::string clip = "clip 0";
    r.Index(0); // the sink's first send, outside the probe
    {
      TestHelpers::ProbeScope probe;
      r.Index(3, YSE::T_DSP);
      r.Value(0.25f, YSE::T_DSP);
      r.Index(5, YSE::T_DSP);
      r.peek->GetInlet(0)->SetList(list, YSE::T_DSP);
      r.Message(clip, YSE::T_DSP);
      r.Channel(1, YSE::T_DSP);
      r.peek->GetInlet(0)->SetFloat(6.f, YSE::T_DSP);
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
    CHECK(At(*b, 0, 5) == 0.25f);
    CHECK(At(*b, 1, 4) == 0.5f);
    CHECK(r.sink.lastFloat == 16.f);
  }

  // ─── ~poke ────────────────────────────────────────────────────────────────

  TEST_CASE("~poke: registry and shape (#1144)") {
    YSE::patcher p;
    p.create(1);
    YSE::pHandle* h = p.CreateObject(YSE::OBJ::D_POKE, "shape_1144");
    REQUIRE(h != nullptr);
    CHECK(std::string(h->Type()) == "~poke");
    CHECK(h->GetInputs() == 3);
    CHECK(h->GetOutputs() == 0);

    bool listed = false;
    for (const auto& name : YSE::PATCHER::Register().AllNames())
      if (name == YSE::OBJ::D_POKE) listed = true;
    CHECK(listed);

    dPoke standalone;
    CHECK(standalone.GetCategory() == YSE::PATCHER::pCategory::SAMPLING);
    CHECK(standalone.CurrentChannel() == 1);
  }

  TEST_CASE("~poke: two numbers write the value at the index each block (#1144)") {
    auto b = Counting("nums_1144", 1, 8);
    dPoke poke;
    poke.SetParams("nums_1144");
    CHECK(poke.Buffer() == b);
    // The index starts at -1: a block writes nothing.
    poke.Calculate(YSE::T_DSP);
    CHECK(At(*b, 0, 0) == 10.f);

    poke.GetInlet(0)->SetFloat(0.5f, YSE::T_GUI);
    poke.GetInlet(1)->SetInt(3, YSE::T_GUI);
    poke.Calculate(YSE::T_DSP);
    CHECK(At(*b, 0, 3) == 0.5f);
    // A float index is truncated; -1 stops writing again.
    poke.GetInlet(1)->SetFloat(4.8f, YSE::T_GUI);
    poke.GetInlet(0)->SetInt(2, YSE::T_GUI);
    poke.Calculate(YSE::T_DSP);
    CHECK(At(*b, 0, 4) == 2.f);
    poke.GetInlet(1)->SetFloat(-1.f, YSE::T_GUI);
    poke.GetInlet(0)->SetFloat(9.f, YSE::T_GUI);
    poke.Calculate(YSE::T_DSP);
    CHECK(At(*b, 0, 4) == 2.f);
    // Not clipped.
    CHECK(At(*b, 0, 5) == 15.f);
  }

  TEST_CASE("~poke: signals write each value at its index (#1144)") {
    auto b = sampleBufferRegistry::acquire("sig_1144");
    b->setSize(YSE::STANDARD_BUFFERSIZE * 2);
    dPoke poke;
    poke.SetParams("sig_1144");
    YSE::DSP::buffer values(YSE::STANDARD_BUFFERSIZE);
    YSE::DSP::buffer indexes(YSE::STANDARD_BUFFERSIZE);
    for (UInt i = 0; i < YSE::STANDARD_BUFFERSIZE; ++i) {
      values.getPtr()[i] = static_cast<float>(i) * 0.001f;
      // Every other sample, from the end of the first block on.
      indexes.getPtr()[i] = i % 2 == 0 ? static_cast<float>(YSE::STANDARD_BUFFERSIZE + i) : -1.f;
    }
    // The numbers are ignored while signals are connected (#1036). The
    // indexes arrive first: the values inlet is the hot one, and runs it.
    poke.GetInlet(1)->SetFloat(0.f, YSE::T_GUI);
    poke.GetInlet(0)->SetFloat(7.f, YSE::T_GUI);
    poke.GetInlet(1)->SetBuffer(&indexes, YSE::T_DSP);
    poke.GetInlet(0)->SetBuffer(&values, YSE::T_DSP);
    CHECK(At(*b, 0, 0) == 0.f);
    CHECK(At(*b, 0, YSE::STANDARD_BUFFERSIZE) == 0.f);
    CHECK(At(*b, 0, YSE::STANDARD_BUFFERSIZE + 2) == doctest::Approx(0.002f));
    CHECK(At(*b, 0, YSE::STANDARD_BUFFERSIZE + 3) == 0.f);
    CHECK(At(*b, 0, YSE::STANDARD_BUFFERSIZE + 10) == doctest::Approx(0.010f));
  }

  TEST_CASE("~poke: a list writes once, at once; the channel by argument and inlet (#1144)") {
    auto b = Counting("plist_1144", 3, 4);
    dPoke poke;
    poke.SetParams("plist_1144 2");
    CHECK(poke.CurrentChannel() == 2);
    poke.GetInlet(0)->SetList("0.25 1", YSE::T_GUI);
    CHECK(At(*b, 1, 1) == 0.25f);
    poke.GetInlet(0)->SetList("0.75 3 3", YSE::T_GUI);
    CHECK(At(*b, 2, 3) == 0.75f);
    CHECK(poke.CurrentChannel() == 3);
    poke.GetInlet(0)->SetList("0.75 4", YSE::T_GUI); // past the end
    poke.GetInlet(0)->SetList("0.75", YSE::T_GUI); // too short
    poke.GetInlet(2)->SetInt(1, YSE::T_GUI);
    poke.GetInlet(0)->SetFloat(-0.5f, YSE::T_GUI);
    poke.GetInlet(1)->SetInt(0, YSE::T_GUI);
    poke.Calculate(YSE::T_DSP);
    CHECK(At(*b, 0, 0) == -0.5f);
    CHECK(At(*b, 1, 0) == 20.f);
    // A channel the buffer lacks writes nothing.
    poke.GetInlet(2)->SetFloat(9.f, YSE::T_GUI);
    poke.GetInlet(0)->SetList("1 0", YSE::T_GUI);
    CHECK(At(*b, 2, 0) == 30.f);
  }

  TEST_CASE("~poke records ~count-indexed ~sig into a ~buffer that ~peek reads (#1144)") {
    // The patch a user builds: [~sig] and [~count] into [~poke rec], with the
    // buffer held by [~buffer rec] and read back by [~peek rec].
    OrderSink sink;
    auto sinkHandle = std::make_unique<YSE::pHandle>(&sink);
    patcherImplementation p(1, nullptr);
    YSE::pHandle* buffer = p.CreateObject(YSE::OBJ::D_BUFFER, "rec_1144");
    YSE::pHandle* value = p.CreateObject(YSE::OBJ::D_SIG, "0.25");
    YSE::pHandle* index = p.CreateObject(YSE::OBJ::D_COUNT, "0 0 1");
    YSE::pHandle* poke = p.CreateObject(YSE::OBJ::D_POKE, "rec_1144");
    YSE::pHandle* peek = p.CreateObject(YSE::OBJ::D_PEEK, "rec_1144");
    REQUIRE(buffer != nullptr);
    REQUIRE(poke != nullptr);
    REQUIRE(peek != nullptr);
    p.Connect(value, 0, poke, 0);
    p.Connect(index, 0, poke, 1);
    p.Connect(peek, 0, sinkHandle.get(), 0);

    std::shared_ptr<sampleBuffer> rec = sampleBufferRegistry::find("rec_1144");
    REQUIRE(rec != nullptr);
    rec->setSize(YSE::STANDARD_BUFFERSIZE + 10);

    p.Calculate(YSE::T_DSP);
    // The first block's count, 0 to STANDARD_BUFFERSIZE - 1, wrote 0.25 there.
    CHECK(At(*rec, 0, 0) == 0.25f);
    CHECK(At(*rec, 0, YSE::STANDARD_BUFFERSIZE - 1) == 0.25f);
    CHECK(At(*rec, 0, YSE::STANDARD_BUFFERSIZE) == 0.f);

    // ~sig changes; the second block's count runs on past the end, where the
    // writes beyond the buffer are dropped.
    value->SetFloatData(0, -0.5f);
    p.Calculate(YSE::T_DSP);
    CHECK(At(*rec, 0, YSE::STANDARD_BUFFERSIZE + 9) == -0.5f);
    CHECK(At(*rec, 0, 0) == 0.25f);

    // ~peek reads both back, in the same patch.
    YSE::PATCHER::pObject* peekObject = Object(p, peek);
    peekObject->GetInlet(0)->SetInt(1, YSE::T_GUI);
    CHECK(sink.lastFloat == 0.25f);
    peekObject->GetInlet(0)->SetInt(static_cast<int>(YSE::STANDARD_BUFFERSIZE) + 3, YSE::T_GUI);
    CHECK(sink.lastFloat == -0.5f);
    // And ~peek's own write is what the host and the next ~peek read see.
    peekObject->GetInlet(1)->SetFloat(0.125f, YSE::T_GUI);
    peekObject->GetInlet(0)->SetInt(2, YSE::T_GUI);
    CHECK(At(*rec, 0, 2) == 0.125f);
  }

  TEST_CASE("~poke: arguments survive a DumpJSON / ParseJSON round trip (#1144)") {
    std::shared_ptr<sampleBuffer> b = Counting("json_poke_1144", 2, 8);
    std::string dump;
    {
      patcherImplementation src(1, nullptr);
      YSE::pHandle* value = src.CreateObject(YSE::OBJ::D_SIG, "0.5");
      YSE::pHandle* index = src.CreateObject(YSE::OBJ::D_SIG, "6");
      YSE::pHandle* poke = src.CreateObject(YSE::OBJ::D_POKE, "json_poke_1144 2");
      src.Connect(value, 0, poke, 0);
      src.Connect(index, 0, poke, 1);
      dump = src.DumpJSON();
    }
    patcherImplementation dst(1, nullptr);
    dst.ParseJSON(dump);
    CHECK(dst.DumpJSON() == dump);
    CHECK(dump.find("json_poke_1144 2") != std::string::npos);
    // The reloaded ~poke writes channel 2 of the same buffer.
    dst.Calculate(YSE::T_DSP);
    CHECK(At(*b, 1, 6) == 0.5f);
    CHECK(At(*b, 0, 6) == 16.f);
  }

  TEST_CASE("~poke: blocks and lists on the audio thread allocate nothing (#1144)") {
    auto b = sampleBufferRegistry::acquire("rt_poke_1144");
    b->setSize(1024, 2);
    dPoke poke;
    poke.SetParams("rt_poke_1144");
    YSE::DSP::buffer values(YSE::STANDARD_BUFFERSIZE);
    YSE::DSP::buffer indexes(YSE::STANDARD_BUFFERSIZE);
    for (UInt i = 0; i < YSE::STANDARD_BUFFERSIZE; ++i) {
      values.getPtr()[i] = 0.5f;
      indexes.getPtr()[i] = static_cast<float>(i);
    }
    const std::string list = "0.75 600 2";
    poke.Calculate(YSE::T_DSP);
    {
      TestHelpers::ProbeScope probe;
      poke.GetInlet(1)->SetInt(700, YSE::T_DSP);
      poke.GetInlet(0)->SetFloat(0.25f, YSE::T_DSP);
      poke.Calculate(YSE::T_DSP);
      poke.GetInlet(0)->SetList(list, YSE::T_DSP);
      poke.GetInlet(1)->SetBuffer(&indexes, YSE::T_DSP);
      poke.GetInlet(0)->SetBuffer(&values, YSE::T_DSP); // the hot inlet runs it
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
    CHECK(At(*b, 0, 700) == 0.25f);
    CHECK(At(*b, 1, 600) == 0.75f);
    CHECK(At(*b, 1, 5) == 0.5f); // the list's channel 2 is the channel since
    CHECK(At(*b, 0, 5) == 0.f);
  }

  TEST_CASE("~peek on a control thread while ~poke writes on the audio thread (#1144)") {
    // Every value ~peek reads is whole: the fill, or what ~poke wrote. Under
    // ASan or TSan a write racing the read, or a freed storage, shows here.
    auto b = sampleBufferRegistry::acquire("race_pp_1144");
    b->setSize(YSE::STANDARD_BUFFERSIZE);
    b->fill(1.f);
    TestHelpers::FloatSink floats; // before the ~peek that sends to it
    patcherImplementation p(1, nullptr);
    YSE::pHandle* value = p.CreateObject(YSE::OBJ::D_SIG, "-1");
    YSE::pHandle* index =
        p.CreateObject(YSE::OBJ::D_COUNT, "0 " + std::to_string(YSE::STANDARD_BUFFERSIZE) + " 1");
    YSE::pHandle* poke = p.CreateObject(YSE::OBJ::D_POKE, "race_pp_1144");
    p.Connect(value, 0, poke, 0);
    p.Connect(index, 0, poke, 1);
    std::unique_ptr<YSE::PATCHER::pObject> peek(YSE::PATCHER::Register().Get(YSE::OBJ::D_PEEK));
    peek->SetParams("race_pp_1144");

    std::atomic<bool> stop{false};
    std::atomic<int> reads{0};
    std::atomic<int> bad{0};
    TestHelpers::Wire(*peek, 0, floats, 0);
    std::thread control([&] {
      int n = 0;
      while (!stop.load()) {
        floats.gotFloat = false;
        peek->GetInlet(0)->SetInt(n++ % static_cast<int>(YSE::STANDARD_BUFFERSIZE), YSE::T_GUI);
        if (floats.gotFloat) {
          if (!(floats.received == 1.f || floats.received == -1.f)) bad.fetch_add(1);
          reads.fetch_add(1);
        }
        // Edits from the control side meanwhile: the copy must not race.
        if (n % 64 == 0) b->applyGain(1.f);
      }
    });
    for (int block = 0; block < 2000 || reads.load() < 100; ++block)
      p.Calculate(YSE::T_DSP);
    stop.store(true);
    control.join();
    CHECK(bad.load() == 0);
    CHECK(reads.load() > 0);
  }

  // ─── set (#1256) ──────────────────────────────────────────────────────────

  TEST_CASE("bufferBinding: a reader binding is rebound by name in the background (#1256)") {
    using YSE::PATCHER::bufferBinding;
    auto a = Counting("bind_1256_a", 1, 4);
    auto b = Counting("bind_1256_b", 1, 4);
    // Any object in a patcher with file I/O can own one; a ~peek is.
    PeekRig owner("");
    YSE::PATCHER::fileScheduler* io = owner.p.FileIO();
    REQUIRE(io != nullptr);

    bufferBinding<YSE::DSP::sampleBufferReader> binding;
    CHECK(binding.Current().buffer() == nullptr);
    binding.Bind("bind_1256_a");
    CHECK(binding.Current().buffer() == a);
    CHECK_FALSE(binding.Take(*owner.peek)); // nothing waiting

    // Of two requests before a take, the last wins.
    CHECK(binding.Request(*owner.peek, "bind_1256_a", 11));
    CHECK(binding.Request(*owner.peek, "bind_1256_b", 11));
    io->WaitIdle();
    CHECK(binding.Current().buffer() == a); // not before the take
    CHECK(binding.Take(*owner.peek));
    CHECK(binding.Current().buffer() == b);
    CHECK(binding.Current().pin().at(0, 2) == 12.f);
    io->WaitIdle(); // the release task Take() asked for
    CHECK_FALSE(binding.ReleasePending());
    owner.p.Calculate(YSE::T_DSP); // hands the slots back

    // An empty name unbinds; a name no slot holds is refused.
    CHECK(binding.HandleSet(*owner.peek, "set"));
    io->WaitIdle();
    CHECK(binding.Take(*owner.peek));
    CHECK(binding.Current().buffer() == nullptr);
    CHECK(binding.Current().pin().empty());
    const std::string tooLong(YSE::PATCHER::fileScheduler::PATH_CAPACITY, 'x');
    CHECK_FALSE(binding.Request(*owner.peek, tooLong.c_str(), tooLong.size()));
    CHECK_FALSE(binding.HandleSet(*owner.peek, "clip 0")); // not a set
    io->WaitIdle();
    CHECK(io->Dropped() == 0); // refused before it reached the table

    // Outside a patcher there is no scheduler: refused.
    dPeek standalone;
    CHECK_FALSE(binding.Request(standalone, "bind_1256_a", 11));
  }

  TEST_CASE("~peek: set reads and writes another buffer, with the audio stopped (#1256)") {
    auto a = Counting("peek_set_1256_a", 1, 8);
    auto b = Counting("peek_set_1256_b", 2, 8);
    b->applyOffset(100.f); // 110, 111, ... and 120, 121, ...
    PeekRig r("peek_set_1256_a");
    YSE::PATCHER::fileScheduler* io = r.p.FileIO();
    REQUIRE(io != nullptr);
    r.Index(3);
    CHECK(r.sink.lastFloat == 13.f);

    r.Message("set peek_set_1256_b");
    io->WaitIdle();
    // No block has run: the next index takes the new writer and reads b.
    r.Index(3);
    CHECK(r.sink.lastFloat == 113.f);
    CHECK(r.peek->Buffer() == b);
    r.Channel(2);
    r.Index(3);
    CHECK(r.sink.lastFloat == 123.f); // b has a second channel
    r.Value(0.5f);
    r.Index(1);
    CHECK(At(*b, 1, 1) == 0.5f);
    CHECK(At(*a, 0, 1) == 11.f);
    io->WaitIdle();
    CHECK_FALSE(r.peek->ReleasePending()); // a's writer is freed

    // A list takes it too. set with no name unbinds.
    r.p.Calculate(YSE::T_DSP);
    r.Message("set peek_set_1256_a");
    io->WaitIdle();
    r.peek->GetInlet(0)->SetList("2 -0.25 1", YSE::T_GUI);
    CHECK(At(*a, 0, 2) == -0.25f);
    CHECK(r.peek->Buffer() == a);
    r.Message("set");
    io->WaitIdle();
    const int before = r.sink.count;
    r.Index(0);
    CHECK(r.sink.count == before);
    CHECK(r.peek->Buffer() == nullptr);
    CHECK(io->Dropped() == 0);
  }

  TEST_CASE("~poke: set writes another buffer from a later block (#1256)") {
    auto a = Counting("poke_set_1256_a", 1, 8);
    auto b = Counting("poke_set_1256_b", 1, 8);
    PokeRig r("poke_set_1256_a", 0.5f, 2);
    r.p.Calculate(YSE::T_DSP);
    CHECK(At(*a, 0, 2) == 0.5f);

    r.Message("set poke_set_1256_b");
    r.Settle();
    CHECK(r.poke->Buffer() == b);
    CHECK(At(*b, 0, 2) == 0.5f);
    CHECK_FALSE(r.poke->ReleasePending());
    // The list writes the new buffer too, and the old one no more.
    r.poke->GetInlet(0)->SetList("0.75 5", YSE::T_GUI);
    CHECK(At(*b, 0, 5) == 0.75f);
    CHECK(At(*a, 0, 5) == 15.f);
    a->fill(0.f);
    r.p.Calculate(YSE::T_DSP);
    CHECK(At(*a, 0, 2) == 0.f);

    // set with no name: unbound, writes nothing.
    r.Message("set");
    r.Settle();
    CHECK(r.poke->Buffer() == nullptr);
    b->fill(0.f);
    r.p.Calculate(YSE::T_DSP);
    r.poke->GetInlet(0)->SetList("0.75 5", YSE::T_GUI);
    CHECK(At(*b, 0, 2) == 0.f);
    CHECK(At(*b, 0, 5) == 0.f);
    CHECK(r.p.FileIO()->Dropped() == 0);
  }

  TEST_CASE("~peek / ~poke: a set is not saved by DumpJSON (#1256)") {
    auto a = Counting("json_set_1256_a", 1, 8);
    auto b = Counting("json_set_1256_b", 1, 8);
    std::string before;
    {
      PokeRig r("json_set_1256_a", 0.5f, 4);
      REQUIRE(r.p.CreateObject(YSE::OBJ::D_PEEK, "json_set_1256_a") != nullptr);
      before = r.p.DumpJSON();
      r.Message("set json_set_1256_b");
      r.Settle();
      CHECK(r.poke->Buffer() == b);
      // The creation arguments are what is saved.
      CHECK(r.p.DumpJSON() == before);
    }
    CHECK(before.find("json_set_1256_b") == std::string::npos);
    a->fill(0.f);
    patcherImplementation dst(1, nullptr);
    dst.ParseJSON(before);
    dst.Calculate(YSE::T_DSP);
    CHECK(At(*a, 0, 4) == 0.5f); // the reload writes a again
  }

  TEST_CASE("~poke: a set in flight survives the object's deletion (#1256)") {
    auto a = Counting("poke_deleted_1256", 1, 8);
    const long held = a.use_count();
    {
      PokeRig r("", 0.f, -1);
      r.Message("set poke_deleted_1256");
      r.p.DeleteObject(r.handle);
      r.p.FileIO()->WaitIdle();
      r.p.Calculate(YSE::T_DSP);
      r.p.FileIO()->WaitIdle();
    }
    // Whatever the task bound was freed with the object.
    CHECK(a.use_count() == held);
  }

  TEST_CASE("~peek / ~poke: set allocates and locks nothing on the audio thread (#1256)") {
    auto a = Counting("rt_set_1256_a", 1, 64);
    auto b = Counting("rt_set_1256_b", 1, 64);
    PokeRig r("rt_set_1256_a", 0.5f, 7);
    YSE::pHandle* peekHandle = r.p.CreateObject(YSE::OBJ::D_PEEK, "rt_set_1256_a");
    REQUIRE(peekHandle != nullptr);
    YSE::PATCHER::pObject* peek = Object(r.p, peekHandle);
    r.Settle();
    // Built before the probe: the strings themselves allocate.
    const std::string toB = "set rt_set_1256_b";
    const std::string toA = "set rt_set_1256_a";
    for (const std::string* m : {&toB, &toA, &toB}) {
      CAPTURE(*m);
      {
        // Lock-free too: the request is a CAS into the scheduler's table.
        TestHelpers::ProbeScope probe;
        r.Message(*m, YSE::T_DSP);
        peek->GetInlet(0)->SetMessage(*m, YSE::T_DSP);
        CHECK(TestHelpers::g_alloc_count.load() == 0);
      }
      r.p.FileIO()->WaitIdle();
      {
        // The block that takes the new writers and asks for the old ones to
        // go, and the index that does the same for ~peek.
        TestHelpers::ProbeScope probe;
        r.p.Calculate(YSE::T_DSP);
        peek->GetInlet(1)->SetFloat(0.25f, YSE::T_DSP);
        peek->GetInlet(0)->SetInt(9, YSE::T_DSP);
        CHECK(TestHelpers::g_alloc_count.load() == 0);
      }
      r.p.FileIO()->WaitIdle();
      CHECK_FALSE(r.poke->ReleasePending());
      CHECK_FALSE(static_cast<dPeek*>(peek)->ReleasePending());
      r.p.Calculate(YSE::T_DSP); // hands the slots back
    }
    CHECK(r.poke->Buffer() == b);
    CHECK(static_cast<dPeek*>(peek)->Buffer() == b);
    CHECK(At(*b, 0, 7) == 0.5f);
    CHECK(At(*b, 0, 9) == 0.25f);
    CHECK(r.p.FileIO()->Dropped() == 0);
  }

  TEST_CASE("~peek / ~poke: control threads set while the audio thread writes (#1256)") {
    // Two buffers of different lengths, filled 1 and 2; ~poke writes -1 all
    // over whichever it holds. Every value ~peek reads is whole, and the
    // buffers are never freed under a writer. Under ASan or TSan a broken
    // handoff shows up here.
    auto ones = sampleBufferRegistry::acquire("race_set_1256_1");
    ones->setSize(YSE::STANDARD_BUFFERSIZE);
    ones->fill(1.f);
    auto twos = sampleBufferRegistry::acquire("race_set_1256_2");
    twos->setSize(YSE::STANDARD_BUFFERSIZE * 2);
    twos->fill(2.f);
    TestHelpers::FloatSink floats; // before the ~peek that sends to it
    auto sinkHandle = std::make_unique<YSE::pHandle>(&floats);
    patcherImplementation p(1, nullptr);
    YSE::pHandle* value = p.CreateObject(YSE::OBJ::D_SIG, "-1");
    YSE::pHandle* index = p.CreateObject(
        YSE::OBJ::D_COUNT, "0 " + std::to_string(YSE::STANDARD_BUFFERSIZE * 2) + " 1");
    YSE::pHandle* pokeHandle = p.CreateObject(YSE::OBJ::D_POKE, "race_set_1256_1");
    YSE::pHandle* peekHandle = p.CreateObject(YSE::OBJ::D_PEEK, "race_set_1256_2");
    p.Connect(value, 0, pokeHandle, 0);
    p.Connect(index, 0, pokeHandle, 1);
    auto* poke = static_cast<dPoke*>(Object(p, pokeHandle));
    YSE::PATCHER::pObject* peek = Object(p, peekHandle);
    p.Connect(peekHandle, 0, sinkHandle.get(), 0);
    const std::string toOnes = "set race_set_1256_1";
    const std::string toTwos = "set race_set_1256_2";

    std::atomic<bool> stop{false};
    std::atomic<int> reads{0};
    std::atomic<int> bad{0};
    std::thread pokeControl([&] {
      int n = 0;
      while (!stop.load()) {
        poke->GetInlet(0)->SetMessage((n++ % 2) != 0 ? toOnes : toTwos, YSE::T_GUI);
        if (n % 8 == 0) poke->GetInlet(0)->SetList("-1 3", YSE::T_GUI);
        std::this_thread::yield();
      }
    });
    std::thread peekControl([&] {
      int n = 0;
      while (!stop.load()) {
        if (n % 16 == 0)
          peek->GetInlet(0)->SetMessage((n / 16) % 2 != 0 ? toOnes : toTwos, YSE::T_GUI);
        floats.gotFloat = false;
        peek->GetInlet(0)->SetInt(n++ % static_cast<int>(YSE::STANDARD_BUFFERSIZE), YSE::T_GUI);
        if (floats.gotFloat) {
          const float v = floats.received;
          if (!(v == 1.f || v == 2.f || v == -1.f)) bad.fetch_add(1);
          reads.fetch_add(1);
        }
        std::this_thread::yield();
      }
    });

    int switches = 0;
    const YSE::DSP::sampleBuffer* last = poke->Buffer().get();
    for (int block = 0; block < 20000 && (block < 2000 || switches == 0 || reads.load() < 100);
         ++block) {
      p.Calculate(YSE::T_DSP);
      // Between blocks, on the thread that takes: safe to look.
      const YSE::DSP::sampleBuffer* now = poke->Buffer().get();
      if (now != last) switches++;
      last = now;
    }
    stop.store(true);
    pokeControl.join();
    peekControl.join();
    CHECK(bad.load() == 0);
    CHECK(reads.load() > 0);
    CHECK(switches > 0);

    // Quiet again, everything let go is freed.
    for (int i = 0; i < 3; ++i) {
      p.FileIO()->WaitIdle();
      p.Calculate(YSE::T_DSP);
    }
    p.FileIO()->WaitIdle();
    CHECK_FALSE(poke->ReleasePending());
  }

} // TEST_SUITE("patcher")
