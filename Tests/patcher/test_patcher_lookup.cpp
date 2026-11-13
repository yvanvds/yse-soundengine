// Tests for the ~lookup patcher object (patcher/sampling/dLookup), issue
// #1147. The sample math is DSP::transferLookup's
// (Tests/dsp/test_transfer_lookup.cpp) and the reader is
// DSP::sampleBufferReader's (test_sampleBuffer.cpp). These show that pins,
// messages and arguments reach them, in rendered patches heard through a
// ~dac: a ~cycle waveshaped by a drawn curve, the number fallback, the
// offset and size by argument, number, message and signal, the channel, a
// ~buffer edit, set (#1256), a round trip, allocation-free blocks and
// messages on the audio thread, and edits made while it renders.

#include <doctest/doctest.h>
#include <algorithm>
#include <atomic>
#include <cmath>
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
#include "patcher/sampling/dLookup.h"
#include "support/alloc_probe.hpp"
#include "sinks.hpp"

using YSE::STANDARD_BUFFERSIZE;
using YSE::DSP::sampleBuffer;
using YSE::DSP::sampleBufferRegistry;
using YSE::PATCHER::dLookup;
using YSE::PATCHER::patcherImplementation;

namespace {

  // A named buffer whose frame n of channel c is (c + 1) * 10 * n.
  std::shared_ptr<sampleBuffer> Tens(const std::string& name, UInt frames, UInt channels = 1) {
    std::shared_ptr<sampleBuffer> b = sampleBufferRegistry::acquire(name);
    b->setSize(frames, static_cast<Int>(channels));
    for (UInt c = 0; c < channels; ++c) {
      std::vector<float> values(frames);
      for (UInt n = 0; n < frames; ++n)
        values[n] = static_cast<float>((c + 1) * 10 * n);
      b->setSamples(c, 0, values.data(), frames);
    }
    return b;
  }

  // A named buffer holding f(t) at `points` positions spread from -1 to 1.
  template <typename F>
  std::shared_ptr<sampleBuffer> Curve(const std::string& name, UInt points, F f) {
    std::shared_ptr<sampleBuffer> b = sampleBufferRegistry::acquire(name);
    b->setSize(points, 1);
    std::vector<float> values(points);
    for (UInt n = 0; n < points; ++n) {
      const double t = -1.0 + 2.0 * static_cast<double>(n) / static_cast<double>(points - 1);
      values[n] = static_cast<float>(f(t));
    }
    b->setSamples(0, 0, values.data(), points);
    return b;
  }

  float Out(patcherImplementation& p, UInt i, UInt channel = 0) {
    return p.output[channel].getPtr()[i];
  }

  // [source] -> [~lookup args] -> [~dac], `source` made from `type` and
  // `sourceArgs`, or no source at all when `type` is empty.
  struct LookupRig {
    patcherImplementation p{1, nullptr};
    YSE::pHandle* source = nullptr;
    YSE::pHandle* handle = nullptr;
    YSE::pHandle* dac = nullptr;
    dLookup* lookup = nullptr;

    LookupRig(const std::string& args, const char* type, const std::string& sourceArgs) {
      handle = p.CreateObject(YSE::OBJ::D_LOOKUP, args);
      REQUIRE(handle != nullptr);
      dac = p.CreateObject(YSE::OBJ::D_DAC, "");
      REQUIRE(dac != nullptr);
      if (type != nullptr && *type != '\0') {
        source = p.CreateObject(type, sourceArgs);
        REQUIRE(source != nullptr);
        p.Connect(source, 0, handle, 0);
      }
      p.Connect(handle, 0, dac, 0);
      lookup = static_cast<dLookup*>(p.GetObjectFromID(handle->GetID()));
    }

    void Message(const std::string& text, YSE::THREAD thread = YSE::T_GUI) {
      lookup->GetInlet(0)->SetMessage(text, thread);
    }

    // What the next block reads, at its first sample.
    float Block() {
      p.Calculate(YSE::T_DSP);
      return Out(p, 0);
    }

