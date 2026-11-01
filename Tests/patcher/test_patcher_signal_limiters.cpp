// Tests for the ~limi and ~peaklim patcher objects (patcher/dynamics/dLimi,
// dPeaklim), issue #1115. The limiting is DSP::lookaheadLimiter's and is
// tested in Tests/dsp/test_lookahead_limiter.cpp; these show that the pins,
// the settings parameter, the messages and the meter outlet reach it, alone
// and in rendered patches after a JSON round trip.

#include <doctest/doctest.h>
#include <cmath>
#include <memory>
#include <string>
#include <vector>
#include "patcher/patcher.hpp"
#include "patcher/patcherImplementation.h"
#include "patcher/pHandle.hpp"
#include "patcher/pObjectList.hpp"
#include "patcher/pRegistry.h"
#include "patcher/dynamics/dLimi.h"
#include "patcher/dynamics/dPeaklim.h"
#include "dsp/dynamics/lookaheadLimiter.hpp"
#include "dsp/generators/phasor.hpp"
#include "headers/constants.hpp"
#include "support/alloc_probe.hpp"
#include "support/audio_helpers.hpp"
#include "sinks.hpp"

using TestHelpers::BufferSink;
using TestHelpers::FloatSink;
using YSE::DSP::buffer;
using YSE::DSP::lookaheadLimiter;
using YSE::DSP::phasor;
using YSE::PATCHER::dLimi;
using YSE::PATCHER::dPeaklim;
using YSE::PATCHER::patcherImplementation;

namespace {
  constexpr unsigned int N = YSE::STANDARD_BUFFERSIZE;

  // Loud and quiet stretches, a little different per channel.
  void Fill(buffer& b, unsigned int start, unsigned int channel) {
    for (unsigned int i = 0; i < b.getLength(); ++i) {
      const auto n = static_cast<float>(start + i);
      const float level = ((start + i) / 300) % 2 == 0 ? 2.5f : 0.3f;
      b.getPtr()[i] = level * std::sin(n * (0.04f + 0.01f * static_cast<float>(channel)));
    }
  }

  float GuiFloat(YSE::pHandle* h) {
    return std::stof(h->GetGuiValue());
  }

  float Largest(buffer& b) {
    float largest = 0.f;
    for (unsigned int i = 0; i < b.getLength(); ++i)
      largest = std::fmax(largest, std::fabs(b.getPtr()[i]));
    return largest;
  }
} // namespace

