// Tests for the ~train patcher object (patcher/generatorObjects/dTrain),
// issue #1059. The pulses are DSP::pulseTrain's and are tested in
// Tests/dsp/test_pulse_train.cpp; these show that the signals, the numbers,
// the bang, the messages and the arguments reach it, that the onset bang
// goes out only through the armed outlet (#1037), and that it works in a
// rendered patch after a JSON round trip.

#include <doctest/doctest.h>
#include <cmath>
#include <memory>
#include <string>
#include "patcher/patcher.hpp"
#include "patcher/patcherImplementation.h"
#include "patcher/pHandle.hpp"
#include "patcher/pObjectList.hpp"
#include "patcher/pRegistry.h"
#include "patcher/generatorObjects/dTrain.h"
#include "headers/constants.hpp"
#include "support/alloc_probe.hpp"
#include "support/audio_helpers.hpp"
#include "sinks.hpp"

using TestHelpers::BangSink;
using TestHelpers::BufferSink;
using TestHelpers::ScopedSampleRate;
using TestHelpers::Wire;
using YSE::DSP::buffer;
using YSE::PATCHER::dTrain;
using YSE::PATCHER::patcherImplementation;

namespace {
  constexpr unsigned int N = YSE::STANDARD_BUFFERSIZE;

  // ~train with a buffer sink on the signal outlet and a bang sink on the
  // onset outlet. Block() runs one block and the flush the patcher runs
  // after its traversal, and says whether a bang went out.
  struct Rig {
    BufferSink out;
    BangSink onset;
    dTrain train;

    Rig() {
      Wire(train, 0, out);
      Wire(train, 1, onset);
    }
    bool Block() {
      onset.gotBang = false;
      train.ResetDSP();
      train.Calculate(YSE::T_DSP);
      train.FlushArmedOutlets(YSE::T_GUI);
      return onset.gotBang;
    }
    // A signal on the interval inlet.
    bool Block(buffer& ms) {
      onset.gotBang = false;
      train.ResetDSP();
      train.GetInlet(0)->SetBuffer(&ms, YSE::T_DSP); // the last input: runs CALC()
      train.FlushArmedOutlets(YSE::T_GUI);
      return onset.gotBang;
    }
    float At(unsigned int i) const {
      return out.received->getPtr()[i];
    }
  };

  // Sample n of a train with constant settings and a period in samples.
  float Reference(unsigned int n, double period, double width, double phase) {
    double p = static_cast<double>(n) / period - phase;
    p -= std::floor(p);
    return p < width ? 1.f : 0.f;
  }

  int GuiInt(YSE::pHandle* h) {
    return std::stoi(h->GetGuiValue());
  }
} // namespace

