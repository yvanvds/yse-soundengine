// Tests for the ~buffir patcher object (patcher/sampling/dBuffir), issue
// #1149. The sample math is DSP::firFilter's (Tests/dsp/test_fir_filter.cpp)
// and the reader is DSP::sampleBufferReader's (test_sampleBuffer.cpp). These
// show that pins, messages and arguments reach them, in rendered patches
// heard through a ~dac: a ~click's impulse response, a ~cycle filtered by a
// buffer kernel, the number fallback, the offset and size by argument,
// number, message, set and signal, the channel, clear, a ~buffer edit, set
// (#1256), a round trip, allocation-free blocks and messages on the audio
// thread, and edits made while it renders.

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
#include "patcher/sampling/dBuffir.h"
#include "support/alloc_probe.hpp"
#include "sinks.hpp"

using YSE::STANDARD_BUFFERSIZE;
using YSE::DSP::sampleBuffer;
using YSE::DSP::sampleBufferRegistry;
using YSE::PATCHER::dBuffir;
using YSE::PATCHER::patcherImplementation;

namespace {

  // A named buffer whose frame n of channel c is (c + 1) * 10 * n + 1.
  std::shared_ptr<sampleBuffer> Tens(const std::string& name, UInt frames, UInt channels = 1) {
    std::shared_ptr<sampleBuffer> b = sampleBufferRegistry::acquire(name);
    b->setSize(frames, static_cast<Int>(channels));
    for (UInt c = 0; c < channels; ++c) {
      std::vector<float> values(frames);
      for (UInt n = 0; n < frames; ++n)
        values[n] = static_cast<float>((c + 1) * 10 * n + 1);
      b->setSamples(c, 0, values.data(), frames);
    }
    return b;
  }

  // A named one-channel buffer holding `values`.
  std::shared_ptr<sampleBuffer> Kernel(const std::string& name, std::vector<float> values) {
    std::shared_ptr<sampleBuffer> b = sampleBufferRegistry::acquire(name);
    b->setSize(static_cast<UInt>(values.size()), 1);
    b->setSamples(0, 0, values.data(), static_cast<UInt>(values.size()));
    return b;
  }

  // The sum of frames first .. first + count - 1 of a Tens channel.
  float TensSum(UInt first, UInt count, UInt channel = 1) {
    float sum = 0.f;
    for (UInt n = first; n < first + count; ++n)
      sum += static_cast<float>(channel * 10 * n + 1);
    return sum;
  }

  float Out(patcherImplementation& p, UInt i, UInt channel = 0) {
    return p.output[channel].getPtr()[i];
  }

  // [source] -> [~buffir args] -> [~dac], `source` made from `type` and
  // `sourceArgs`, or no source at all when `type` is empty.
  struct BuffirRig {
    patcherImplementation p{1, nullptr};
    YSE::pHandle* source = nullptr;
    YSE::pHandle* handle = nullptr;
    YSE::pHandle* dac = nullptr;
    dBuffir* fir = nullptr;

    BuffirRig(const std::string& args, const char* type, const std::string& sourceArgs) {
      handle = p.CreateObject(YSE::OBJ::D_BUFFIR, args);
      REQUIRE(handle != nullptr);
      dac = p.CreateObject(YSE::OBJ::D_DAC, "");
      REQUIRE(dac != nullptr);
      if (type != nullptr && *type != '\0') {
        source = p.CreateObject(type, sourceArgs);
        REQUIRE(source != nullptr);
        p.Connect(source, 0, handle, 0);
      }
      p.Connect(handle, 0, dac, 0);
      fir = static_cast<dBuffir*>(p.GetObjectFromID(handle->GetID()));
    }

    void Message(const std::string& text, YSE::THREAD thread = YSE::T_GUI) {
      fir->GetInlet(0)->SetMessage(text, thread);
    }

