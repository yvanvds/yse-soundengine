// Tests for the ~comp patcher object (patcher/dynamics/dComp), issue #1116.
// The compression is DSP::MODULES::compressor's and is tested in
// Tests/dsp/test_module_compressor.cpp; these show that the pins, the
// sidechain, the settings parameter, the messages and the meter outlet reach
// it, alone and in a rendered patch after a JSON round trip.

#include <doctest/doctest.h>
#include <cmath>
#include <memory>
#include <string>
#include "patcher/patcher.hpp"
#include "patcher/patcherImplementation.h"
#include "patcher/pHandle.hpp"
#include "patcher/pObjectList.hpp"
#include "patcher/pRegistry.h"
#include "patcher/dynamics/dComp.h"
#include "dsp/modules/compressor.hpp"
#include "dsp/generators/phasor.hpp"
#include "headers/constants.hpp"
#include "support/alloc_probe.hpp"
#include "support/audio_helpers.hpp"
#include "sinks.hpp"

using TestHelpers::BufferSink;
using TestHelpers::FloatSink;
using YSE::DSP::buffer;
using YSE::DSP::phasor;
using YSE::DSP::MODULES::compressor;
using YSE::PATCHER::dComp;
using YSE::PATCHER::patcherImplementation;

namespace {
  constexpr unsigned int N = YSE::STANDARD_BUFFERSIZE;

  // Loud and quiet stretches, a little different per channel.
  void Fill(buffer& b, unsigned int start, unsigned int channel) {
    for (unsigned int i = 0; i < b.getLength(); ++i) {
      const auto n = static_cast<float>(start + i);
      const float level = ((start + i) / 700) % 2 == 0 ? 0.9f : 0.05f;
      b.getPtr()[i] = level * std::sin(n * (0.03f + 0.01f * static_cast<float>(channel)));
    }
  }

  float GuiFloat(YSE::pHandle* h) {
    return std::stof(h->GetGuiValue());
  }
} // namespace