    // The bind task runs on the background pool, the next block takes the
    // reader, and the task that frees the old one runs after it.
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

  TEST_CASE("~lookup: registry and shape (#1147)") {
    YSE::patcher p;
    p.create(1);
    YSE::pHandle* h = p.CreateObject(YSE::OBJ::D_LOOKUP, "shape_1147");
    REQUIRE(h != nullptr);
    CHECK(std::string(h->Type()) == "~lookup");
    CHECK(h->GetInputs() == 3);
    REQUIRE(h->GetOutputs() == 1);
    CHECK(h->OutputDataType(0) == YSE::OUT_TYPE::BUFFER);

    bool listed = false;
    for (const auto& name : YSE::PATCHER::Register().AllNames())
      if (name == YSE::OBJ::D_LOOKUP) listed = true;
    CHECK(listed);

    dLookup standalone;
    CHECK(standalone.GetCategory() == YSE::PATCHER::pCategory::SAMPLING);
    CHECK(standalone.CurrentOffset() == 0);
    CHECK(standalone.CurrentSize() == 512);
    CHECK(standalone.CurrentChannel() == 1);
    CHECK(standalone.Buffer() == nullptr);
  }

  TEST_CASE("~lookup waveshapes a ~cycle through a drawn curve (#1147)") {
    // The patch a user builds: [~cycle] -> [~lookup curve 0 1025] -> [~dac],
    // the ~cycle heard on a second channel too. A squaring curve gives the
    // ~cycle squared, within the linear-interpolation bound; an identity
    // line gives the ~cycle back.
    constexpr UInt POINTS = 1025;
    auto square = Curve("square_1147", POINTS, [](double t) { return t * t; });
    auto line = Curve("line_1147", POINTS, [](double t) { return t; });

    patcherImplementation p(2, nullptr);
    YSE::pHandle* cycle = p.CreateObject(YSE::OBJ::D_CYCLE, "441");
    YSE::pHandle* shaper = p.CreateObject(YSE::OBJ::D_LOOKUP, "square_1147 0 1025");
    YSE::pHandle* dac = p.CreateObject(YSE::OBJ::D_DAC, "");
    REQUIRE(cycle != nullptr);
    REQUIRE(shaper != nullptr);
    REQUIRE(dac != nullptr);
    p.Connect(cycle, 0, shaper, 0);
    p.Connect(shaper, 0, dac, 0);
    p.Connect(cycle, 0, dac, 1);

    const double h = 2.0 / static_cast<double>(POINTS - 1);
    const double bound = h * h / 8.0 * 2.0 + 1e-5;
    double loudest = 0.0;
    for (int block = 0; block < 4; ++block) {
      p.Calculate(YSE::T_DSP);
      for (UInt i = 0; i < STANDARD_BUFFERSIZE; ++i) {
        CAPTURE(i);
        const double x = Out(p, i, 1);
        loudest = std::max(loudest, std::fabs(x));
        CHECK(std::fabs(static_cast<double>(Out(p, i, 0)) - x * x) <= bound);
      }
    }
    CHECK(loudest > 0.9); // the ~cycle really swung across the curve

    auto* shaperObject = static_cast<dLookup*>(p.GetObjectFromID(shaper->GetID()));
    shaperObject->GetInlet(0)->SetMessage("set line_1147", YSE::T_GUI);
    p.FileIO()->WaitIdle();
    p.Calculate(YSE::T_DSP);
    p.FileIO()->WaitIdle();
    CHECK(shaperObject->Buffer() == line);
    p.Calculate(YSE::T_DSP);
    for (UInt i = 0; i < STANDARD_BUFFERSIZE; ++i) {
      CAPTURE(i);
      CHECK(std::fabs(Out(p, i, 0) - Out(p, i, 1)) <= 1e-5f);
    }
  }

