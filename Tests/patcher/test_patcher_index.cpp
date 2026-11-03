// Tests for the ~index patcher object (patcher/sampling/dIndex), issue
// #1145. The sample math is DSP::sampleIndexReader's
// (Tests/dsp/test_sample_access.cpp) and the reader is
// DSP::sampleBufferReader's (test_sampleBuffer.cpp). These show that pins,
// messages and arguments reach them, in rendered patches heard through a
// ~dac: a ~count stepping through a ~buffer, what ~poke wrote read back, the
// number fallback, the channel, set (#1256), a round trip, allocation-free
// blocks and messages on the audio thread, and edits made while it renders.

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
#include "patcher/sampling/dIndex.h"
#include "support/alloc_probe.hpp"
#include "sinks.hpp"

using YSE::STANDARD_BUFFERSIZE;
using YSE::DSP::sampleBuffer;
using YSE::DSP::sampleBufferRegistry;
using YSE::PATCHER::dIndex;
using YSE::PATCHER::patcherImplementation;

namespace {

  // A named buffer whose sample n of channel c is (c + 1) * 1000 + n.
  std::shared_ptr<sampleBuffer> Counting(const std::string& name, UInt channels, UInt frames) {
    std::shared_ptr<sampleBuffer> b = sampleBufferRegistry::acquire(name);
    b->setSize(frames, static_cast<Int>(channels));
    for (UInt c = 0; c < channels; ++c) {
      std::vector<float> values(frames);
      for (UInt n = 0; n < frames; ++n)
        values[n] = static_cast<float>((c + 1) * 1000 + n);
      b->setSamples(c, 0, values.data(), frames);
    }
    return b;
  }

  float Out(patcherImplementation& p, UInt i) {
    return p.output[0].getPtr()[i];
  }

  // [source] -> [~index args] -> [~dac], `source` made from `type` and
  // `sourceArgs`, or no source at all when `type` is empty.
  struct IndexRig {
    patcherImplementation p{1, nullptr};
    YSE::pHandle* source = nullptr;
    YSE::pHandle* handle = nullptr;
    YSE::pHandle* dac = nullptr;
    dIndex* index = nullptr;

    IndexRig(const std::string& args, const char* type, const std::string& sourceArgs) {
      handle = p.CreateObject(YSE::OBJ::D_INDEX, args);
      REQUIRE(handle != nullptr);
      dac = p.CreateObject(YSE::OBJ::D_DAC, "");
      REQUIRE(dac != nullptr);
      if (type != nullptr && *type != '\0') {
        source = p.CreateObject(type, sourceArgs);
        REQUIRE(source != nullptr);
        p.Connect(source, 0, handle, 0);
      }
      p.Connect(handle, 0, dac, 0);
      index = static_cast<dIndex*>(p.GetObjectFromID(handle->GetID()));
    }