TEST_SUITE("patcher") {

  TEST_CASE("~comp: registry and shape (#1116)") {
    YSE::patcher p;
    p.create(1);
    YSE::pHandle* obj = p.CreateObject(YSE::OBJ::D_COMP);
    REQUIRE(obj != nullptr);
    CHECK(std::string(obj->Type()) == "~comp");
    CHECK(obj->GetInputs() == 3);
    CHECK(obj->GetOutputs() == 3);
    CHECK(obj->IsDSPInput(0));
    CHECK(obj->IsDSPInput(1));
    CHECK(obj->IsDSPInput(2));
    CHECK(obj->OutputDataType(0) == YSE::OUT_TYPE::BUFFER);
    CHECK(obj->OutputDataType(1) == YSE::OUT_TYPE::BUFFER);
    CHECK(obj->OutputDataType(2) == YSE::OUT_TYPE::FLOAT);

    std::unique_ptr<YSE::PATCHER::pObject> o(YSE::PATCHER::Register().Get(YSE::OBJ::D_COMP));
    REQUIRE(o != nullptr);
    CHECK(o->GetCategory() == YSE::PATCHER::pCategory::DYNAMICS);
    CHECK(o->IsDSPObject());
    CHECK(o->GetParamCount() == 1);
  }

  TEST_CASE("~comp: the settings parameter (#1116)") {
    {
      // The class's defaults reach it at the first block.
      dComp obj;
      obj.Calculate(YSE::T_DSP);
      compressor& f = obj.Compressor();
      CHECK(f.threshold() == -18.f);
      CHECK(f.ratio() == 4.f);
      CHECK(f.attack() == 10.f);
      CHECK(f.release() == 100.f);
      CHECK(f.makeup() == 0.f);
      CHECK(f.detector() == YSE::DSP::MODULES::DETECT_PEAK);
      CHECK(f.impact() == 1.f);
    }
    {
      dComp obj;
      const std::string args = "threshold -30 ratio 8 attack 2 release 40 makeup 6 detector 1 "
                               "mix 0.5 bypass 0 meters 0 meterrate 50";
      obj.SetParams(args);
      CHECK(obj.GetParams() == args);
      obj.Calculate(YSE::T_DSP);
      compressor& f = obj.Compressor();
      CHECK(f.threshold() == -30.f);
      CHECK(f.ratio() == 8.f);
      CHECK(f.attack() == 2.f);
      CHECK(f.release() == 40.f);
      CHECK(f.makeup() == 6.f);
      CHECK(f.detector() == YSE::DSP::MODULES::DETECT_RMS);
      CHECK(f.impact() == 0.5f);
    }
    {
      // The class clamps; malformed and unknown words are ignored; bypass is
      // a dry mix.
      dComp obj;
      obj.SetParams("threshold -100 ratio 50 mix 3 makeup x what 3 bypass 1");
      obj.Calculate(YSE::T_DSP);
      compressor& f = obj.Compressor();
      CHECK(f.threshold() == -60.f);
      CHECK(f.ratio() == 20.f);
      CHECK(f.makeup() == 0.f);
      CHECK(f.impact() == 0.f);
    }
  }

  TEST_CASE("~comp: signals, the sidechain, numbers and messages against the DSP class "
            "(#1116, #1036)") {
    TestHelpers::ScopedSampleRate rate(48000);
    BufferSink out0;
    BufferSink out1;
    dComp obj;
    const std::string args = "threshold -24 ratio 6 attack 1 release 30 meters 0";
    obj.SetParams(args);
    obj.ConnectOutlet(out0.GetInlet(0), 0);
    out0.ConnectInlet(obj.GetOutlet(0), 0);
    obj.ConnectOutlet(out1.GetInlet(0), 1);
    out1.ConnectInlet(obj.GetOutlet(1), 0);

    compressor reference;
    reference.threshold(-24.f).ratio(6.f).attack(1.f).release(30.f);
    buffer objIn0(N);
    buffer objIn1(N);
    buffer objKey(N);
    MULTICHANNELBUFFER ref(2);
    MULTICHANNELBUFFER refKey(1);
    ref[0].resize(N);
    ref[1].resize(N);
    refKey[0].resize(N);
    unsigned int start = 0;
    bool connect1 = true;
    bool sidechain = false;
    float constant1 = 0.f;
    const auto step = [&]() {
      obj.ResetDSP();
      Fill(objIn0, start, 0);
      Fill(ref[0], start, 0);
      Fill(objIn1, start, 1);
      if (connect1)
        Fill(ref[1], start, 1);
      else
        ref[1] = constant1;
      Fill(objKey, start + 350, 2); // loud where the input is quiet
      Fill(refKey[0], start + 350, 2);
      start += N;
      obj.GetInlet(0)->SetBuffer(&objIn0, YSE::T_GUI);
      if (connect1) obj.GetInlet(1)->SetBuffer(&objIn1, YSE::T_GUI);
      if (sidechain) obj.GetInlet(2)->SetBuffer(&objKey, YSE::T_GUI);
      obj.Calculate(YSE::T_DSP);
      REQUIRE(out0.received != nullptr);
      REQUIRE(out1.received != nullptr);
      if (sidechain)
        reference.process(ref, refKey);
      else
        reference.process(ref);
      CHECK(TestHelpers::buffersNearlyEqual(*out0.received, ref[0], 0.f));
      CHECK(TestHelpers::buffersNearlyEqual(*out1.received, ref[1], 0.f));
    };

    for (int i = 0; i < 12; ++i)
      step();
    CHECK(reference.gainReductionDb() < -1.f);
    sidechain = true;
    for (int i = 0; i < 12; ++i)
      step();
    sidechain = false;
    step();
    obj.SetMessage("threshold -12", 0.f);
    reference.threshold(-12.f);
    step();
    obj.GetInlet(0)->SetMessage("ratio 3", YSE::T_GUI);
    reference.ratio(3.f);
    step();
    obj.SetMessage("attack 5", 0.f);
    reference.attack(5.f);
    step();
    obj.SetMessage("release 200", 0.f);
    reference.release(200.f);
    step();
    obj.SetMessage("makeup 4", 0.f);
    reference.makeup(4.f);
    step();
    obj.SetMessage("detector 1", 0.f);
    reference.detector(YSE::DSP::MODULES::DETECT_RMS);
    step();
    obj.SetMessage("mix 0.25", 0.f);
    reference.impact(0.25f);
    step();
    obj.SetMessage("bypass 1", 0.f);
    reference.impact(0.f);
    step();
    obj.SetMessage("bypass 0", 0.f);
    reference.impact(0.25f);
    step();
    obj.SetMessage("savesettings", 0.f); // not ported: ignored
    obj.SetMessage("ratio y", 0.f);
    step();

    connect1 = false;
    obj.GetInlet(1)->SetFloat(-0.5f, YSE::T_GUI);
    constant1 = -0.5f;
    step();
    obj.GetInlet(1)->SetInt(1, YSE::T_GUI);
    constant1 = 1.f;
    step();
  }

  TEST_CASE("~comp: the gain-reduction meter, armed and sent after the block (#1116, #1037)") {
    TestHelpers::ScopedSampleRate rate(48000);
    FloatSink meter;
    dComp obj;
    obj.SetParams("threshold -20 ratio 4 attack 0.1");
    obj.ConnectOutlet(meter.GetInlet(0), 2);
    meter.ConnectInlet(obj.GetOutlet(2), 0);
    buffer in(N);
    const auto block = [&](float level) {
      obj.ResetDSP();
      in = level;
      obj.GetInlet(0)->SetBuffer(&in, YSE::T_GUI);
      meter.gotFloat = false;
      obj.Calculate(YSE::T_DSP);
      CHECK_FALSE(meter.gotFloat); // never during the block
      obj.FlushArmedOutlets(YSE::T_GUI);
    };

    // Default: meters on, every 100 ms = 4800 samples = 37.5 blocks, so the
    // first report comes after block 38.
    for (int i = 0; i < 37; ++i) {
      block(1.f);
      CHECK_FALSE(meter.gotFloat);
    }
    block(1.f);
    REQUIRE(meter.gotFloat);
    // 0 dB against -20, ratio 4: (0 + 20) * (1/4 - 1) = -15 dB.
    CHECK(meter.received == doctest::Approx(-15.f).epsilon(1e-3));
    meter.gotFloat = false;

    // meterrate 0: every block; silence reports 0 once recovered.
    obj.SetMessage("meterrate 0", 0.f);
    block(1.f);
    CHECK(meter.gotFloat);
    meter.gotFloat = false;
    for (int i = 0; i < 400; ++i)
      block(0.f);
    CHECK(meter.gotFloat);
    CHECK(meter.received > -0.01f);
    meter.gotFloat = false;

    // meters 0: silent, until meterdata asks for one report.
    obj.SetMessage("meters 0", 0.f);
    block(1.f);
    block(1.f);
    CHECK_FALSE(meter.gotFloat);
    obj.SetMessage("meterdata", 0.f);
    block(1.f);
    CHECK(meter.gotFloat);
    CHECK(meter.received == doctest::Approx(-15.f).epsilon(1e-3)); // the deepest of the three
    meter.gotFloat = false;
    block(1.f);
    CHECK_FALSE(meter.gotFloat);
  }

  TEST_CASE("~comp: in a rendered patch, a sidechain ducks a stereo signal and the meter "
            "reaches a number box, after a JSON round trip (#1116)") {
    TestHelpers::ScopedSampleRate rate(48000);
    std::string dump;
    unsigned int meterID = 0;
    {
      patcherImplementation src(2, nullptr);
      YSE::pHandle* left = src.CreateObject(YSE::OBJ::D_PHASOR, "375");
      YSE::pHandle* quiet = src.CreateObject(YSE::OBJ::D_MULTIPLY, "0.05");
      YSE::pHandle* right = src.CreateObject(YSE::OBJ::D_PHASOR, "750");
      YSE::pHandle* keyOsc = src.CreateObject(YSE::OBJ::D_PHASOR, "50");
      YSE::pHandle* comp = src.CreateObject(
          YSE::OBJ::D_COMP, "threshold -30 ratio 8 attack 1 release 30 makeup 2 meterrate 0");
      YSE::pHandle* dac = src.CreateObject(YSE::OBJ::D_DAC, "");
      YSE::pHandle* box = src.CreateObject(YSE::OBJ::G_FLOAT, "");
      REQUIRE(left != nullptr);
      REQUIRE(quiet != nullptr);
      REQUIRE(right != nullptr);
      REQUIRE(keyOsc != nullptr);
      REQUIRE(comp != nullptr);
      REQUIRE(dac != nullptr);
      REQUIRE(box != nullptr);
      src.Connect(left, 0, quiet, 0);
      src.Connect(quiet, 0, comp, 0);
      src.Connect(right, 0, comp, 1);
      src.Connect(keyOsc, 0, comp, 2);
      src.Connect(comp, 0, dac, 0);
      src.Connect(comp, 1, dac, 1);
      src.Connect(comp, 2, box, 0);
      meterID = box->GetID();
      dump = src.DumpJSON();
    }
    CHECK(dump.find("~comp") != std::string::npos);
    patcherImplementation dst(2, nullptr);
    dst.ParseJSON(dump);
    YSE::pHandle* box = dst.GetHandleFromID(meterID);
    REQUIRE(box != nullptr);

    // The same chain, made here.
    phasor leftRef;
    phasor rightRef;
    phasor keyRef;
    compressor reference;
    reference.threshold(-30.f).ratio(8.f).attack(1.f).release(30.f).makeup(2.f);
    MULTICHANNELBUFFER ref(2);
    MULTICHANNELBUFFER refKey(1);
    float deepest = 0.f;
    for (int block = 0; block < 20; ++block) {
      CAPTURE(block);
      dst.Calculate(YSE::T_DSP);
      buffer& l = leftRef(375.f, N);
      ref[0].resize(N);
      for (unsigned int i = 0; i < N; ++i)
        ref[0].getPtr()[i] = l.getPtr()[i] * 0.05f;
      ref[1] = rightRef(750.f, N);
      refKey[0] = keyRef(50.f, N);
      reference.process(ref, refKey);
      CHECK(TestHelpers::buffersNearlyEqual(dst.output[0], ref[0], 0.f));
      CHECK(TestHelpers::buffersNearlyEqual(dst.output[1], ref[1], 0.f));
      CHECK(GuiFloat(box) == doctest::Approx(reference.gainReductionDb()).epsilon(1e-4));
      deepest = std::fmin(deepest, GuiFloat(box));
    }
    // The key's ramp reaches near 0 dB: deep reduction, though the left
    // signal alone stays under the threshold most of the time.
    CHECK(deepest < -10.f);
  }

  TEST_CASE("~comp: messages, numbers, blocks and the flush on the audio thread allocate "
            "nothing (#1116)") {
    FloatSink meter;
    BufferSink out;
    dComp comp;
    comp.ConnectOutlet(out.GetInlet(0), 0);
    out.ConnectInlet(comp.GetOutlet(0), 0);
    comp.ConnectOutlet(meter.GetInlet(0), 2);
    meter.ConnectInlet(comp.GetOutlet(2), 0);
    comp.SetMessage("meterrate 0", 0.f);
    buffer in(N);
    in = 0.8f;
    buffer key(N);
    key = 1.f;
    const std::string threshold = "threshold -6";
    const std::string detector = "detector 1";
    const std::string meterdata = "meterdata";
    {
      TestHelpers::ProbeScope probe;
      comp.GetInlet(0)->SetMessage(threshold, YSE::T_DSP);
      comp.GetInlet(0)->SetMessage(detector, YSE::T_DSP);
      comp.GetInlet(0)->SetMessage(meterdata, YSE::T_DSP);
      comp.GetInlet(1)->SetFloat(0.5f, YSE::T_DSP);
      comp.GetInlet(1)->SetInt(1, YSE::T_DSP);
      // The first block: the constructor sized everything.
      comp.ResetDSP();
      comp.GetInlet(0)->SetBuffer(&in, YSE::T_DSP);
      comp.Calculate(YSE::T_DSP);
      comp.FlushArmedOutlets(YSE::T_GUI);
      comp.ResetDSP();
      comp.GetInlet(0)->SetBuffer(&in, YSE::T_DSP);
      comp.GetInlet(1)->SetBuffer(&in, YSE::T_DSP);
      comp.GetInlet(2)->SetBuffer(&key, YSE::T_DSP);
      comp.Calculate(YSE::T_DSP);
      comp.FlushArmedOutlets(YSE::T_GUI);
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
    CHECK(meter.gotFloat);
  }

  // ─── the controls of #1241 ──────────────────────────────────────────────

  TEST_CASE("~comp: range, gate, delay, sidechain filter, coupling, smoothing, freeze and "
            "progressive release reach the class through the settings (#1241)") {
    TestHelpers::ScopedSampleRate rate(48000);
    {
      // Off until set: the class's defaults.
      dComp obj;
      obj.Calculate(YSE::T_DSP);
      compressor& f = obj.Compressor();
      CHECK(f.range() == compressor::MAX_RANGE);
      CHECK(f.gainSmoothing() == 0.f);
      CHECK_FALSE(f.freeze());
      CHECK_FALSE(f.progressiveRelease());
      CHECK_FALSE(f.gate());
      CHECK(f.gateThreshold() == compressor::DEFAULT_GATE_THRESHOLD);
      CHECK(f.lookahead() == 0u);
      CHECK_FALSE(f.sidechainFilter());
      CHECK(f.sidechainFrequency() == compressor::DEFAULT_SIDECHAIN_FREQUENCY);
      CHECK(f.linked());
      CHECK(f.maxLookahead() == dComp::MAX_DELAY_SAMPLES);
    }
    {
      dComp obj;
      const std::string args =
          "range 12 smoothgain 5 freezelevel 1 progressiverelease 1 ngenabled 1 ngthreshold -70 "
          "delay 2 sidechainfilterenabled 1 sidechainfilterfreq 250 channelcoupling 0";
      obj.SetParams(args);
      CHECK(obj.GetParams() == args);
      obj.Calculate(YSE::T_DSP);
      compressor& f = obj.Compressor();
      CHECK(f.range() == 12.f);
      CHECK(f.gainSmoothing() == 5.f);
      CHECK(f.freeze());
      CHECK(f.progressiveRelease());
      CHECK(f.gate());
      CHECK(f.gateThreshold() == -70.f);
      CHECK(f.lookahead() == 96u); // 2 ms at 48 kHz
      CHECK(f.latency() == 96u);
      CHECK(f.sidechainFilter());
      CHECK(f.sidechainFrequency() == 250.f);
      CHECK_FALSE(f.linked());
    }
    {
      // Clamped by the class, and the delay at the object's delay line.
      dComp obj;
      obj.SetParams("range 500 smoothgain -3 ngthreshold -200 delay 100000 "
                    "sidechainfilterfreq 1 channelcoupling 1");
      obj.Calculate(YSE::T_DSP);
      compressor& f = obj.Compressor();
      CHECK(f.range() == compressor::MAX_RANGE);
      CHECK(f.gainSmoothing() == 0.f);
      CHECK(f.gateThreshold() == compressor::MIN_GATE_THRESHOLD);
      CHECK(f.lookahead() == dComp::MAX_DELAY_SAMPLES);
      CHECK(f.sidechainFrequency() == compressor::MIN_SIDECHAIN_FREQUENCY);
      CHECK(f.linked());
      obj.SetParams("delay -5");
      obj.Calculate(YSE::T_DSP);
      CHECK(obj.Compressor().lookahead() == 0u);
    }
  }

  TEST_CASE("~comp: the #1241 messages against the DSP class, bit for bit (#1241)") {
    TestHelpers::ScopedSampleRate rate(48000);
    BufferSink out0;
    BufferSink out1;
    dComp obj;
    obj.SetParams("threshold -24 ratio 6 attack 1 release 30 meters 0");
    obj.ConnectOutlet(out0.GetInlet(0), 0);
    out0.ConnectInlet(obj.GetOutlet(0), 0);
    obj.ConnectOutlet(out1.GetInlet(0), 1);
    out1.ConnectInlet(obj.GetOutlet(1), 0);

    compressor reference(dComp::MAX_DELAY_SAMPLES, 2);
    reference.threshold(-24.f).ratio(6.f).attack(1.f).release(30.f);
    buffer objIn0(N);
    buffer objIn1(N);
    MULTICHANNELBUFFER ref(2);
    ref[0].resize(N);
    ref[1].resize(N);
    unsigned int start = 0;
    const auto step = [&]() {
      for (int i = 0; i < 6; ++i) {
        obj.ResetDSP();
        Fill(objIn0, start, 0);
        Fill(ref[0], start, 0);
        Fill(objIn1, start + 300, 1);
        Fill(ref[1], start + 300, 1);
        start += N;
        obj.GetInlet(0)->SetBuffer(&objIn0, YSE::T_GUI);
        obj.GetInlet(1)->SetBuffer(&objIn1, YSE::T_GUI);
        obj.Calculate(YSE::T_DSP);
        REQUIRE(out0.received != nullptr);
        REQUIRE(out1.received != nullptr);
        reference.process(ref);
        CHECK(TestHelpers::buffersNearlyEqual(*out0.received, ref[0], 0.f));
        CHECK(TestHelpers::buffersNearlyEqual(*out1.received, ref[1], 0.f));
      }
    };

    step();
    obj.SetMessage("range 6", 0.f);
    reference.range(6.f);
    step();
    obj.SetMessage("smoothgain 10", 0.f);
    reference.gainSmoothing(10.f);
    step();
    obj.SetMessage("progressiverelease 1", 0.f);
    reference.progressiveRelease(true);
    step();
    obj.SetMessage("ngenabled 1", 0.f);
    reference.gate(true);
    step();
    obj.SetMessage("ngthreshold -20", 0.f);
    reference.gateThreshold(-20.f);
    step();
    obj.GetInlet(0)->SetMessage("delay 1.5", YSE::T_GUI);
    reference.lookahead(72); // 1.5 ms at 48 kHz
    step();
    CHECK(obj.Compressor().latency() == 72u);
    obj.SetMessage("sidechainfilterenabled 1", 0.f);
    reference.sidechainFilter(true);
    step();
    obj.SetMessage("sidechainfilterfreq 400", 0.f);
    reference.sidechainFrequency(400.f);
    step();
    obj.SetMessage("channelcoupling 0", 0.f);
    reference.linked(false);
    step();
    obj.SetMessage("freezelevel 1", 0.f);
    reference.freeze(true);
    step();
    obj.SetMessage("freezelevel 0", 0.f);
    reference.freeze(false);
    obj.SetMessage("delay 0", 0.f);
    reference.lookahead(0);
    obj.SetMessage("channelcoupling 1", 0.f);
    reference.linked(true);
    step();
    CHECK(reference.gainReductionDb() < -0.5f);
  }

  TEST_CASE("~comp: the #1241 settings in a rendered patch after a JSON round trip (#1241)") {
    TestHelpers::ScopedSampleRate rate(48000);
    const std::string args = "threshold -30 ratio 8 attack 1 release 30 range 9 smoothgain 2 "
                             "progressiverelease 1 ngenabled 1 ngthreshold -40 delay 1 "
                             "sidechainfilterenabled 1 sidechainfilterfreq 200 channelcoupling 0 "
                             "meterrate 0";
    std::string dump;
    unsigned int compID = 0;
    {
      patcherImplementation src(2, nullptr);
      YSE::pHandle* left = src.CreateObject(YSE::OBJ::D_PHASOR, "375");
      YSE::pHandle* quiet = src.CreateObject(YSE::OBJ::D_MULTIPLY, "0.05");
      YSE::pHandle* right = src.CreateObject(YSE::OBJ::D_PHASOR, "750");
      YSE::pHandle* comp = src.CreateObject(YSE::OBJ::D_COMP, args);
      YSE::pHandle* dac = src.CreateObject(YSE::OBJ::D_DAC, "");
      REQUIRE(left != nullptr);
      REQUIRE(quiet != nullptr);
      REQUIRE(right != nullptr);
      REQUIRE(comp != nullptr);
      REQUIRE(dac != nullptr);
      src.Connect(left, 0, quiet, 0);
      src.Connect(quiet, 0, comp, 0);
      src.Connect(right, 0, comp, 1);
      src.Connect(comp, 0, dac, 0);
      src.Connect(comp, 1, dac, 1);
      compID = comp->GetID();
      dump = src.DumpJSON();
    }
    CHECK(dump.find("channelcoupling 0") != std::string::npos);
    patcherImplementation dst(2, nullptr);
    dst.ParseJSON(dump);
    YSE::pHandle* comp = dst.GetHandleFromID(compID);
    REQUIRE(comp != nullptr);

    // The same chain, made here.
    phasor leftRef;
    phasor rightRef;
    compressor reference(dComp::MAX_DELAY_SAMPLES, 2);
    reference.threshold(-30.f).ratio(8.f).attack(1.f).release(30.f).range(9.f).gainSmoothing(2.f);
    reference.progressiveRelease(true).gate(true).gateThreshold(-40.f).lookahead(48);
    reference.sidechainFilter(true).sidechainFrequency(200.f).linked(false);
    MULTICHANNELBUFFER ref(2);
    for (int block = 0; block < 20; ++block) {
      CAPTURE(block);
      dst.Calculate(YSE::T_DSP);
      buffer& l = leftRef(375.f, N);
      ref[0].resize(N);
      for (unsigned int i = 0; i < N; ++i)
        ref[0].getPtr()[i] = l.getPtr()[i] * 0.05f;
      ref[1] = rightRef(750.f, N);
      reference.process(ref);
      CHECK(TestHelpers::buffersNearlyEqual(dst.output[0], ref[0], 0.f));
      CHECK(TestHelpers::buffersNearlyEqual(dst.output[1], ref[1], 0.f));
    }
    // Unlinked: the loud right channel is held to the range, the quiet
    // left one is not touched by the compressor; the delay is 48 samples.
    CHECK(reference.gainReductionDb() == doctest::Approx(-9.f).epsilon(0.05));
    CHECK(reference.latency() == 48u);
  }

  TEST_CASE("~comp: every #1241 control on, changed by messages on the audio thread, "
            "allocates nothing (#1241)") {
    TestHelpers::ScopedSampleRate rate(48000);
    BufferSink out;
    dComp comp;
    comp.SetParams("range 10 smoothgain 3 progressiverelease 1 ngenabled 1 delay 1 "
                   "sidechainfilterenabled 1 channelcoupling 0 detector 1 mix 0.7");
    comp.ConnectOutlet(out.GetInlet(0), 0);
    out.ConnectInlet(comp.GetOutlet(0), 0);
    buffer in(N);
    in = 0.8f;
    buffer key(N);
    key = 1.f;
    const std::string delayLong = "delay 1000";
    const std::string delayShort = "delay 0.25";
    const std::string freeze = "freezelevel 1";
    const std::string coupled = "channelcoupling 1";
    const std::string freq = "sidechainfilterfreq 900";
    const std::string gate = "ngthreshold -30";
    {
      TestHelpers::ProbeScope probe;
      const auto block = [&](bool sidechain) {
        comp.ResetDSP();
        comp.GetInlet(0)->SetBuffer(&in, YSE::T_DSP);
        comp.GetInlet(1)->SetBuffer(&in, YSE::T_DSP);
        if (sidechain) comp.GetInlet(2)->SetBuffer(&key, YSE::T_DSP);
        comp.Calculate(YSE::T_DSP);
      };
      block(false);
      comp.GetInlet(0)->SetMessage(delayLong, YSE::T_DSP); // held at the delay line
      block(true);
      comp.GetInlet(0)->SetMessage(delayShort, YSE::T_DSP);
      comp.GetInlet(0)->SetMessage(freeze, YSE::T_DSP);
      comp.GetInlet(0)->SetMessage(coupled, YSE::T_DSP);
      comp.GetInlet(0)->SetMessage(freq, YSE::T_DSP);
      comp.GetInlet(0)->SetMessage(gate, YSE::T_DSP);
      block(false);
      block(true);
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
    CHECK(comp.Compressor().lookahead() == 12u);
  }

} // TEST_SUITE("patcher")
