// Tests for the ~peek and ~poke patcher objects (patcher/sampling/dPeek,
// dPoke), issue #1144. The sample math is DSP::sampleAccess's and
// DSP::sampleIndexWriter's (Tests/dsp/test_sample_access.cpp) and the
// in-place writer is DSP::sampleBufferWriter's (test_sampleBuffer.cpp).
// These show that pins, messages and arguments reach them, alone and in a
// rendered patch beside a ~buffer: ~poke recording a signal that ~peek reads
// back, ~peek writing what a host reads, a control thread peeking while the
// audio thread pokes, a round trip, and allocation-free messages and blocks.

#include <doctest/doctest.h>
#include <atomic>
#include <memory>
#include <string>
#include <thread>
#include <vector>
#include "dsp/sampling/sampleBuffer.hpp"
#include "headers/constants.hpp"
#include "patcher/pHandle.hpp"
#include "patcher/pObjectList.hpp"
#include "patcher/pRegistry.h"
#include "patcher/patcher.hpp"
#include "patcher/patcherImplementation.h"
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

} // TEST_SUITE("patcher")