  TEST_CASE("~lookup: -1 to 1 across the table, 0 beyond; a number while no signal is "
            "connected (#1147)") {
    auto b = Tens("num_1147", 5);
    LookupRig r("num_1147", "", "");
    // The default input is 0: the middle entry.
    CHECK(r.Block() == 20.f);
    CHECK(Out(r.p, STANDARD_BUFFERSIZE - 1) == 20.f);
    r.lookup->GetInlet(0)->SetFloat(-1.f, YSE::T_GUI);
    CHECK(r.Block() == 0.f);
    r.lookup->GetInlet(0)->SetInt(1, YSE::T_GUI);
    CHECK(r.Block() == 40.f);
    r.lookup->GetInlet(0)->SetFloat(0.25f, YSE::T_GUI);
    CHECK(r.Block() == 25.f);
    r.lookup->GetInlet(0)->SetFloat(1.5f, YSE::T_GUI);
    CHECK(r.Block() == 0.f);
    r.lookup->GetInlet(0)->SetInt(-2, YSE::T_GUI);
    CHECK(r.Block() == 0.f);
  }

  TEST_CASE("~lookup: a connected signal wins over the number (#1147)") {
    auto b = Tens("sigwins_1147", 5);
    LookupRig r("sigwins_1147", YSE::OBJ::D_SIG, "0.5");
    r.lookup->GetInlet(0)->SetFloat(-1.f, YSE::T_GUI); // ignored while the cord is in
    CHECK(r.Block() == 30.f);
    r.source->SetFloatData(0, -0.5f);
    CHECK(r.Block() == 10.f);
    r.source->SetFloatData(0, 1.01f);
    CHECK(r.Block() == 0.f);
    // Without the cord the number is back.
    r.p.Disconnect(r.source, 0, r.handle, 0);
    CHECK(r.Block() == 0.f);
    r.lookup->GetInlet(0)->SetFloat(1.f, YSE::T_GUI);
    CHECK(r.Block() == 40.f);
  }

  TEST_CASE("~lookup: offset and size by argument, number and message (#1147)") {
    auto b = Tens("range_1147", 20);
    // Frames 4 .. 8: -1 reads 40, 1 reads 80.
    LookupRig r("range_1147 4 9", YSE::OBJ::D_SIG, "-1");
    CHECK(r.lookup->CurrentOffset() == 4);
    CHECK(r.lookup->CurrentSize() == 9);
    CHECK(r.Block() == 40.f);
    r.source->SetFloatData(0, 1.f);
    CHECK(r.Block() == 80.f);
    r.source->SetFloatData(0, 0.f);
    CHECK(r.Block() == 60.f);

    // Numbers in the middle and right inlets; a float is truncated.
    r.lookup->GetInlet(1)->SetInt(2, YSE::T_GUI);
    r.lookup->GetInlet(2)->SetFloat(11.9f, YSE::T_GUI);
    CHECK(r.lookup->CurrentOffset() == 2);
    CHECK(r.lookup->CurrentSize() == 11);
    CHECK(r.Block() == 60.f); // frames 2 .. 10, middle 6
    r.source->SetFloatData(0, 1.f);
    CHECK(r.Block() == 100.f);

    // Messages in the left inlet.
    r.Message("offset 10");
    r.Message("size 15");
    CHECK(r.lookup->CurrentOffset() == 10);
    CHECK(r.lookup->CurrentSize() == 15);
    CHECK(r.Block() == 140.f);
    r.source->SetFloatData(0, -1.f);
    CHECK(r.Block() == 100.f);

    // Below 0 is 0; a size past the buffer stops at its last frame.
    r.lookup->GetInlet(1)->SetInt(-5, YSE::T_GUI);
    r.Message("size 1e9");
    CHECK(r.lookup->CurrentOffset() == 0);
    CHECK(r.Block() == 0.f);
    r.source->SetFloatData(0, 1.f);
    CHECK(r.Block() == 190.f);
    // An offset at or past the size: no entries, silence.
    r.Message("offset 30");
    CHECK(r.Block() == 0.f);
    r.Message("offset 3");
    r.Message("size 3");
    CHECK(r.Block() == 0.f);
    // Not a number: ignored.
    r.Message("size none");
    r.Message("offset");
    CHECK(r.lookup->CurrentOffset() == 3);
    CHECK(r.lookup->CurrentSize() == 3);
  }