TEST_SUITE("patcher") {

  TEST_CASE("~limi: registry and shape (#1115)") {
    YSE::patcher p;
    p.create(1);
    YSE::pHandle* obj = p.CreateObject(YSE::OBJ::D_LIMI);
    REQUIRE(obj != nullptr);
    CHECK(std::string(obj->Type()) == "~limi");
    CHECK(obj->GetInputs() == 1);
    CHECK(obj->GetOutputs() == 1);
    CHECK(obj->IsDSPInput(0));
    CHECK(obj->OutputDataType(0) == YSE::OUT_TYPE::BUFFER);

    YSE::pHandle* three = p.CreateObject(YSE::OBJ::D_LIMI, "3");
    REQUIRE(three != nullptr);
    CHECK(three->GetInputs() == 3);
    CHECK(three->GetOutputs() == 3);
    for (unsigned int i = 0; i < 3; ++i) {
      CHECK(three->IsDSPInput(i));
      CHECK(three->OutputDataType(i) == YSE::OUT_TYPE::BUFFER);
    }

    std::unique_ptr<YSE::PATCHER::pObject> o(YSE::PATCHER::Register().Get(YSE::OBJ::D_LIMI));
    REQUIRE(o != nullptr);
    CHECK(o->GetCategory() == YSE::PATCHER::pCategory::DYNAMICS);
    CHECK(o->IsDSPObject());
    CHECK(o->GetParamCount() == 1);
  }

  TEST_CASE("~limi: the settings parameter (#1115)") {
    {
      dLimi obj;
      CHECK(obj.ChannelCount() == 1);
      CHECK(obj.Limiter().maxLookahead() == lookaheadLimiter::DEFAULT_MAX_LOOKAHEAD);
    }
    {
      dLimi obj;
      const std::string args = "2 256 threshold -6 lookahead 32 release 20 preamp 3 postamp -1 "
                               "mode 0 dcblock 1 bypass 0";
      obj.SetParams(args);
      CHECK(obj.GetParams() == args);
      CHECK(obj.ChannelCount() == 2);
      CHECK(obj.NumInputs() == 2);
      CHECK(obj.NumOutputs() == 2);
      CHECK(obj.Limiter().maxLookahead() == 256);
      // The settings reach the class at the first block.
      obj.Calculate(YSE::T_DSP);
      const lookaheadLimiter& f = obj.Limiter();
      CHECK(f.threshold() == -6.f);
      CHECK(f.lookahead() == 32);
      CHECK(f.release() == 20.f);
      CHECK(f.preamp() == 3.f);
      CHECK(f.postamp() == -1.f);
      CHECK(f.mode() == lookaheadLimiter::LINEAR);
      CHECK(f.dcBlock());
      CHECK_FALSE(f.bypass());
    }
    {
      // Clamped counts, a lookahead past the line, malformed and unknown
      // words ignored; words alone are fine.
      dLimi obj;
      obj.SetParams("0 16 lookahead 999 threshold x what 3");
      CHECK(obj.ChannelCount() == 1);
      obj.Calculate(YSE::T_DSP);
      CHECK(obj.Limiter().lookahead() == 16);
      CHECK(obj.Limiter().threshold() == 0.f);

      dLimi many;
      many.SetParams("100");
      CHECK(many.ChannelCount() == static_cast<int>(lookaheadLimiter::MAX_CHANNELS));

      dLimi words;
      words.SetParams("threshold -3");
      CHECK(words.ChannelCount() == 1);
      words.Calculate(YSE::T_DSP);
      CHECK(words.Limiter().threshold() == -3.f);
    }
  }

  TEST_CASE("~limi: signals, numbers and messages against the DSP class (#1115, #1036)") {
    TestHelpers::ScopedSampleRate rate(48000);
    BufferSink out0;
    BufferSink out1;
    dLimi obj;
    obj.SetParams("2 256 threshold -6 lookahead 32");
    obj.ConnectOutlet(out0.GetInlet(0), 0);
    out0.ConnectInlet(obj.GetOutlet(0), 0);
    obj.ConnectOutlet(out1.GetInlet(0), 1);
    out1.ConnectInlet(obj.GetOutlet(1), 0);

    lookaheadLimiter reference(2, 256);
    reference.threshold(-6.f).lookahead(32);
    buffer objIn0(N);
    buffer objIn1(N);
    buffer refIn0(N);
    buffer refIn1(N);
    unsigned int start = 0;
    float constant1 = 0.f;
    bool connect1 = true;

    const auto step = [&]() {
      obj.ResetDSP(); // as the patcher does before every block
      Fill(objIn0, start, 0);
      Fill(refIn0, start, 0);
      Fill(objIn1, start, 1);
      if (connect1)
        Fill(refIn1, start, 1);
      else
        refIn1 = constant1;
      start += N;
      obj.GetInlet(0)->SetBuffer(&objIn0, YSE::T_GUI);
      if (connect1) obj.GetInlet(1)->SetBuffer(&objIn1, YSE::T_GUI);
      obj.Calculate(YSE::T_DSP);
      REQUIRE(out0.received != nullptr);
      REQUIRE(out1.received != nullptr);
      buffer* list[2] = {&refIn0, &refIn1};
      reference(list, 2);
      CHECK(TestHelpers::buffersNearlyEqual(*out0.received, reference.output(0), 0.f));
      CHECK(TestHelpers::buffersNearlyEqual(*out1.received, reference.output(1), 0.f));
    };

    for (int i = 0; i < 6; ++i)
      step();
    CHECK(reference.gainReduction() < 0.f);

    obj.SetMessage("threshold -9", 0.f);
    reference.threshold(-9.f);
    step();
    obj.GetInlet(0)->SetMessage("release 12", YSE::T_GUI);
    reference.release(12.f);
    step();
    obj.GetInlet(0)->SetList("preamp 2", YSE::T_GUI);
    reference.preamp(2.f);
    step();
    obj.SetMessage("postamp -3", 0.f);
    reference.postamp(-3.f);
    step();
    obj.SetMessage("mode 0", 0.f);
    reference.mode(lookaheadLimiter::LINEAR);
    step();
    obj.SetMessage("dcblock 1", 0.f);
    reference.dcBlock(true);
    step();
    obj.SetMessage("lookahead 64", 0.f);
    reference.lookahead(64);
    step();
    obj.SetMessage("bypass 1", 0.f);
    reference.bypass(true);
    step();
    obj.SetMessage("bypass 0", 0.f);
    reference.bypass(false);
    step();
    obj.SetMessage("clear", 0.f);
    reference.clear();
    step();
    CHECK(obj.Limiter().lookahead() == 64);

    // Unknown and malformed messages are ignored.
    obj.SetMessage("threshold", 0.f);
    obj.SetMessage("threshold x", 0.f);
    obj.SetMessage("loud 3", 0.f);
    obj.SetMessage("", 0.f);
    step();
    CHECK(obj.Limiter().threshold() == -9.f);

    // An unconnected right inlet reads its last number, float or int.
    connect1 = false;
    obj.GetInlet(1)->SetFloat(0.75f, YSE::T_GUI);
    constant1 = 0.75f;
    step();
    obj.GetInlet(1)->SetInt(2, YSE::T_GUI);
    constant1 = 2.f;
    step();
    step();
  }

  TEST_CASE("~limi: in a rendered patch, a loud ramp from ~phasor is limited bit for bit, after "
            "a JSON round trip (#1115)") {
    TestHelpers::ScopedSampleRate rate(48000);
    std::string dump;
    {
      patcherImplementation src(2, nullptr);
      YSE::pHandle* ramp = src.CreateObject(YSE::OBJ::D_PHASOR, "375");
      YSE::pHandle* loud = src.CreateObject(YSE::OBJ::D_MULTIPLY, "4");
      YSE::pHandle* limi = src.CreateObject(YSE::OBJ::D_LIMI, "1 512 threshold -6 lookahead 64");
      YSE::pHandle* cmd = src.CreateObject(YSE::OBJ::G_RECEIVE, "cmd1115");
      YSE::pHandle* dac = src.CreateObject(YSE::OBJ::D_DAC, "");
      REQUIRE(ramp != nullptr);
      REQUIRE(loud != nullptr);
      REQUIRE(limi != nullptr);
      REQUIRE(cmd != nullptr);
      REQUIRE(dac != nullptr);
      src.Connect(ramp, 0, loud, 0);
      src.Connect(loud, 0, limi, 0);
      src.Connect(cmd, 0, limi, 0);
      src.Connect(limi, 0, dac, 0);
      dump = src.DumpJSON();
    }
    CHECK(dump.find("~limi") != std::string::npos);
    CHECK(dump.find("threshold -6 lookahead 64") != std::string::npos);
    patcherImplementation dst(2, nullptr);
    dst.ParseJSON(dump);

    // The same chain, made here: ~phasor 375 at 48 kHz rises 1/128 per
    // sample, times 4.
    phasor rampRef;
    lookaheadLimiter reference(1, 512);
    reference.threshold(-6.f).lookahead(64);
    buffer loudRef(N);
    const float ceiling = std::pow(10.f, -6.f / 20.f);
    for (int block = 0; block < 12; ++block) {
      CAPTURE(block);
      if (block == 6) {
        REQUIRE(dst.PassData("threshold -12", "cmd1115", YSE::T_GUI));
        reference.threshold(-12.f);
      }
      dst.Calculate(YSE::T_DSP);
      buffer& r = rampRef(375.f, N);
      for (unsigned int i = 0; i < N; ++i)
        loudRef.getPtr()[i] = r.getPtr()[i] * 4.f;
      buffer& expected = reference(loudRef);
      CHECK(TestHelpers::buffersNearlyEqual(dst.output[0], expected, 0.f));
      if (block >= 1 && block < 6) {
        CHECK(Largest(dst.output[0]) <= ceiling);
        CHECK(Largest(dst.output[0]) > ceiling * 0.95f);
      }
    }
    CHECK(Largest(dst.output[0]) <= std::pow(10.f, -12.f / 20.f));
    // The first 64 samples are the lookahead: silence.
    patcherImplementation again(2, nullptr);
    again.ParseJSON(dump);
    again.Calculate(YSE::T_DSP);
    for (unsigned int i = 0; i < 64; ++i)
      CHECK(again.output[0].getPtr()[i] == 0.f);
    CHECK(again.output[0].getPtr()[65] != 0.f);
  }

  TEST_CASE("~limi: a stereo ~limi 2 links its channels in a rendered patch, after a JSON round "
            "trip (#1115)") {
    TestHelpers::ScopedSampleRate rate(48000);
    std::string dump;
    {
      patcherImplementation src(2, nullptr);
      // Left quiet, right loud: the left is turned down with the right.
      YSE::pHandle* left = src.CreateObject(YSE::OBJ::D_CYCLE, "375");
      YSE::pHandle* quiet = src.CreateObject(YSE::OBJ::D_MULTIPLY, "0.25");
      YSE::pHandle* right = src.CreateObject(YSE::OBJ::D_CYCLE, "750");
      YSE::pHandle* loud = src.CreateObject(YSE::OBJ::D_MULTIPLY, "4");
      YSE::pHandle* limi = src.CreateObject(YSE::OBJ::D_LIMI, "2 128 lookahead 16");
      YSE::pHandle* dac = src.CreateObject(YSE::OBJ::D_DAC, "");
      REQUIRE(limi != nullptr);
      src.Connect(left, 0, quiet, 0);
      src.Connect(quiet, 0, limi, 0);
      src.Connect(right, 0, loud, 0);
      src.Connect(loud, 0, limi, 1);
      src.Connect(limi, 0, dac, 0);
      src.Connect(limi, 1, dac, 1);
      dump = src.DumpJSON();
    }
    patcherImplementation dst(2, nullptr);
    dst.ParseJSON(dump);
    for (int block = 0; block < 8; ++block)
      dst.Calculate(YSE::T_DSP);
    // The right peaks at the threshold (1); the left, at 0.25 before, comes
    // out near 0.25 / 4: the gain the right needs.
    CHECK(Largest(dst.output[1]) <= 1.f);
    CHECK(Largest(dst.output[1]) > 0.95f);
    CHECK(Largest(dst.output[0]) < 0.1f);
    CHECK(Largest(dst.output[0]) > 0.04f);
  }

  TEST_CASE("~peaklim: registry and shape (#1115)") {
    YSE::patcher p;
    p.create(1);
    YSE::pHandle* obj = p.CreateObject(YSE::OBJ::D_PEAKLIM);
    REQUIRE(obj != nullptr);
    CHECK(std::string(obj->Type()) == "~peaklim");
    CHECK(obj->GetInputs() == 2);
    CHECK(obj->GetOutputs() == 3);
    CHECK(obj->IsDSPInput(0));
    CHECK(obj->IsDSPInput(1));
    CHECK(obj->OutputDataType(0) == YSE::OUT_TYPE::BUFFER);
    CHECK(obj->OutputDataType(1) == YSE::OUT_TYPE::BUFFER);
    CHECK(obj->OutputDataType(2) == YSE::OUT_TYPE::FLOAT);

    std::unique_ptr<YSE::PATCHER::pObject> o(YSE::PATCHER::Register().Get(YSE::OBJ::D_PEAKLIM));
    REQUIRE(o != nullptr);
    CHECK(o->GetCategory() == YSE::PATCHER::pCategory::DYNAMICS);
    CHECK(o->IsDSPObject());
    CHECK(o->GetParamCount() == 1);

    dPeaklim d;
    CHECK(d.Limiter().channels() == 2);
    CHECK(d.Limiter().lookahead() == dPeaklim::LOOKAHEAD);
    CHECK(d.Limiter().latency() == 64);
  }

  TEST_CASE("~peaklim: signals, settings and messages against the DSP class (#1115, #1036)") {
    TestHelpers::ScopedSampleRate rate(48000);
    BufferSink out0;
    BufferSink out1;
    dPeaklim obj;
    const std::string args = "threshold -3 ingain 2 outgain -1 mode 0 meters 0";
    obj.SetParams(args);
    CHECK(obj.GetParams() == args);
    obj.ConnectOutlet(out0.GetInlet(0), 0);
    out0.ConnectInlet(obj.GetOutlet(0), 0);
    obj.ConnectOutlet(out1.GetInlet(0), 1);
    out1.ConnectInlet(obj.GetOutlet(1), 0);

    lookaheadLimiter reference(2, dPeaklim::LOOKAHEAD);
    reference.lookahead(dPeaklim::LOOKAHEAD)
        .threshold(-3.f)
        .preamp(2.f)
        .postamp(-1.f)
        .mode(lookaheadLimiter::LINEAR);
    buffer objIn0(N);
    buffer objIn1(N);
    buffer refIn0(N);
    buffer refIn1(N);
    unsigned int start = 0;
    bool connect1 = true;
    float constant1 = 0.f;
    const auto step = [&]() {
      obj.ResetDSP();
      Fill(objIn0, start, 0);
      Fill(refIn0, start, 0);
      Fill(objIn1, start, 1);
      if (connect1)
        Fill(refIn1, start, 1);
      else
        refIn1 = constant1;
      start += N;
      obj.GetInlet(0)->SetBuffer(&objIn0, YSE::T_GUI);
      if (connect1) obj.GetInlet(1)->SetBuffer(&objIn1, YSE::T_GUI);
      obj.Calculate(YSE::T_DSP);
      REQUIRE(out0.received != nullptr);
      REQUIRE(out1.received != nullptr);
      buffer* list[2] = {&refIn0, &refIn1};
      reference(list, 2);
      CHECK(TestHelpers::buffersNearlyEqual(*out0.received, reference.output(0), 0.f));
      CHECK(TestHelpers::buffersNearlyEqual(*out1.received, reference.output(1), 0.f));
    };

    for (int i = 0; i < 4; ++i)
      step();
    obj.SetMessage("threshold -8", 0.f);
    reference.threshold(-8.f);
    step();
    obj.GetInlet(0)->SetMessage("ingain 4", YSE::T_GUI);
    reference.preamp(4.f);
    step();
    obj.SetMessage("outgain 1", 0.f);
    reference.postamp(1.f);
    step();
    obj.SetMessage("mode 1", 0.f);
    reference.mode(lookaheadLimiter::EXPONENTIAL);
    step();
    obj.SetMessage("bypass 1", 0.f);
    reference.bypass(true);
    step();
    obj.SetMessage("bypass 0", 0.f);
    reference.bypass(false);
    step();
    obj.SetMessage("savesettings", 0.f); // not ported: ignored
    obj.SetMessage("threshold y", 0.f);
    step();

    connect1 = false;
    obj.GetInlet(1)->SetFloat(-1.5f, YSE::T_GUI);
    constant1 = -1.5f;
    step();
    obj.GetInlet(1)->SetInt(1, YSE::T_GUI);
    constant1 = 1.f;
    step();
  }

  TEST_CASE("~peaklim: the gain-reduction meter, armed and sent after the block (#1115, "
            "#1037)") {
    TestHelpers::ScopedSampleRate rate(48000);
    FloatSink meter;
    dPeaklim obj;
    obj.ConnectOutlet(meter.GetInlet(0), 2);
    meter.ConnectInlet(obj.GetOutlet(2), 0);
    buffer in(N);
    unsigned int start = 0;
    const auto block = [&](float level) {
      obj.ResetDSP();
      for (unsigned int i = 0; i < N; ++i)
        in.getPtr()[i] = level * std::sin(static_cast<float>(start + i) * 0.05f);
      start += N;
      obj.GetInlet(0)->SetBuffer(&in, YSE::T_GUI);
      meter.gotFloat = false;
      obj.Calculate(YSE::T_DSP);
      CHECK_FALSE(meter.gotFloat); // never during the block
      obj.FlushArmedOutlets(YSE::T_GUI);
    };

    // Default: meters on, every 100 ms = 4800 samples = 37.5 blocks, so the
    // first report comes after block 38.
    for (int i = 0; i < 37; ++i) {
      block(4.f);
      CHECK_FALSE(meter.gotFloat);
    }
    block(4.f);
    REQUIRE(meter.gotFloat);
    // A level of 4 against a threshold of 1: about -12 dB at the deepest.
    CHECK(meter.received < -11.f);
    CHECK(meter.received > -12.1f);
    meter.gotFloat = false;

    // meterrate 0: every block; quiet input reports 0 once recovered.
    obj.SetMessage("meterrate 0", 0.f);
    block(4.f);
    CHECK(meter.gotFloat);
    meter.gotFloat = false;
    for (int i = 0; i < 400; ++i)
      block(0.1f);
    CHECK(meter.gotFloat);
    CHECK(meter.received == 0.f);
    meter.gotFloat = false;

    // meters 0: silent, until meterdata asks for one report.
    obj.SetMessage("meters 0", 0.f);
    block(4.f);
    block(4.f);
    CHECK_FALSE(meter.gotFloat);
    obj.SetMessage("meterdata", 0.f);
    block(4.f);
    CHECK(meter.gotFloat);
    CHECK(meter.received < -11.f); // the deepest of the three blocks
    meter.gotFloat = false;
    block(4.f);
    CHECK_FALSE(meter.gotFloat);
  }

  TEST_CASE("~peaklim: in a rendered patch, a loud stereo signal is held under the threshold "
            "and the meter reaches a number box, after a JSON round trip (#1115)") {
    TestHelpers::ScopedSampleRate rate(48000);
    std::string dump;
    unsigned int meterID = 0;
    {
      patcherImplementation src(2, nullptr);
      YSE::pHandle* ramp = src.CreateObject(YSE::OBJ::D_PHASOR, "375");
      YSE::pHandle* loud = src.CreateObject(YSE::OBJ::D_MULTIPLY, "4");
      YSE::pHandle* osc = src.CreateObject(YSE::OBJ::D_PHASOR, "750");
      YSE::pHandle* lim =
          src.CreateObject(YSE::OBJ::D_PEAKLIM, "threshold -6 outgain -2 meterrate 0");
      YSE::pHandle* dac = src.CreateObject(YSE::OBJ::D_DAC, "");
      YSE::pHandle* box = src.CreateObject(YSE::OBJ::G_FLOAT, "");
      REQUIRE(ramp != nullptr);
      REQUIRE(loud != nullptr);
      REQUIRE(osc != nullptr);
      REQUIRE(lim != nullptr);
      REQUIRE(dac != nullptr);
      REQUIRE(box != nullptr);
      src.Connect(ramp, 0, loud, 0);
      src.Connect(loud, 0, lim, 0);
      src.Connect(osc, 0, lim, 1);
      src.Connect(lim, 0, dac, 0);
      src.Connect(lim, 1, dac, 1);
      src.Connect(lim, 2, box, 0);
      meterID = box->GetID();
      dump = src.DumpJSON();
    }
    CHECK(dump.find("~peaklim") != std::string::npos);
    patcherImplementation dst(2, nullptr);
    dst.ParseJSON(dump);
    YSE::pHandle* box = dst.GetHandleFromID(meterID);
    REQUIRE(box != nullptr);

    // The same chain, made here.
    phasor rampRef;
    phasor rightRef;
    lookaheadLimiter reference(2, dPeaklim::LOOKAHEAD);
    reference.lookahead(dPeaklim::LOOKAHEAD).threshold(-6.f).postamp(-2.f);
    buffer loudRef(N);
    const float ceiling = std::pow(10.f, -8.f / 20.f);
    for (int block = 0; block < 10; ++block) {
      CAPTURE(block);
      dst.Calculate(YSE::T_DSP);
      buffer& r = rampRef(375.f, N);
      for (unsigned int i = 0; i < N; ++i)
        loudRef.getPtr()[i] = r.getPtr()[i] * 4.f;
      buffer* list[2] = {&loudRef, &rightRef(750.f, N)};
      reference(list, 2);
      CHECK(TestHelpers::buffersNearlyEqual(dst.output[0], reference.output(0), 0.f));
      CHECK(TestHelpers::buffersNearlyEqual(dst.output[1], reference.output(1), 0.f));
      CHECK(Largest(dst.output[0]) <= ceiling);
      CHECK(Largest(dst.output[1]) <= ceiling);
      if (block >= 1) {
        CHECK(Largest(dst.output[0]) > ceiling * 0.95f);
        // ~4 against 0.5: about -18 dB.
        CHECK(GuiFloat(box) < -17.f);
        CHECK(GuiFloat(box) > -18.2f);
      }
    }
  }

  TEST_CASE("~limi and ~peaklim: messages, numbers, blocks and the flush on the audio thread "
            "allocate nothing (#1115)") {
    BufferSink out;
    dLimi limi;
    limi.SetParams("2 256");
    limi.ConnectOutlet(out.GetInlet(0), 0);
    out.ConnectInlet(limi.GetOutlet(0), 0);
    FloatSink meter;
    dPeaklim peak;
    peak.ConnectOutlet(meter.GetInlet(0), 2);
    meter.ConnectInlet(peak.GetOutlet(2), 0);
    peak.SetMessage("meterrate 0", 0.f);
    buffer in(N);
    in = 3.f;
    const std::string threshold = "threshold -6";
    const std::string lookahead = "lookahead 200";
    const std::string clear = "clear";
    const std::string ingain = "ingain 3";
    const std::string meterdata = "meterdata";
    limi.Calculate(YSE::T_DSP);
    peak.Calculate(YSE::T_DSP);
    peak.FlushArmedOutlets(YSE::T_GUI);
    {
      TestHelpers::ProbeScope probe;
      limi.GetInlet(0)->SetMessage(threshold, YSE::T_DSP);
      limi.GetInlet(0)->SetMessage(lookahead, YSE::T_DSP);
      limi.GetInlet(0)->SetMessage(clear, YSE::T_DSP);
      limi.GetInlet(1)->SetFloat(0.5f, YSE::T_DSP);
      limi.GetInlet(1)->SetInt(1, YSE::T_DSP);
      limi.ResetDSP();
      limi.GetInlet(0)->SetBuffer(&in, YSE::T_DSP);
      limi.Calculate(YSE::T_DSP);
      peak.GetInlet(0)->SetMessage(ingain, YSE::T_DSP);
      peak.GetInlet(0)->SetMessage(meterdata, YSE::T_DSP);
      peak.GetInlet(0)->SetFloat(0.5f, YSE::T_DSP);
      peak.ResetDSP();
      peak.GetInlet(0)->SetBuffer(&in, YSE::T_DSP);
      peak.GetInlet(1)->SetBuffer(&in, YSE::T_DSP);
      peak.Calculate(YSE::T_DSP);
      peak.FlushArmedOutlets(YSE::T_GUI);
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
    CHECK(meter.gotFloat);
  }

} // TEST_SUITE("patcher")