    // What the next block outputs at sample `i`.
    float Block(UInt i = STANDARD_BUFFERSIZE - 1) {
      p.Calculate(YSE::T_DSP);
      return Out(p, i);
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

  TEST_CASE("~buffir: registry and shape (#1149)") {
    YSE::patcher p;
    p.create(1);
    YSE::pHandle* h = p.CreateObject(YSE::OBJ::D_BUFFIR, "shape_1149");
    REQUIRE(h != nullptr);
    CHECK(std::string(h->Type()) == "~buffir");
    CHECK(h->GetInputs() == 3);
    REQUIRE(h->GetOutputs() == 1);
    CHECK(h->OutputDataType(0) == YSE::OUT_TYPE::BUFFER);

    bool listed = false;
    for (const auto& name : YSE::PATCHER::Register().AllNames())
      if (name == YSE::OBJ::D_BUFFIR) listed = true;
    CHECK(listed);

    dBuffir standalone;
    CHECK(standalone.GetCategory() == YSE::PATCHER::pCategory::SAMPLING);
    CHECK(standalone.CurrentOffset() == 0);
    CHECK(standalone.CurrentSize() == 0);
    CHECK(standalone.CurrentChannel() == 1);
    CHECK(standalone.Buffer() == nullptr);
  }

  TEST_CASE("~buffir: a ~click through it is heard as the taps (#1149)") {
    // The patch a user builds to hear an impulse response:
    // [~click] -> [~buffir ir 0 200] -> [~dac]. A bang plays the 200 taps,
    // across blocks, then silence.
    auto ir = Tens("ir_1149", 300);
    patcherImplementation p(1, nullptr);
    YSE::pHandle* click = p.CreateObject(YSE::OBJ::D_CLICK, "");
    YSE::pHandle* fir = p.CreateObject(YSE::OBJ::D_BUFFIR, "ir_1149 0 200");
    YSE::pHandle* dac = p.CreateObject(YSE::OBJ::D_DAC, "");
    REQUIRE(click != nullptr);
    REQUIRE(fir != nullptr);
    REQUIRE(dac != nullptr);
    p.Connect(click, 0, fir, 0);
    p.Connect(fir, 0, dac, 0);

    p.Calculate(YSE::T_DSP);
    CHECK(Out(p, 0) == 0.f);
    p.GetObjectFromID(click->GetID())->GetInlet(0)->SetBang(YSE::T_GUI);
    UInt sample = 0;
    for (int block = 0; block < 4; ++block) {
      p.Calculate(YSE::T_DSP);
      for (UInt i = 0; i < STANDARD_BUFFERSIZE; ++i, ++sample) {
        CAPTURE(sample);
        CHECK(Out(p, i) == (sample < 200 ? static_cast<float>(10 * sample + 1) : 0.f));
      }
    }
  }

  TEST_CASE("~buffir filters a ~cycle by a buffer kernel; set switches kernels (#1149)") {
    // [~cycle] -> [~buffir delay_1149 0 4] -> [~dac], the ~cycle heard on a
    // second channel too. Taps {0, 0, 0, 0.5}: the ~cycle at half, three
    // samples late. Then set to {0.5, 0.5}: the mean of two neighbours.
    auto delay = Kernel("delay_1149", {0.f, 0.f, 0.f, 0.5f});
    auto mean = Kernel("mean_1149", {0.5f, 0.5f});

    patcherImplementation p(2, nullptr);
    YSE::pHandle* cycle = p.CreateObject(YSE::OBJ::D_CYCLE, "441");
    YSE::pHandle* fir = p.CreateObject(YSE::OBJ::D_BUFFIR, "delay_1149 0 4");
    YSE::pHandle* dac = p.CreateObject(YSE::OBJ::D_DAC, "");
    REQUIRE(cycle != nullptr);
    REQUIRE(fir != nullptr);
    REQUIRE(dac != nullptr);
    p.Connect(cycle, 0, fir, 0);
    p.Connect(fir, 0, dac, 0);
    p.Connect(cycle, 0, dac, 1);

    std::vector<float> heard; // the ~cycle, every sample so far
    double loudest = 0.0;
    for (int block = 0; block < 4; ++block) {
      p.Calculate(YSE::T_DSP);
      for (UInt i = 0; i < STANDARD_BUFFERSIZE; ++i) {
        heard.push_back(Out(p, i, 1));
        loudest = std::max(loudest, std::fabs(static_cast<double>(Out(p, i, 1))));
        const std::size_t n = heard.size() - 1;
        const float expected = n < 3 ? 0.f : 0.5f * heard[n - 3];
        CAPTURE(n);
        CHECK(std::fabs(Out(p, i, 0) - expected) <= 1e-6f);
      }
    }
    CHECK(loudest > 0.9); // the ~cycle really swung

    auto* object = static_cast<dBuffir*>(p.GetObjectFromID(fir->GetID()));
    object->GetInlet(0)->SetMessage("set mean_1149 0 2", YSE::T_GUI);
    CHECK(object->CurrentSize() == 2);
    p.FileIO()->WaitIdle();
    for (int block = 0; block < 2; ++block) {
      p.Calculate(YSE::T_DSP);
      for (UInt i = 0; i < STANDARD_BUFFERSIZE; ++i)
        heard.push_back(Out(p, i, 1));
    }
    p.FileIO()->WaitIdle();
    CHECK(object->Buffer() == mean);
    for (UInt i = 0; i < STANDARD_BUFFERSIZE; ++i) {
      const std::size_t n = heard.size() - STANDARD_BUFFERSIZE + i;
      CAPTURE(n);
      CHECK(std::fabs(Out(p, i, 0) - 0.5f * (heard[n] + heard[n - 1])) <= 1e-6f);
    }
  }

  TEST_CASE("~buffir: a number while no signal is connected; a connected signal wins "
            "(#1149)") {
    auto b = Tens("num_1149", 10);
    BuffirRig r("num_1149 0 5", "", "");
    // The default input is 0.
    CHECK(r.Block() == 0.f);
    // A constant 1: the running sum of the taps, then their total.
    r.fir->GetInlet(0)->SetFloat(1.f, YSE::T_GUI);
    CHECK(r.Block(0) == 1.f);
    CHECK(Out(r.p, 1) == TensSum(0, 2));
    CHECK(Out(r.p, 4) == TensSum(0, 5));
    CHECK(Out(r.p, STANDARD_BUFFERSIZE - 1) == TensSum(0, 5));
    r.fir->GetInlet(0)->SetInt(2, YSE::T_GUI);
    CHECK(r.Block() == 2.f * TensSum(0, 5));

    BuffirRig s("num_1149 0 5", YSE::OBJ::D_SIG, "-1");
    s.fir->GetInlet(0)->SetFloat(3.f, YSE::T_GUI); // ignored while the cord is in
    CHECK(s.Block() == -TensSum(0, 5));
    // Without the cord the number is back.
    s.p.Disconnect(s.source, 0, s.handle, 0);
    s.Block();
    CHECK(s.Block() == 3.f * TensSum(0, 5));
  }

  TEST_CASE("~buffir: offset and size by argument, number, message and set (#1149)") {
    auto b = Tens("range_1149", 20);
    // A constant 1 in: each block's last sample is the sum of the taps.
    BuffirRig r("range_1149 4 3", YSE::OBJ::D_SIG, "1");
    CHECK(r.fir->CurrentOffset() == 4);
    CHECK(r.fir->CurrentSize() == 3);
    CHECK(r.Block() == TensSum(4, 3));

    // Numbers in the middle and right inlets; a float is truncated.
    r.fir->GetInlet(1)->SetInt(2, YSE::T_GUI);
    r.fir->GetInlet(2)->SetFloat(6.9f, YSE::T_GUI);
    CHECK(r.fir->CurrentOffset() == 2);
    CHECK(r.fir->CurrentSize() == 6);
    CHECK(r.Block() == TensSum(2, 6));

    // Messages in the left inlet.
    r.Message("offset 10");
    r.Message("size 5");
    CHECK(r.fir->CurrentOffset() == 10);
    CHECK(r.fir->CurrentSize() == 5);
    CHECK(r.Block() == TensSum(10, 5));

    // set with numbers: the offset, then the size; one left out keeps its
    // value. The same buffer, so no rebind to wait for.
    r.Message("set range_1149 1");
    CHECK(r.fir->CurrentOffset() == 1);
    CHECK(r.fir->CurrentSize() == 5);
    r.Message("set range_1149 3 2");
    CHECK(r.fir->CurrentOffset() == 3);
    CHECK(r.fir->CurrentSize() == 2);
    r.Settle();
    CHECK(r.Block() == TensSum(3, 2));

    // Below 0 is 0; a size past the buffer stops at its last frame, and past
    // 4096 is held there.
    r.fir->GetInlet(1)->SetInt(-5, YSE::T_GUI);
    r.Message("size 1e9");
    CHECK(r.fir->CurrentOffset() == 0);
    CHECK(r.Block() == TensSum(0, 20));
    // An offset at or past the end, or size 0: silence.
    r.Message("offset 20");
    CHECK(r.Block() == 0.f);
    r.Message("offset 3");
    r.Message("size 0");
    CHECK(r.Block() == 0.f);
    // Not a number: ignored.
    r.Message("size none");
    r.Message("offset");
    CHECK(r.fir->CurrentOffset() == 3);
    CHECK(r.fir->CurrentSize() == 0);
  }

  TEST_CASE("~buffir: offset and size as signals, read at each block's first sample "
            "(#1149)") {
    auto b = Tens("sigrange_1149", 20);
    patcherImplementation p(1, nullptr);
    YSE::pHandle* in = p.CreateObject(YSE::OBJ::D_SIG, "1");
    YSE::pHandle* offset = p.CreateObject(YSE::OBJ::D_SIG, "5");
    YSE::pHandle* size = p.CreateObject(YSE::OBJ::D_SIG, "3");
    YSE::pHandle* fir = p.CreateObject(YSE::OBJ::D_BUFFIR, "sigrange_1149 0 20");
    YSE::pHandle* dac = p.CreateObject(YSE::OBJ::D_DAC, "");
    REQUIRE(fir != nullptr);
    p.Connect(in, 0, fir, 0);
    p.Connect(offset, 0, fir, 1);
    p.Connect(size, 0, fir, 2);
    p.Connect(fir, 0, dac, 0);
    auto* object = static_cast<dBuffir*>(p.GetObjectFromID(fir->GetID()));
    const UInt last = STANDARD_BUFFERSIZE - 1;

    p.Calculate(YSE::T_DSP);
    CHECK(Out(p, last) == TensSum(5, 3));
    // A number does not change the audio while a cord is in, nor does a
    // connected signal change the stored number.
    object->GetInlet(1)->SetInt(1, YSE::T_GUI);
    p.Calculate(YSE::T_DSP);
    CHECK(Out(p, last) == TensSum(5, 3));
    CHECK(object->CurrentOffset() == 1);
    CHECK(object->CurrentSize() == 20);
    offset->SetFloatData(0, 2.7f);
    p.Calculate(YSE::T_DSP);
    CHECK(Out(p, last) == TensSum(2, 3)); // truncated
    size->SetFloatData(0, -4.f);
    p.Calculate(YSE::T_DSP);
    CHECK(Out(p, last) == 0.f); // no taps
    // The cords out: the numbers are back.
    p.Disconnect(offset, 0, fir, 1);
    p.Disconnect(size, 0, fir, 2);
    p.Calculate(YSE::T_DSP);
    CHECK(Out(p, last) == TensSum(1, 19));
  }

  TEST_CASE("~buffir: the channel, from 1, by argument and by message (#1149)") {
    auto b = Tens("chan_1149", 5, 3);
    BuffirRig r("chan_1149 0 5 2", YSE::OBJ::D_SIG, "1");
    CHECK(r.fir->CurrentChannel() == 2);
    CHECK(r.Block() == TensSum(0, 5, 2));
    r.Message("chan 3");
    CHECK(r.fir->CurrentChannel() == 3);
    CHECK(r.Block() == TensSum(0, 5, 3));
    // A float is truncated; below 1 is 1.
    r.Message("chan 1.9");
    CHECK(r.fir->CurrentChannel() == 1);
    r.Message("chan 2");
    r.Message("chan -4");
    CHECK(r.fir->CurrentChannel() == 1);
    CHECK(r.Block() == TensSum(0, 5));
    // A channel the buffer lacks: silence.
    r.Message("chan 4");
    CHECK(r.Block() == 0.f);
  }

  TEST_CASE("~buffir: clear empties the history at the next block (#1149)") {
    auto b = Tens("clear_1149", 10);
    BuffirRig r("clear_1149 0 10", YSE::OBJ::D_SIG, "1");
    r.Block();
    // Input 0 from here: without clear, the 1s still in the history ring
    // out over the first nine samples.
    r.source->SetFloatData(0, 0.f);
    CHECK(r.Block(0) == TensSum(1, 9));
    CHECK(Out(r.p, 8) == TensSum(9, 1));
    CHECK(Out(r.p, 9) == 0.f);

    r.source->SetFloatData(0, 1.f);
    r.Block();
    r.source->SetFloatData(0, 0.f);
    r.Message("clear");
    CHECK(r.Block(0) == 0.f);
    CHECK(Out(r.p, 8) == 0.f);
    // A clear is used once.
    r.source->SetFloatData(0, 1.f);
    r.Block();
    r.source->SetFloatData(0, 0.f);
    CHECK(r.Block(0) == TensSum(1, 9));
  }

  TEST_CASE("~buffir: no name, or a buffer filled later, resized and edited by ~buffer "
            "(#1149)") {
    {
      BuffirRig r("", YSE::OBJ::D_SIG, "1");
      CHECK(r.Block() == 0.f);
    }
    // The name is acquired when the object is made; the samples come later.
    BuffirRig r("later_1149 0 100", YSE::OBJ::D_SIG, "1");
    CHECK(r.Block() == 0.f);
    auto b = Tens("later_1149", 9);
    CHECK(r.Block() == TensSum(0, 9));
    // A resize is seen at the next block: the taps follow the buffer.
    b->setSize(3);
    CHECK(r.Block() == TensSum(0, 3));

    // A ~buffer of the same name, and its fill, heard from a later block.
    YSE::pHandle* buffer = r.p.CreateObject(YSE::OBJ::D_BUFFER, "later_1149");
    REQUIRE(buffer != nullptr);
    r.p.GetObjectFromID(buffer->GetID())->GetInlet(0)->SetMessage("fill 0.25", YSE::T_GUI);
    r.p.FileIO()->WaitIdle();
    r.p.Calculate(YSE::T_DSP);
    r.p.FileIO()->WaitIdle();
    CHECK(r.Block() == 0.75f);
  }

  TEST_CASE("~buffir: set reads another buffer from a later block (#1149)") {
    auto a = Tens("set_1149_a", 5);
    auto b = Tens("set_1149_b", 5);
    b->applyOffset(100.f);
    BuffirRig r("set_1149_a 0 5", YSE::OBJ::D_SIG, "1");
    CHECK(r.Block() == TensSum(0, 5));

    r.Message("set set_1149_b");
    r.Settle();
    CHECK(r.fir->Buffer() == b);
    CHECK_FALSE(r.fir->ReleasePending());
    CHECK(r.Block() == TensSum(0, 5) + 500.f);

    // set with no name: unbound, silence.
    r.Message("set");
    r.Settle();
    CHECK(r.fir->Buffer() == nullptr);
    CHECK(r.Block() == 0.f);
    CHECK(r.p.FileIO()->Dropped() == 0);

    // Outside a patcher a set does nothing.
    dBuffir standalone;
    standalone.SetParams("set_1149_a");
    standalone.GetInlet(0)->SetMessage("set set_1149_b", YSE::T_GUI);
    CHECK(standalone.Buffer() == a);
  }

  TEST_CASE("~buffir: arguments survive a DumpJSON / ParseJSON round trip; messages are not "
            "saved (#1149)") {
    auto a = Tens("json_1149_a", 20, 2);
    auto b = Tens("json_1149_b", 20, 2);
    std::string dump;
    {
      BuffirRig r("json_1149_a 2 5 2", YSE::OBJ::D_SIG, "1");
      dump = r.p.DumpJSON();
      CHECK(dump.find("json_1149_a 2 5 2") != std::string::npos);
      r.Message("set json_1149_b 1 3");
      r.Settle();
      CHECK(r.fir->Buffer() == b);
      CHECK(r.p.DumpJSON() == dump);
      r.Message("offset 3");
      r.Message("size 12");
      r.Message("chan 1");
      r.Message("clear");
      r.fir->GetInlet(1)->SetInt(4, YSE::T_GUI);
      r.fir->GetInlet(2)->SetInt(10, YSE::T_GUI);
      CHECK(r.p.DumpJSON() == dump);
    }
    CHECK(dump.find("json_1149_b") == std::string::npos);
    patcherImplementation dst(1, nullptr);
    dst.ParseJSON(dump);
    CHECK(dst.DumpJSON() == dump);
    // The reloaded ~buffir sums frames 2 .. 6 of channel 2 of a again.
    dst.Calculate(YSE::T_DSP);
    CHECK(Out(dst, STANDARD_BUFFERSIZE - 1) == TensSum(2, 5, 2));
  }

  TEST_CASE("~buffir: blocks, numbers and messages on the audio thread allocate nothing "
            "(#1149)") {
    auto a = Tens("rt_1149_a", 5000, 2);
    auto b = Tens("rt_1149_b", 512, 2);
    BuffirRig r("rt_1149_a 0 4096", YSE::OBJ::D_CYCLE, "100");
    r.Settle();
    // Built before the probe: the strings themselves allocate.
    const std::string toB = "set rt_1149_b 2 300";
    const std::string toA = "set rt_1149_a";
    const std::string chan = "chan 2";
    const std::string offset = "offset 3";
    const std::string size = "size 600";
    const std::string clear = "clear";
    for (const std::string* m : {&toB, &toA, &toB}) {
      CAPTURE(*m);
      {
        // Lock-free too: the request is a CAS into the scheduler's table.
        TestHelpers::ProbeScope probe;
        r.Message(*m, YSE::T_DSP);
        r.Message(chan, YSE::T_DSP);
        r.Message(offset, YSE::T_DSP);
        r.Message(size, YSE::T_DSP);
        r.Message(clear, YSE::T_DSP);
        r.fir->GetInlet(0)->SetFloat(0.5f, YSE::T_DSP);
        r.fir->GetInlet(0)->SetInt(0, YSE::T_DSP);
        r.fir->GetInlet(1)->SetFloat(1.f, YSE::T_DSP);
        r.fir->GetInlet(1)->SetInt(2, YSE::T_DSP);
        r.fir->GetInlet(2)->SetFloat(500.f, YSE::T_DSP);
        r.fir->GetInlet(2)->SetInt(4096, YSE::T_DSP);
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
      CHECK_FALSE(r.fir->ReleasePending());
      r.p.Calculate(YSE::T_DSP); // hands the slots back
    }
    CHECK(r.fir->Buffer() == b);
    CHECK(r.p.FileIO()->Dropped() == 0);
  }

  TEST_CASE("~buffir renders while control threads edit and set its buffers (#1149)") {
    // Two kernels of 16 taps filled 1/16 and 1/8, edited and switched from
    // control threads while the audio thread filters a ~cycle through them.
    // Each output is a weighted sum of 16 inputs within -1 .. 1 by taps of
    // at most 3/16 (the 3 an edit fills with, over 16), so within -3 .. 3.
    // Under ASan or TSan a freed storage or a broken handoff shows up here.
    auto ones = sampleBufferRegistry::acquire("race_1149_1");
    ones->setSize(16);
    ones->fill(1.f / 16.f);
    auto twos = sampleBufferRegistry::acquire("race_1149_2");
    twos->setSize(48);
    twos->fill(2.f / 16.f);
    BuffirRig r("race_1149_1 0 16", YSE::OBJ::D_CYCLE, "300");
    const std::string toOnes = "set race_1149_1";
    const std::string toTwos = "set race_1149_2";
    const std::string clear = "clear";

    std::atomic<bool> stop{false};
    std::thread setter([&] {
      int n = 0;
      while (!stop.load()) {
        r.Message((n++ % 2) != 0 ? toOnes : toTwos, YSE::T_GUI);
        if (n % 5 == 0) r.Message(clear, YSE::T_GUI);
        std::this_thread::yield();
      }
    });
    std::thread editor([&] {
      int n = 0;
      while (!stop.load()) {
        // A resize swaps the storage; the fill writes a value of its own.
        ones->setSize(16 + static_cast<UInt>(n % 7));
        ones->fill(n % 2 != 0 ? 1.f / 16.f : 3.f / 16.f);
        n++;
        std::this_thread::yield();
      }
    });

    int bad = 0;
    int switches = 0;
    const sampleBuffer* last = r.fir->Buffer().get();
    for (int block = 0; block < 20000 && (block < 2000 || switches == 0); ++block) {
      r.p.Calculate(YSE::T_DSP);
      for (UInt i = 0; i < STANDARD_BUFFERSIZE; ++i) {
        const float v = Out(r.p, i);
        if (!(v >= -3.001f && v <= 3.001f)) bad++;
      }
      const sampleBuffer* now = r.fir->Buffer().get();
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