  TEST_CASE("~lookup: offset and size as signals, read at each block's first sample "
            "(#1147)") {
    auto b = Tens("sigrange_1147", 20);
    patcherImplementation p(1, nullptr);
    YSE::pHandle* in = p.CreateObject(YSE::OBJ::D_SIG, "1");
    YSE::pHandle* offset = p.CreateObject(YSE::OBJ::D_SIG, "5");
    YSE::pHandle* size = p.CreateObject(YSE::OBJ::D_SIG, "8");
    YSE::pHandle* shaper = p.CreateObject(YSE::OBJ::D_LOOKUP, "sigrange_1147 0 20");
    YSE::pHandle* dac = p.CreateObject(YSE::OBJ::D_DAC, "");
    REQUIRE(shaper != nullptr);
    p.Connect(in, 0, shaper, 0);
    p.Connect(offset, 0, shaper, 1);
    p.Connect(size, 0, shaper, 2);
    p.Connect(shaper, 0, dac, 0);
    auto* object = static_cast<dLookup*>(p.GetObjectFromID(shaper->GetID()));

    p.Calculate(YSE::T_DSP);
    CHECK(Out(p, 0) == 70.f); // frames 5 .. 7
    CHECK(Out(p, STANDARD_BUFFERSIZE - 1) == 70.f);
    in->SetFloatData(0, -1.f);
    p.Calculate(YSE::T_DSP);
    CHECK(Out(p, 0) == 50.f);
    // A number does not change the audio while a cord is in, nor does a
    // connected signal change the stored number.
    object->GetInlet(1)->SetInt(1, YSE::T_GUI);
    p.Calculate(YSE::T_DSP);
    CHECK(Out(p, 0) == 50.f);
    CHECK(object->CurrentOffset() == 1);
    CHECK(object->CurrentSize() == 20);
    offset->SetFloatData(0, 2.7f);
    p.Calculate(YSE::T_DSP);
    CHECK(Out(p, 0) == 20.f); // truncated
    size->SetFloatData(0, -4.f);
    p.Calculate(YSE::T_DSP);
    CHECK(Out(p, 0) == 0.f); // no entries
    // The cords out: the numbers are back.
    p.Disconnect(offset, 0, shaper, 1);
    p.Disconnect(size, 0, shaper, 2);
    p.Calculate(YSE::T_DSP);
    CHECK(Out(p, 0) == 10.f);
  }

  TEST_CASE("~lookup: the channel, from 1, by argument and by message (#1147)") {
    auto b = Tens("chan_1147", 5, 3);
    LookupRig r("chan_1147 0 512 2", YSE::OBJ::D_SIG, "1");
    CHECK(r.lookup->CurrentChannel() == 2);
    CHECK(r.Block() == 80.f);
    r.Message("chan 3");
    CHECK(r.lookup->CurrentChannel() == 3);
    CHECK(r.Block() == 120.f);
    // A float is truncated; below 1 is 1.
    r.Message("chan 1.9");
    CHECK(r.lookup->CurrentChannel() == 1);
    r.Message("chan 2");
    r.Message("chan -4");
    CHECK(r.lookup->CurrentChannel() == 1);
    CHECK(r.Block() == 40.f);
    // A channel the buffer lacks: silence.
    r.Message("chan 4");
    CHECK(r.Block() == 0.f);
  }