TEST_SUITE("patcher") {

  TEST_CASE("~train: registry and shape (#1059)") {
    YSE::patcher p;
    p.create(1);
    YSE::pHandle* obj = p.CreateObject(YSE::OBJ::D_TRAIN);
    REQUIRE(obj != nullptr);
    CHECK(std::string(obj->Type()) == "~train");
    CHECK(obj->GetInputs() == 3);
    CHECK(obj->GetOutputs() == 2);
    CHECK(obj->IsDSPInput(0));
    CHECK(obj->IsDSPInput(1));
    CHECK(obj->IsDSPInput(2));
    CHECK(obj->OutputDataType(0) == YSE::OUT_TYPE::BUFFER);
    CHECK(obj->OutputDataType(1) == YSE::OUT_TYPE::BANG);

    std::unique_ptr<YSE::PATCHER::pObject> o(YSE::PATCHER::Register().Get(YSE::OBJ::D_TRAIN));
    REQUIRE(o != nullptr);
    CHECK(o->GetCategory() == YSE::PATCHER::pCategory::OSC);
    CHECK(o->IsDSPObject());
    CHECK(o->GetParamCount() == 4);
  }

  TEST_CASE("~train: defaults are 1000 ms at half width; the onset bang is armed, then sent "
            "(#1059, #1037)") {
    ScopedSampleRate rate(1000);
    Rig rig;
    rig.train.ResetDSP();
    rig.train.Calculate(YSE::T_DSP);
    CHECK_FALSE(rig.onset.gotBang); // CALC() only arms
    CHECK(rig.train.GetOutlet(1)->Armed());
    rig.train.FlushArmedOutlets(YSE::T_GUI);
    CHECK(rig.onset.gotBang);
    REQUIRE(rig.out.received != nullptr);
    REQUIRE(rig.out.received->getLength() == N);
    for (unsigned int i = 0; i < N; ++i)
      REQUIRE(rig.At(i) == 1.f);
    CHECK_FALSE(rig.Block()); // samples 128-255: still the first pulse
    CHECK_FALSE(rig.Block());
    CHECK_FALSE(rig.Block()); // 384-511: the pulse ends at 500
    CHECK(rig.At(500 - 3 * N - 1) == 1.f);
    CHECK(rig.At(500 - 3 * N) == 0.f);
  }

  TEST_CASE("~train: the arguments set interval, width and phase (#1059)") {
    ScopedSampleRate rate(1000);
    Rig rig;
    rig.train.SetParams("16 0.25 0.5");
    for (unsigned int block = 0; block < 3; ++block) {
      REQUIRE(rig.Block()); // a pulse begins in every block
      for (unsigned int i = 0; i < N; ++i)
        REQUIRE(rig.At(i) == Reference(block * N + i, 16.0, 0.25, 0.5));
    }
  }

  TEST_CASE("~train: numbers at the three inlets are interval, width and phase, and are not "
            "saved (#1059, #1036)") {
    ScopedSampleRate rate(1000);
    Rig rig;
    rig.train.GetInlet(0)->SetFloat(8.f, YSE::T_GUI);
    rig.train.GetInlet(1)->SetInt(1, YSE::T_GUI); // all but one sample
    rig.train.GetInlet(2)->SetFloat(0.5f, YSE::T_GUI);
    CHECK(rig.train.GetParams() == "");
    REQUIRE(rig.Block());
    for (unsigned int i = 0; i < N; ++i)
      REQUIRE(rig.At(i) == ((i % 8u) == 3u ? 0.f : 1.f));

    rig.train.GetInlet(0)->SetInt(16, YSE::T_GUI);
    rig.train.GetInlet(1)->SetFloat(0.25f, YSE::T_GUI);
    rig.train.GetInlet(2)->SetInt(0, YSE::T_GUI);
    REQUIRE(rig.Block()); // 16-sample cycles from sample 128, on the grid
    for (unsigned int i = 0; i < N; ++i)
      REQUIRE(rig.At(i) == Reference(N + i, 16.0, 0.25, 0.0));

    rig.train.GetInlet(0)->SetFloat(0.f, YSE::T_GUI); // 0 stops it
    rig.Block();
    CHECK_FALSE(rig.Block());
  }

  TEST_CASE("~train: a bang starts a pulse at the next block (#1059)") {
    ScopedSampleRate rate(1000);
    Rig rig;
    rig.train.SetParams("1000 0.5 0.5"); // off for the first 500 samples
    CHECK_FALSE(rig.Block());
    CHECK(rig.At(0) == 0.f);
    rig.train.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK_FALSE(rig.onset.gotBang); // a bang only asks
    REQUIRE(rig.Block());
    CHECK(rig.At(0) == 1.f);
    CHECK(rig.At(N - 1) == 1.f);
    CHECK_FALSE(rig.Block());
  }

  TEST_CASE("~train: 'interval', 'width', 'phase' and 'resetmode' messages (#1059)") {
    ScopedSampleRate rate(1000);
    Rig rig;
    rig.train.GetInlet(0)->SetMessage("interval 8", YSE::T_GUI);
    rig.train.SetMessage("width 0.25", 0.f);
    rig.train.SetMessage("phase 0.5", 0.f);
    REQUIRE(rig.Block());
    for (unsigned int i = 0; i < N; ++i)
      REQUIRE(rig.At(i) == Reference(i, 8.0, 0.25, 0.5));

    // Malformed or unknown messages change nothing.
    rig.train.SetMessage("interval", 0.f);
    rig.train.SetMessage("interval abc", 0.f);
    rig.train.SetMessage("width nan", 0.f);
    rig.train.SetMessage("phase", 0.f);
    rig.train.SetMessage("set 0", 0.f);
    rig.train.SetMessage("", 0.f);
    REQUIRE(rig.Block());
    for (unsigned int i = 0; i < N; ++i)
      REQUIRE(rig.At(i) == Reference(N + i, 8.0, 0.25, 0.5));
    CHECK(rig.train.GetParams() == "");
  }

  TEST_CASE("~train: resetmode 1 holds a new interval until the cycle ends (#1059)") {
    ScopedSampleRate rate(1000);
    // 256 samples a cycle; after one block the cycle is halfway.
    Rig now;
    now.train.SetParams("256");
    REQUIRE(now.Block());
    now.train.SetMessage("interval 64", 0.f);
    CHECK(now.Block()); // at once: the rest of the cycle takes 32 samples

    Rig later;
    later.train.SetParams("256 0.5 0 1");
    REQUIRE(later.Block());
    later.train.SetMessage("interval 64", 0.f);
    CHECK_FALSE(later.Block()); // the 256-sample cycle runs on
    REQUIRE(later.Block()); // a new cycle at 256, now 64 long
    for (unsigned int i = 0; i < N; ++i)
      REQUIRE(later.At(i) == Reference(i, 64.0, 0.5, 0.0));

    Rig message;
    message.train.SetParams("256");
    message.train.SetMessage("resetmode 1", 0.f);
    REQUIRE(message.Block());
    message.train.SetMessage("interval 64", 0.f);
    CHECK_FALSE(message.Block());
    message.train.SetMessage("resetmode 0", 0.f);
    REQUIRE(message.Block());
  }

  TEST_CASE("~train: a connected interval signal wins over the number (#1059, #1036)") {
    ScopedSampleRate rate(1000);
    Rig rig;
    rig.train.GetInlet(0)->SetFloat(1000.f, YSE::T_GUI);
    buffer ms(N);
    ms = 4.f;
    REQUIRE(rig.Block(ms));
    for (unsigned int i = 0; i < N; ++i)
      REQUIRE(rig.At(i) == ((i % 4u) < 2u ? 1.f : 0.f));
  }

  TEST_CASE("~train: the arguments survive a DumpJSON / ParseJSON round trip (#1059)") {
    std::string dump;
    {
      patcherImplementation src(1, nullptr);
      YSE::pHandle* a = src.CreateObject(YSE::OBJ::D_TRAIN, "12.5 0.25 0.75 1");
      REQUIRE(a != nullptr);
      YSE::pHandle* b = src.CreateObject(YSE::OBJ::D_TRAIN, "");
      REQUIRE(b != nullptr);
      dump = src.DumpJSON();
    }
    patcherImplementation dst(1, nullptr);
    dst.ParseJSON(dump);
    YSE::pHandle* a = dst.GetHandleFromID(0);
    YSE::pHandle* b = dst.GetHandleFromID(1);
    REQUIRE(a != nullptr);
    REQUIRE(b != nullptr);
    CHECK(std::string(a->Type()) == "~train");
    CHECK(a->GetParams() == "12.5 0.25 0.75 1");
    CHECK(b->GetParams() == "");
  }

  TEST_CASE("~train: in a rendered patch, the bang outlet clocks a counter and a signal "
            "interval wins, after a JSON round trip (#1059)") {
    ScopedSampleRate rate(48000);
    std::string dump;
    unsigned int slowID = 0;
    unsigned int fastID = 0;
    unsigned int edgesID = 0;
    {
      patcherImplementation src(1, nullptr);
      // 8 ms at 48 kHz is 384 samples: an onset every three blocks.
      YSE::pHandle* slow = src.CreateObject(YSE::OBJ::D_TRAIN, "8");
      // The 4 ms signal overrides the argument: 192 samples.
      YSE::pHandle* ms = src.CreateObject(YSE::OBJ::D_SIG, "4");
      YSE::pHandle* fast = src.CreateObject(YSE::OBJ::D_TRAIN, "8");
      YSE::pHandle* edge = src.CreateObject(YSE::OBJ::D_EDGE, "");
      YSE::pHandle* slowCount = src.CreateObject(YSE::OBJ::G_COUNTER, "");
      YSE::pHandle* fastCount = src.CreateObject(YSE::OBJ::G_COUNTER, "");
      YSE::pHandle* edgeCount = src.CreateObject(YSE::OBJ::G_COUNTER, "");
      REQUIRE(slow != nullptr);
      REQUIRE(ms != nullptr);
      REQUIRE(fast != nullptr);
      REQUIRE(edge != nullptr);
      REQUIRE(slowCount != nullptr);
      REQUIRE(fastCount != nullptr);
      REQUIRE(edgeCount != nullptr);
      src.Connect(slow, 1, slowCount, 0);
      src.Connect(ms, 0, fast, 0);
      src.Connect(fast, 1, fastCount, 0);
      src.Connect(fast, 0, edge, 0); // the signal itself rises at each onset
      src.Connect(edge, 0, edgeCount, 0);
      slowID = slowCount->GetID();
      fastID = fastCount->GetID();
      edgesID = edgeCount->GetID();
      dump = src.DumpJSON();
    }
    patcherImplementation dst(1, nullptr);
    dst.ParseJSON(dump);
    YSE::pHandle* slowCount = dst.GetHandleFromID(slowID);
    YSE::pHandle* fastCount = dst.GetHandleFromID(fastID);
    YSE::pHandle* edgeCount = dst.GetHandleFromID(edgesID);
    REQUIRE(slowCount != nullptr);
    REQUIRE(fastCount != nullptr);
    REQUIRE(edgeCount != nullptr);

    // Onsets: slow at 0 and 384 (blocks 1 and 4); fast at 0, 192, 384 and
    // 576 (blocks 1, 2, 4 and 5).
    const int slow[] = {1, 1, 1, 2, 2, 2};
    const int fast[] = {1, 2, 2, 3, 4, 4};
    for (int block = 0; block < 6; ++block) {
      CAPTURE(block);
      dst.Calculate(YSE::T_DSP);
      CHECK(GuiInt(slowCount) == slow[block]);
      CHECK(GuiInt(fastCount) == fast[block]);
      CHECK(GuiInt(edgeCount) == fast[block]);
    }
  }

  TEST_CASE("~train: numbers, messages, a bang, blocks and the flush on the audio thread "
            "allocate nothing (#1059)") {
    ScopedSampleRate rate(1000);
    Rig rig;
    buffer ms(N);
    ms = 5.f;
    const std::string defer = "resetmode 1";
    const std::string every = "interval 16";
    rig.Block();
    {
      TestHelpers::ProbeScope probe;
      rig.train.GetInlet(0)->SetFloat(2.f, YSE::T_DSP);
      rig.train.GetInlet(0)->SetInt(4, YSE::T_DSP);
      rig.train.GetInlet(1)->SetFloat(0.3f, YSE::T_DSP);
      rig.train.GetInlet(2)->SetInt(0, YSE::T_DSP);
      rig.train.GetInlet(0)->SetBang(YSE::T_DSP);
      rig.train.GetInlet(0)->SetMessage(defer, YSE::T_DSP);
      rig.train.GetInlet(0)->SetMessage(every, YSE::T_DSP);
      rig.Block();
      rig.Block(ms);
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
    CHECK(rig.onset.gotBang);
  }

} // TEST_SUITE("patcher")
