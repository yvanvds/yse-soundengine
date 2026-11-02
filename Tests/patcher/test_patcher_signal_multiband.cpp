// Tests for the ~multiband patcher object (patcher/dynamics/dMultiband),
// issue #1117. The compression is DSP::multibandCompressor's and is tested in
// Tests/dsp/test_multiband_compressor.cpp; these show that the band count,
// the pins, the settings parameter, the messages and the per-band meter
// outlets reach it, alone and in a rendered patch after a JSON round trip.

#include <doctest/doctest.h>
#include <algorithm>
#include <cmath>
#include <memory>
#include <string>
#include "patcher/patcher.hpp"
#include "patcher/patcherImplementation.h"
#include "patcher/pHandle.hpp"
#include "patcher/pObjectList.hpp"
#include "patcher/pRegistry.h"
#include "patcher/dynamics/dMultiband.h"
#include "dsp/dynamics/multibandCompressor.hpp"
#include "dsp/generators/phasor.hpp"
#include "headers/constants.hpp"
#include "support/alloc_probe.hpp"
#include "support/audio_helpers.hpp"
#include "sinks.hpp"

using TestHelpers::BufferSink;
using TestHelpers::FloatSink;
using YSE::DSP::buffer;
using YSE::DSP::multibandCompressor;
using YSE::DSP::phasor;
using YSE::PATCHER::dMultiband;
using YSE::PATCHER::patcherImplementation;

namespace {
  constexpr unsigned int N = YSE::STANDARD_BUFFERSIZE;
  constexpr double PI = 3.14159265358979323846;

  // Loud and quiet stretches of a low and a high tone, a little different
  // per channel.
  void Fill(buffer& b, unsigned int start, unsigned int channel) {
    for (unsigned int i = 0; i < b.getLength(); ++i) {
      const auto t = static_cast<double>(start + i) / 48000.0;
      const double level = ((start + i) / 2000) % 2 == 0 ? 0.9 : 0.05;
      const double c = static_cast<double>(channel);
      b.getPtr()[i] =
          static_cast<float>(level * (0.6 * std::sin(2.0 * PI * (60.0 + 20.0 * c) * t) +
                                      0.4 * std::sin(2.0 * PI * (7000.0 - 1000.0 * c) * t)));
    }
  }

  float GuiFloat(YSE::pHandle* h) {
    return std::stof(h->GetGuiValue());
  }
} // namespace