    void Message(const std::string& text, YSE::THREAD thread = YSE::T_GUI) {
      index->GetInlet(0)->SetMessage(text, thread);
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

  TEST_CASE("~index: registry and shape (#1145)") {
    YSE::patcher p;
    p.create(1);
    YSE::pHandle* h = p.CreateObject(YSE::OBJ::D_INDEX, "shape_1145");
    REQUIRE(h != nullptr);
    CHECK(std::string(h->Type()) == "~index");
    CHECK(h->GetInputs() == 2);
    REQUIRE(h->GetOutputs() == 1);
    CHECK(h->OutputDataType(0) == YSE::OUT_TYPE::BUFFER);

    bool listed = false;
    for (const auto& name : YSE::PATCHER::Register().AllNames())
      if (name == YSE::OBJ::D_INDEX) listed = true;
    CHECK(listed);

    dIndex standalone;
    CHECK(standalone.GetCategory() == YSE::PATCHER::pCategory::SAMPLING);
    CHECK(standalone.CurrentChannel() == 1);
    CHECK(standalone.Buffer() == nullptr);
  }

  TEST_CASE("~index: a ~count steps through a buffer, sample for sample (#1145)") {
    // [~count] -> [~index tbl] -> [~dac]: the table, read out in order, and
    // the last sample held once the count runs past the end.
    auto b = Counting("count_1145", 1, STANDARD_BUFFERSIZE + 10);
    IndexRig r("count_1145", YSE::OBJ::D_COUNT, "0 0 1");
    CHECK(r.index->Buffer() == b);
    r.p.Calculate(YSE::T_DSP);
    for (UInt i = 0; i < STANDARD_BUFFERSIZE; ++i) {
      CAPTURE(i);
      CHECK(Out(r.p, i) == 1000.f + static_cast<float>(i));
    }
    r.p.Calculate(YSE::T_DSP);
    CHECK(Out(r.p, 0) == 1000.f + static_cast<float>(STANDARD_BUFFERSIZE));
    CHECK(Out(r.p, 9) == 1009.f + static_cast<float>(STANDARD_BUFFERSIZE));
    CHECK(Out(r.p, 10) == 1009.f + static_cast<float>(STANDARD_BUFFERSIZE)); // clamped
    CHECK(Out(r.p, STANDARD_BUFFERSIZE - 1) == 1009.f + static_cast<float>(STANDARD_BUFFERSIZE));
  }

  TEST_CASE("~index: a number index while no signal is connected; float truncated (#1145)") {
    auto b = Counting("num_1145", 2, 16);
    IndexRig r("num_1145", "", "");
    // The default index is 0.
    r.p.Calculate(YSE::T_DSP);
    CHECK(Out(r.p, 0) == 1000.f);
    CHECK(Out(r.p, STANDARD_BUFFERSIZE - 1) == 1000.f);

    r.index->GetInlet(0)->SetInt(5, YSE::T_GUI);
    r.p.Calculate(YSE::T_DSP);
    CHECK(Out(r.p, 0) == 1005.f);
    CHECK(Out(r.p, STANDARD_BUFFERSIZE - 1) == 1005.f);
    r.index->GetInlet(0)->SetFloat(7.9f, YSE::T_GUI);
    r.p.Calculate(YSE::T_DSP);
    CHECK(Out(r.p, 3) == 1007.f);
    r.index->GetInlet(0)->SetFloat(-4.f, YSE::T_GUI);
    r.p.Calculate(YSE::T_DSP);
    CHECK(Out(r.p, 3) == 1000.f);
    r.index->GetInlet(0)->SetInt(400, YSE::T_GUI);
    r.p.Calculate(YSE::T_DSP);
    CHECK(Out(r.p, 3) == 1015.f);
  }

  TEST_CASE("~index: a connected signal wins over the number (#1145)") {
    auto b = Counting("sigwins_1145", 1, 16);
    IndexRig r("sigwins_1145", YSE::OBJ::D_SIG, "9");
    r.index->GetInlet(0)->SetInt(2, YSE::T_GUI); // ignored while the cord is in
    r.p.Calculate(YSE::T_DSP);
    CHECK(Out(r.p, 0) == 1009.f);
    // The ~sig's own number changes what is read.
    r.source->SetFloatData(0, 12.5f);
    r.p.Calculate(YSE::T_DSP);
    CHECK(Out(r.p, 0) == 1012.f);
    // Without the cord the number is back.
    r.p.Disconnect(r.source, 0, r.handle, 0);
    r.p.Calculate(YSE::T_DSP);
    CHECK(Out(r.p, 0) == 1002.f);
  }

  TEST_CASE("~index: the channel, from 1, by argument and by inlet (#1145)") {
    auto b = Counting("chan_1145", 3, 8);
    IndexRig r("chan_1145 2", YSE::OBJ::D_SIG, "4");
    CHECK(r.index->CurrentChannel() == 2);
    r.p.Calculate(YSE::T_DSP);
    CHECK(Out(r.p, 0) == 2004.f);
    r.index->GetInlet(1)->SetInt(3, YSE::T_GUI);
    r.p.Calculate(YSE::T_DSP);
    CHECK(Out(r.p, 0) == 3004.f);
    // A float is truncated; below 1 is 1.
    r.index->GetInlet(1)->SetFloat(2.7f, YSE::T_GUI);
    CHECK(r.index->CurrentChannel() == 2);
    r.index->GetInlet(1)->SetFloat(0.f, YSE::T_GUI);
    CHECK(r.index->CurrentChannel() == 1);
    r.index->GetInlet(1)->SetInt(-3, YSE::T_GUI);
    CHECK(r.index->CurrentChannel() == 1);
    r.p.Calculate(YSE::T_DSP);
    CHECK(Out(r.p, 0) == 1004.f);
    // A channel the buffer lacks: silence.
    r.index->GetInlet(1)->SetInt(4, YSE::T_GUI);
    r.p.Calculate(YSE::T_DSP);
    CHECK(Out(r.p, 0) == 0.f);
  }

  TEST_CASE("~index: no name, or a buffer filled later (#1145)") {
    {
      IndexRig r("", YSE::OBJ::D_SIG, "1");
      r.p.Calculate(YSE::T_DSP);
      CHECK(Out(r.p, 0) == 0.f);
    }
    // The name is acquired when the object is made; the samples come later.
    IndexRig r("later_1145", YSE::OBJ::D_SIG, "3");
    r.p.Calculate(YSE::T_DSP);
    CHECK(Out(r.p, 0) == 0.f);
    auto b = Counting("later_1145", 1, 8);
    r.p.Calculate(YSE::T_DSP);
    CHECK(Out(r.p, 0) == 1003.f);
    // A resize is seen at the next block: index 3 is now past the end.
    b->setSize(2);
    r.p.Calculate(YSE::T_DSP);
    CHECK(Out(r.p, 0) == 1001.f);
  }

  TEST_CASE("~index reads back what ~poke wrote into a ~buffer (#1145)") {
    // The patch a user builds: [~sig] and [~count] into [~poke tab] write a
    // table that [~index tab] reads at the index a [~sig] gives, heard at the
    // [~dac]. ~index reads only frames written in an earlier block, so the
    // order of the two in a block does not matter.
    patcherImplementation p(1, nullptr);
    YSE::pHandle* buffer = p.CreateObject(YSE::OBJ::D_BUFFER, "tab_1145");
    YSE::pHandle* value = p.CreateObject(YSE::OBJ::D_SIG, "0.25");
    YSE::pHandle* writeAt = p.CreateObject(YSE::OBJ::D_COUNT, "0 0 1");
    YSE::pHandle* poke = p.CreateObject(YSE::OBJ::D_POKE, "tab_1145");
    YSE::pHandle* readAt = p.CreateObject(YSE::OBJ::D_SIG, "5");
    YSE::pHandle* index = p.CreateObject(YSE::OBJ::D_INDEX, "tab_1145");
    YSE::pHandle* dac = p.CreateObject(YSE::OBJ::D_DAC, "");
    REQUIRE(buffer != nullptr);
    REQUIRE(poke != nullptr);
    REQUIRE(index != nullptr);
    p.Connect(value, 0, poke, 0);
    p.Connect(writeAt, 0, poke, 1);
    p.Connect(readAt, 0, index, 0);
    p.Connect(index, 0, dac, 0);

    std::shared_ptr<sampleBuffer> tab = sampleBufferRegistry::find("tab_1145");
    REQUIRE(tab != nullptr);
    tab->setSize(STANDARD_BUFFERSIZE * 2);

    p.Calculate(YSE::T_DSP); // writes 0.25 into frames 0 .. N - 1
    value->SetFloatData(0, -0.5f);
    p.Calculate(YSE::T_DSP); // and -0.5 into N .. 2N - 1
    CHECK(Out(p, 0) == 0.25f);
    CHECK(Out(p, STANDARD_BUFFERSIZE - 1) == 0.25f);
    readAt->SetFloatData(0, static_cast<float>(STANDARD_BUFFERSIZE) + 3.5f);
    p.Calculate(YSE::T_DSP);
    CHECK(Out(p, 0) == -0.5f);
    // Past the end: the last frame, which ~poke wrote too.
    readAt->SetFloatData(0, 1e6f);
    p.Calculate(YSE::T_DSP);
    CHECK(Out(p, 0) == -0.5f);
    // A ~buffer edit is heard from the next block.
    p.GetObjectFromID(buffer->GetID())->GetInlet(0)->SetMessage("fill 0.75", YSE::T_GUI);
    p.FileIO()->WaitIdle();
    p.Calculate(YSE::T_DSP);
    p.FileIO()->WaitIdle();
    p.Calculate(YSE::T_DSP);
    CHECK(Out(p, 0) == 0.75f);
  }

  TEST_CASE("~index: set reads another buffer from a later block (#1145)") {
    auto a = Counting("set_1145_a", 1, 8);
    auto b = Counting("set_1145_b", 2, 8);
    b->applyOffset(100.f);
    IndexRig r("set_1145_a", YSE::OBJ::D_SIG, "3");
    r.p.Calculate(YSE::T_DSP);
    CHECK(Out(r.p, 0) == 1003.f);

    r.Message("set set_1145_b");
    r.Settle();
    CHECK(r.index->Buffer() == b);
    CHECK_FALSE(r.index->ReleasePending());
    r.p.Calculate(YSE::T_DSP);
    CHECK(Out(r.p, 0) == 1103.f);
    r.index->GetInlet(1)->SetInt(2, YSE::T_GUI);
    r.p.Calculate(YSE::T_DSP);
    CHECK(Out(r.p, 0) == 2103.f); // b has a second channel

    // set with no name: unbound, silence.
    r.Message("set");
    r.Settle();
    CHECK(r.index->Buffer() == nullptr);
    r.p.Calculate(YSE::T_DSP);
    CHECK(Out(r.p, 0) == 0.f);
    CHECK(r.p.FileIO()->Dropped() == 0);

    // Outside a patcher a set does nothing.
    dIndex standalone;
    standalone.SetParams("set_1145_a");
    standalone.GetInlet(0)->SetMessage("set set_1145_b", YSE::T_GUI);
    CHECK(standalone.Buffer() == a);
  }

  TEST_CASE("~index: arguments survive a DumpJSON / ParseJSON round trip; set is not saved "
            "(#1145)") {
    auto a = Counting("json_1145_a", 2, 8);
    auto b = Counting("json_1145_b", 2, 8);
    std::string dump;
    {
      IndexRig r("json_1145_a 2", YSE::OBJ::D_SIG, "6");
      dump = r.p.DumpJSON();
      r.Message("set json_1145_b");
      r.Settle();
      CHECK(r.index->Buffer() == b);
      CHECK(r.p.DumpJSON() == dump);
    }
    CHECK(dump.find("json_1145_a 2") != std::string::npos);
    CHECK(dump.find("json_1145_b") == std::string::npos);
    patcherImplementation dst(1, nullptr);
    dst.ParseJSON(dump);
    CHECK(dst.DumpJSON() == dump);
    // The reloaded ~index reads channel 2 of a again.
    dst.Calculate(YSE::T_DSP);
    CHECK(Out(dst, 0) == 2006.f);
  }

  TEST_CASE("~index: blocks, numbers and set on the audio thread allocate nothing (#1145)") {
    auto a = Counting("rt_1145_a", 2, 1024);
    auto b = Counting("rt_1145_b", 2, 512);
    IndexRig r("rt_1145_a", YSE::OBJ::D_COUNT, "0 0 1");
    r.Settle();
    // Built before the probe: the strings themselves allocate.
    const std::string toB = "set rt_1145_b";
    const std::string toA = "set rt_1145_a";
    for (const std::string* m : {&toB, &toA, &toB}) {
      CAPTURE(*m);
      {
        // Lock-free too: the request is a CAS into the scheduler's table.
        TestHelpers::ProbeScope probe;
        r.Message(*m, YSE::T_DSP);
        r.index->GetInlet(1)->SetInt(2, YSE::T_DSP);
        r.index->GetInlet(1)->SetFloat(1.f, YSE::T_DSP);
        r.index->GetInlet(0)->SetFloat(3.f, YSE::T_DSP);
        r.index->GetInlet(0)->SetInt(4, YSE::T_DSP);
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
      CHECK_FALSE(r.index->ReleasePending());
      r.p.Calculate(YSE::T_DSP); // hands the slots back
    }
    CHECK(r.index->Buffer() == b);
    CHECK(r.p.FileIO()->Dropped() == 0);
  }

  TEST_CASE("~index renders while control threads edit and set its buffers (#1145)") {
    // Two buffers of different lengths filled 1 and 2, edited and switched
    // from control threads while the audio thread reads them at a ~count
    // running past both ends. Every sample heard is whole: 1, 2, or the 3 an
    // edit fills with. Under ASan or TSan a freed storage or a broken handoff
    // shows up here.
    auto ones = sampleBufferRegistry::acquire("race_1145_1");
    ones->setSize(STANDARD_BUFFERSIZE);
    ones->fill(1.f);
    auto twos = sampleBufferRegistry::acquire("race_1145_2");
    twos->setSize(STANDARD_BUFFERSIZE * 3);
    twos->fill(2.f);
    IndexRig r("race_1145_1", YSE::OBJ::D_COUNT,
               "0 " + std::to_string(STANDARD_BUFFERSIZE * 4) + " 1");
    const std::string toOnes = "set race_1145_1";
    const std::string toTwos = "set race_1145_2";

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
    const sampleBuffer* last = r.index->Buffer().get();
    for (int block = 0; block < 20000 && (block < 2000 || switches == 0); ++block) {
      r.p.Calculate(YSE::T_DSP);
      for (UInt i = 0; i < STANDARD_BUFFERSIZE; ++i) {
        const float v = Out(r.p, i);
        // 0: the gap between a resize and the fill that follows it.
        if (!(v == 1.f || v == 2.f || v == 3.f || v == 0.f)) bad++;
      }
      const sampleBuffer* now = r.index->Buffer().get();
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