  TEST_CASE("~lookup: no name, or a buffer filled later, resized and edited by ~buffer "
            "(#1147)") {
    {
      LookupRig r("", YSE::OBJ::D_SIG, "0");
      CHECK(r.Block() == 0.f);
    }
    // The name is acquired when the object is made; the samples come later.
    LookupRig r("later_1147", YSE::OBJ::D_SIG, "1");
    CHECK(r.Block() == 0.f);
    auto b = Tens("later_1147", 9);
    CHECK(r.Block() == 80.f);
    // A resize is seen at the next block: the table follows the buffer.
    b->setSize(3);
    CHECK(r.Block() == 20.f);

    // A ~buffer of the same name, and its fill, heard from a later block.
    YSE::pHandle* buffer = r.p.CreateObject(YSE::OBJ::D_BUFFER, "later_1147");
    REQUIRE(buffer != nullptr);
    r.p.GetObjectFromID(buffer->GetID())->GetInlet(0)->SetMessage("fill 0.75", YSE::T_GUI);
    r.p.FileIO()->WaitIdle();
    r.p.Calculate(YSE::T_DSP);
    r.p.FileIO()->WaitIdle();
    CHECK(r.Block() == 0.75f);
  }

  TEST_CASE("~lookup: set reads another buffer from a later block (#1147)") {
    auto a = Tens("set_1147_a", 5);
    auto b = Tens("set_1147_b", 5);
    b->applyOffset(100.f);
    LookupRig r("set_1147_a", YSE::OBJ::D_SIG, "1");
    CHECK(r.Block() == 40.f);

    r.Message("set set_1147_b");
    r.Settle();
    CHECK(r.lookup->Buffer() == b);
    CHECK_FALSE(r.lookup->ReleasePending());
    CHECK(r.Block() == 140.f);

    // set with no name: unbound, silence.
    r.Message("set");
    r.Settle();
    CHECK(r.lookup->Buffer() == nullptr);
    CHECK(r.Block() == 0.f);
    CHECK(r.p.FileIO()->Dropped() == 0);

    // Outside a patcher a set does nothing.
    dLookup standalone;
    standalone.SetParams("set_1147_a");
    standalone.GetInlet(0)->SetMessage("set set_1147_b", YSE::T_GUI);
    CHECK(standalone.Buffer() == a);
  }

  TEST_CASE("~lookup: arguments survive a DumpJSON / ParseJSON round trip; messages are not "
            "saved (#1147)") {
    auto a = Tens("json_1147_a", 20, 2);
    auto b = Tens("json_1147_b", 20, 2);
    std::string dump;
    {
      LookupRig r("json_1147_a 2 7 2", YSE::OBJ::D_SIG, "1");
      dump = r.p.DumpJSON();
      CHECK(dump.find("json_1147_a 2 7 2") != std::string::npos);
      r.Message("set json_1147_b");
      r.Settle();
      CHECK(r.lookup->Buffer() == b);
      CHECK(r.p.DumpJSON() == dump);
      r.Message("offset 3");
      r.Message("size 12");
      r.Message("chan 1");
      r.lookup->GetInlet(1)->SetInt(4, YSE::T_GUI);
      r.lookup->GetInlet(2)->SetInt(10, YSE::T_GUI);
      CHECK(r.p.DumpJSON() == dump);
    }
    CHECK(dump.find("json_1147_b") == std::string::npos);
    patcherImplementation dst(1, nullptr);
    dst.ParseJSON(dump);
    CHECK(dst.DumpJSON() == dump);
    // The reloaded ~lookup reads frames 2 .. 6 of channel 2 of a again.
    dst.Calculate(YSE::T_DSP);
    CHECK(Out(dst, 0) == 120.f);
  }