TEST_SUITE("patcher") {

  TEST_CASE("~multiband: registry and shape, from the band count (#1117)") {
    YSE::patcher p;
    p.create(1);
    YSE::pHandle* obj = p.CreateObject(YSE::OBJ::D_MULTIBAND);
    REQUIRE(obj != nullptr);
    CHECK(std::string(obj->Type()) == "~multiband");
    CHECK(obj->GetInputs() == 2);
    CHECK(obj->GetOutputs() == 6);
    CHECK(obj->IsDSPInput(0));
    CHECK(obj->IsDSPInput(1));
    CHECK(obj->OutputDataType(0) == YSE::OUT_TYPE::BUFFER);
    CHECK(obj->OutputDataType(1) == YSE::OUT_TYPE::BUFFER);
    for (unsigned int i = 2; i < 6; ++i)
      CHECK(obj->OutputDataType(i) == YSE::OUT_TYPE::FLOAT);

    YSE::pHandle* five = p.CreateObject(YSE::OBJ::D_MULTIBAND, "5");
    REQUIRE(five != nullptr);
    CHECK(five->GetInputs() == 2);
    CHECK(five->GetOutputs() == 7);
    CHECK(five->OutputDataType(6) == YSE::OUT_TYPE::FLOAT);

    std::unique_ptr<YSE::PATCHER::pObject> o(YSE::PATCHER::Register().Get(YSE::OBJ::D_MULTIBAND));
    REQUIRE(o != nullptr);
    CHECK(o->GetCategory() == YSE::PATCHER::pCategory::DYNAMICS);
    CHECK(o->IsDSPObject());
    CHECK(o->GetParamCount() == 1);
  }

  TEST_CASE("~multiband: the settings parameter (#1117)") {
    {
      // The class's defaults.
      dMultiband obj;
      CHECK(obj.BandCount() == 4);
      multibandCompressor& f = obj.Compressor();
      CHECK(f.channels() == 2);
      CHECK(f.crossover(0) == 200.f);
      CHECK(f.threshold(3) == -18.f);
      CHECK(f.ratio(0) == 4.f);
      CHECK(f.output() == 0.f);
    }
    {
      dMultiband obj;
      const std::string args = "5 threshold -30 ratio 3 threshold2 -24 ratio5 10 attack 2 "
                               "release4 300 makeup 2 drive3 6 level1 -4 freq1 80 freq4 9000 "
                               "output -2 detector 1 bypass 0 meters 0 meterrate 50";
      obj.SetParams(args);
      CHECK(obj.GetParams() == args);
      CHECK(obj.BandCount() == 5);
      CHECK(obj.NumInputs() == 2);
      CHECK(obj.NumOutputs() == 7);
      multibandCompressor& f = obj.Compressor();
      CHECK(f.bands() == 5);
      CHECK(f.threshold(0) == -30.f);
      CHECK(f.threshold(1) == -24.f);
      CHECK(f.threshold(4) == -30.f);
      CHECK(f.ratio(0) == 3.f);
      CHECK(f.ratio(4) == 10.f);
      CHECK(f.attack(2) == 2.f);
      CHECK(f.release(3) == 300.f);
      CHECK(f.release(2) == 100.f);
      CHECK(f.makeup(4) == 2.f);
      CHECK(f.drive(2) == 6.f);
      CHECK(f.drive(1) == 0.f);
      CHECK(f.level(0) == -4.f);
      CHECK(f.crossover(0) == 80.f);
      CHECK(f.crossover(1) == 500.f);
      CHECK(f.crossover(3) == 9000.f);
      CHECK(f.output() == -2.f);
      CHECK(f.detector() == YSE::DSP::MODULES::DETECT_RMS);
      CHECK_FALSE(f.bypass());
    }
    {
      // Clamped counts and values; a band or crossover past the count,
      // malformed and unknown words are ignored; words alone are fine.
      dMultiband obj;
      obj.SetParams("9 threshold -100 ratio x what 3 freq 50 bypass 1");
      CHECK(obj.BandCount() == 5);
      CHECK(obj.Compressor().threshold(0) == -60.f);
      CHECK(obj.Compressor().ratio(0) == 4.f);
      CHECK(obj.Compressor().bypass());

      dMultiband two;
      two.SetParams("1 threshold3 -40 freq2 50 freq1 700");
      CHECK(two.BandCount() == 2);
      CHECK(two.NumOutputs() == 4);
      CHECK(two.Compressor().threshold(0) == -18.f);
      CHECK(two.Compressor().threshold(1) == -18.f);
      CHECK(two.Compressor().crossover(0) == 700.f);

      dMultiband words;
      words.SetParams("ratio 2");
      CHECK(words.BandCount() == 4);
      CHECK(words.Compressor().ratio(3) == 2.f);

      // Cleared: back to 4 bands and the defaults.
      dMultiband cleared;
      cleared.SetParams("3 ratio 9");
      cleared.SetParams("");
      CHECK(cleared.BandCount() == 4);
      CHECK(cleared.Compressor().ratio(0) == 4.f);
    }
  }

  TEST_CASE("~multiband: signals, numbers and messages against the DSP class (#1117, #1036)") {
    TestHelpers::ScopedSampleRate rate(48000);
    BufferSink out0;
    BufferSink out1;
    dMultiband obj;
    obj.SetParams("5 threshold -26 ratio 5 attack 1 release 40 meters 0");
    obj.ConnectOutlet(out0.GetInlet(0), 0);
    out0.ConnectInlet(obj.GetOutlet(0), 0);
    obj.ConnectOutlet(out1.GetInlet(0), 1);
    out1.ConnectInlet(obj.GetOutlet(1), 0);

    multibandCompressor reference(5, 2);
    for (unsigned int b = 0; b < 5; ++b)
      reference.threshold(b, -26.f).ratio(b, 5.f).attack(b, 1.f).release(b, 40.f);
    buffer objIn0(N);
    buffer objIn1(N);
    MULTICHANNELBUFFER ref(2);
    ref[0].resize(N);
    ref[1].resize(N);
    unsigned int start = 0;
    bool connect1 = true;
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
      start += N;
      obj.GetInlet(0)->SetBuffer(&objIn0, YSE::T_GUI);
      if (connect1) obj.GetInlet(1)->SetBuffer(&objIn1, YSE::T_GUI);
      obj.Calculate(YSE::T_DSP);
      REQUIRE(out0.received != nullptr);
      REQUIRE(out1.received != nullptr);
      reference.process(ref);
      CHECK(TestHelpers::buffersNearlyEqual(*out0.received, ref[0], 0.f));
      CHECK(TestHelpers::buffersNearlyEqual(*out1.received, ref[1], 0.f));
    };

    for (int i = 0; i < 40; ++i)
      step();
    CHECK(reference.gainReduction(0) < -1.f);
    obj.SetMessage("threshold -12", 0.f);
    for (unsigned int b = 0; b < 5; ++b)
      reference.threshold(b, -12.f);
    step();
    obj.GetInlet(0)->SetMessage("ratio2 8", YSE::T_GUI);
    reference.ratio(1, 8.f);
    step();
    obj.SetMessage("attack5 3", 0.f);
    reference.attack(4, 3.f);
    step();
    obj.SetMessage("release 200", 0.f);
    for (unsigned int b = 0; b < 5; ++b)
      reference.release(b, 200.f);
    step();
    obj.SetMessage("makeup1 4", 0.f);
    reference.makeup(0, 4.f);
    step();
    obj.SetMessage("drive 3", 0.f);
    for (unsigned int b = 0; b < 5; ++b)
      reference.drive(b, 3.f);
    step();
    obj.SetMessage("level4 -6", 0.f);
    reference.level(3, -6.f);
    step();
    obj.SetMessage("freq2 700", 0.f);
    reference.crossover(1, 700.f);
    step();
    obj.SetMessage("output 2", 0.f);
    reference.output(2.f);
    step();
    obj.SetMessage("detector 1", 0.f);
    reference.detector(YSE::DSP::MODULES::DETECT_RMS);
    step();
    obj.SetMessage("bypass 1", 0.f);
    reference.bypass(true);
    step();
    obj.SetMessage("bypass 0", 0.f);
    reference.bypass(false);
    step();
    obj.SetMessage("savesettings", 0.f); // not ported: ignored
    obj.SetMessage("ratio y", 0.f);
    obj.SetMessage("threshold9 -50", 0.f);
    step();

    connect1 = false;
    obj.GetInlet(1)->SetFloat(-0.5f, YSE::T_GUI);
    constant1 = -0.5f;
    step();
    obj.GetInlet(1)->SetInt(1, YSE::T_GUI);
    constant1 = 1.f;
    step();
  }

  TEST_CASE("~multiband: the per-band meters, armed and sent after the block (#1117, #1037)") {
    TestHelpers::ScopedSampleRate rate(48000);
    FloatSink low;
    FloatSink high;
    dMultiband obj;
    obj.SetParams("4 attack 0.1");
    obj.ConnectOutlet(low.GetInlet(0), 2);
    low.ConnectInlet(obj.GetOutlet(2), 0);
    obj.ConnectOutlet(high.GetInlet(0), 5);
    high.ConnectInlet(obj.GetOutlet(5), 0);
    buffer in(N);
    unsigned int start = 0;
    const auto block = [&](float level) {
      obj.ResetDSP();
      for (unsigned int i = 0; i < N; ++i) {
        const auto t = static_cast<double>(start + i) / 48000.0;
        in.getPtr()[i] = level * static_cast<float>(std::sin(2.0 * PI * 50.0 * t));
      }
      start += N;
      obj.GetInlet(0)->SetBuffer(&in, YSE::T_GUI);
      obj.GetInlet(1)->SetBuffer(&in, YSE::T_GUI);
      low.gotFloat = false;
      high.gotFloat = false;
      obj.Calculate(YSE::T_DSP);
      CHECK_FALSE(low.gotFloat); // never during the block
      obj.FlushArmedOutlets(YSE::T_GUI);
    };

    // Default: meters on, every 100 ms = 4800 samples = 37.5 blocks, so the
    // first report comes after block 38.
    for (int i = 0; i < 37; ++i) {
      block(1.f);
      CHECK_FALSE(low.gotFloat);
    }
    block(1.f);
    REQUIRE(low.gotFloat);
    REQUIRE(high.gotFloat);
    // A loud 50 Hz tone: the low band is compressed, the top band is not.
    CHECK(low.received < -6.f);
    CHECK(high.received > -0.5f);
    CHECK(high.received <= 0.f);

    // meterrate 0: every block; silence reports 0 once recovered.
    obj.SetMessage("meterrate 0", 0.f);
    block(1.f);
    CHECK(low.gotFloat);
    for (int i = 0; i < 400; ++i)
      block(0.f);
    CHECK(low.gotFloat);
    CHECK(low.received > -0.01f);

    // meters 0: silent, until meterdata asks for one report.
    obj.SetMessage("meters 0", 0.f);
    block(1.f);
    CHECK_FALSE(low.gotFloat);
    block(1.f);
    CHECK_FALSE(low.gotFloat);
    obj.SetMessage("meterdata", 0.f);
    block(1.f);
    CHECK(low.gotFloat);
    CHECK(high.gotFloat);
    CHECK(low.received < -1.f); // the deepest of the three
    block(1.f);
    CHECK_FALSE(low.gotFloat);
  }

  TEST_CASE("~multiband: in a rendered patch, a 5-band compressor on a stereo signal and its "
            "band meters reach number boxes, after a JSON round trip (#1117)") {
    TestHelpers::ScopedSampleRate rate(48000);
    std::string dump;
    unsigned int lowID = 0;
    unsigned int topID = 0;
    {
      patcherImplementation src(2, nullptr);
      YSE::pHandle* left = src.CreateObject(YSE::OBJ::D_PHASOR, "55");
      YSE::pHandle* right = src.CreateObject(YSE::OBJ::D_PHASOR, "3000");
      YSE::pHandle* quiet = src.CreateObject(YSE::OBJ::D_MULTIPLY, "0.1");
      YSE::pHandle* mb = src.CreateObject(
          YSE::OBJ::D_MULTIBAND,
          "5 threshold -24 ratio 6 attack 1 release 30 freq1 100 level5 -3 output 1 meterrate 0");
      YSE::pHandle* dac = src.CreateObject(YSE::OBJ::D_DAC, "");
      YSE::pHandle* lowBox = src.CreateObject(YSE::OBJ::G_FLOAT, "");
      YSE::pHandle* topBox = src.CreateObject(YSE::OBJ::G_FLOAT, "");
      REQUIRE(left != nullptr);
      REQUIRE(right != nullptr);
      REQUIRE(quiet != nullptr);
      REQUIRE(mb != nullptr);
      REQUIRE(dac != nullptr);
      REQUIRE(lowBox != nullptr);
      REQUIRE(topBox != nullptr);
      src.Connect(left, 0, mb, 0);
      src.Connect(right, 0, quiet, 0);
      src.Connect(quiet, 0, mb, 1);
      src.Connect(mb, 0, dac, 0);
      src.Connect(mb, 1, dac, 1);
      src.Connect(mb, 2, lowBox, 0);
      src.Connect(mb, 6, topBox, 0);
      lowID = lowBox->GetID();
      topID = topBox->GetID();
      dump = src.DumpJSON();
    }
    CHECK(dump.find("~multiband") != std::string::npos);
    patcherImplementation dst(2, nullptr);
    dst.ParseJSON(dump);
    YSE::pHandle* lowBox = dst.GetHandleFromID(lowID);
    YSE::pHandle* topBox = dst.GetHandleFromID(topID);
    REQUIRE(lowBox != nullptr);
    REQUIRE(topBox != nullptr);

    // The same chain, made here.
    phasor leftRef;
    phasor rightRef;
    multibandCompressor reference(5, 2);
    for (unsigned int b = 0; b < 5; ++b)
      reference.threshold(b, -24.f).ratio(b, 6.f).attack(b, 1.f).release(b, 30.f);
    reference.crossover(0, 100.f).level(4, -3.f).output(1.f);
    MULTICHANNELBUFFER ref(2);
    float deepest = 0.f;
    for (int block = 0; block < 30; ++block) {
      CAPTURE(block);
      dst.Calculate(YSE::T_DSP);
      ref[0] = leftRef(55.f, N);
      buffer& r = rightRef(3000.f, N);
      ref[1].resize(N);
      for (unsigned int i = 0; i < N; ++i)
        ref[1].getPtr()[i] = r.getPtr()[i] * 0.1f;
      reference.process(ref);
      CHECK(TestHelpers::buffersNearlyEqual(dst.output[0], ref[0], 0.f));
      CHECK(TestHelpers::buffersNearlyEqual(dst.output[1], ref[1], 0.f));
      CHECK(GuiFloat(lowBox) == doctest::Approx(reference.gainReduction(0)).epsilon(1e-4));
      CHECK(GuiFloat(topBox) == doctest::Approx(reference.gainReduction(4)).epsilon(1e-4));
      deepest = std::min(deepest, GuiFloat(lowBox));
    }
    // The left ramp's 55 Hz fundamental lands in the lowest band, near full
    // scale: deep reduction there.
    CHECK(deepest < -6.f);
  }

  TEST_CASE("~multiband: messages, numbers, blocks and the flush on the audio thread allocate "
            "nothing (#1117)") {
    FloatSink meter;
    BufferSink out;
    dMultiband mb;
    mb.SetParams("5");
    mb.ConnectOutlet(out.GetInlet(0), 0);
    out.ConnectInlet(mb.GetOutlet(0), 0);
    mb.ConnectOutlet(meter.GetInlet(0), 2);
    meter.ConnectInlet(mb.GetOutlet(2), 0);
    mb.SetMessage("meterrate 0", 0.f);
    buffer in(N);
    in = 0.8f;
    const std::string threshold = "threshold -6";
    const std::string band = "ratio3 9";
    const std::string freq = "freq2 600";
    const std::string meterdata = "meterdata";
    {
      TestHelpers::ProbeScope probe;
      mb.GetInlet(0)->SetMessage(threshold, YSE::T_DSP);
      mb.GetInlet(0)->SetMessage(band, YSE::T_DSP);
      mb.GetInlet(0)->SetMessage(freq, YSE::T_DSP);
      mb.GetInlet(0)->SetMessage(meterdata, YSE::T_DSP);
      mb.GetInlet(1)->SetFloat(0.5f, YSE::T_DSP);
      mb.GetInlet(1)->SetInt(1, YSE::T_DSP);
      // The first block: the constructor sized everything.
      mb.ResetDSP();
      mb.GetInlet(0)->SetBuffer(&in, YSE::T_DSP);
      mb.Calculate(YSE::T_DSP);
      mb.FlushArmedOutlets(YSE::T_GUI);
      mb.ResetDSP();
      mb.GetInlet(0)->SetBuffer(&in, YSE::T_DSP);
      mb.GetInlet(1)->SetBuffer(&in, YSE::T_DSP);
      mb.Calculate(YSE::T_DSP);
      mb.FlushArmedOutlets(YSE::T_GUI);
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
    CHECK(meter.gotFloat);
  }

} // TEST_SUITE("patcher")