  TEST_CASE("~lookup: blocks, numbers and messages on the audio thread allocate nothing "
            "(#1147)") {
    auto a = Tens("rt_1147_a", 1024, 2);
    auto b = Tens("rt_1147_b", 512, 2);
    LookupRig r("rt_1147_a", YSE::OBJ::D_CYCLE, "100");
    r.Settle();
    // Built before the probe: the strings themselves allocate.
    const std::string toB = "set rt_1147_b";
    const std::string toA = "set rt_1147_a";
    const std::string chan = "chan 2";
    const std::string offset = "offset 3";
    const std::string size = "size 600";
    for (const std::string* m : {&toB, &toA, &toB}) {
      CAPTURE(*m);
      {
        // Lock-free too: the request is a CAS into the scheduler's table.
        TestHelpers::ProbeScope probe;
        r.Message(*m, YSE::T_DSP);
        r.Message(chan, YSE::T_DSP);
        r.Message(offset, YSE::T_DSP);
        r.Message(size, YSE::T_DSP);
        r.lookup->GetInlet(0)->SetFloat(0.5f, YSE::T_DSP);
        r.lookup->GetInlet(0)->SetInt(0, YSE::T_DSP);
        r.lookup->GetInlet(1)->SetFloat(1.f, YSE::T_DSP);
        r.lookup->GetInlet(1)->SetInt(2, YSE::T_DSP);
        r.lookup->GetInlet(2)->SetFloat(500.f, YSE::T_DSP);
        r.lookup->GetInlet(2)->SetInt(700, YSE::T_DSP);
        CHECK(TestHelpers::g_alloc_count.load() == 0);
      }
      r.p.FileIO()->WaitIdle();
      {
        // The block that takes the new reader and asks for the old one to go.
        TestHelpers::ProbeScope probe;
        r.p.Calculate(YSE::T_DSP);
        r.p.Calculate(YSE::T_DSP);
        CHECK(TestHelpers::g_alloc_count.load() == 0);
      }
      r.p.FileIO()->WaitIdle();
      CHECK_FALSE(r.lookup->ReleasePending());
      r.p.Calculate(YSE::T_DSP); // hands the slots back
    }
    CHECK(r.lookup->Buffer() == b);
    CHECK(r.p.FileIO()->Dropped() == 0);
  }

  TEST_CASE("~lookup renders while control threads edit and set its buffers (#1147)") {
    // Two buffers of different lengths filled 1 and 2, edited and switched
    // from control threads while the audio thread shapes a ~cycle through
    // them. Every sample heard is in reach of what was written: 1, 2, the 3
    // an edit fills with, the 0 between a resize and its fill, or a line
    // between two of those. Under ASan or TSan a freed storage or a broken
    // handoff shows up here.
    auto ones = sampleBufferRegistry::acquire("race_1147_1");
    ones->setSize(STANDARD_BUFFERSIZE);
    ones->fill(1.f);
    auto twos = sampleBufferRegistry::acquire("race_1147_2");
    twos->setSize(STANDARD_BUFFERSIZE * 3);
    twos->fill(2.f);
    LookupRig r("race_1147_1", YSE::OBJ::D_CYCLE, "300");
    const std::string toOnes = "set race_1147_1";
    const std::string toTwos = "set race_1147_2";

    std::atomic<bool> stop{false};
    std::thread setter([&] {
      int n = 0;
      while (!stop.load()) {
        r.Message((n++ % 2) != 0 ? toOnes : toTwos, YSE::T_GUI);
        std::this_thread::yield();
      }
    });
    std::thread editor([&] {
      int n = 0;
      while (!stop.load()) {
        // A resize swaps the storage; the fill writes a value of its own.
        ones->setSize(STANDARD_BUFFERSIZE + static_cast<UInt>(n % 7));
        ones->fill(n % 2 != 0 ? 1.f : 3.f);
        n++;
        std::this_thread::yield();
      }
    });

    int bad = 0;
    int switches = 0;
    const sampleBuffer* last = r.lookup->Buffer().get();
    for (int block = 0; block < 20000 && (block < 2000 || switches == 0); ++block) {
      r.p.Calculate(YSE::T_DSP);
      for (UInt i = 0; i < STANDARD_BUFFERSIZE; ++i) {
        const float v = Out(r.p, i);
        if (!(v >= 0.f && v <= 3.f)) bad++;
      }
      const sampleBuffer* now = r.lookup->Buffer().get();
      if (now != last) switches++;
      last = now;
    }
    stop.store(true);
    setter.join();
    editor.join();
    CHECK(bad == 0);
    CHECK(switches > 0);
  }

} // TEST_SUITE
