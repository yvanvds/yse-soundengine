// Tests for the ~overdrive patcher object (patcher/math/dOverdrive), issue
// #1112. The soft clipping is DSP::overdrive's and is tested in
// Tests/dsp/test_overdrive.cpp; these show that the signals, numbers and the
// argument reach it, alone and in rendered patches after a JSON round trip:
// bit-exact on a ramp from ~phasor, and a loud ~cycle rounded off into -1..1.

#include <doctest/doctest.h>
#include <cmath>
#include <memory>
#include <string>
#include "patcher/patcher.hpp"
#include "patcher/patcherImplementation.h"
#include "patcher/pHandle.hpp"
#include "patcher/pObjectList.hpp"
#include "patcher/pRegistry.h"
#include "patcher/math/dOverdrive.h"
#include "dsp/operators/overdrive.hpp"
#include "dsp/generators/phasor.hpp"
#include "headers/constants.hpp"
#include "support/alloc_probe.hpp"
#include "support/audio_helpers.hpp"
#include "sinks.hpp"

using TestHelpers::BufferSink;
using YSE::DSP::buffer;
using YSE::DSP::overdrive;
using YSE::DSP::phasor;
using YSE::PATCHER::dOverdrive;
using YSE::PATCHER::patcherImplementation;

namespace {
  constexpr unsigned int N = YSE::STANDARD_BUFFERSIZE;

  void Wire(dOverdrive& obj, BufferSink& out) {
    obj.ConnectOutlet(out.GetInlet(0), 0);
    out.ConnectInlet(obj.GetOutlet(0), 0);
  }

  void Fill(buffer& b, unsigned int start) {
    for (unsigned int i = 0; i < b.getLength(); ++i) {
      const auto n = static_cast<float>(start + i);
      b.getPtr()[i] = 1.2f * std::sin(n * 0.01f) + 0.15f * std::sin(n * 2.1f);
    }
  }
} // namespace

TEST_SUITE("patcher") {

  TEST_CASE("~overdrive: registry and shape (#1112)") {
    YSE::patcher p;
    p.create(1);
    YSE::pHandle* obj = p.CreateObject(YSE::OBJ::D_OVERDRIVE);
    REQUIRE(obj != nullptr);
    CHECK(std::string(obj->Type()) == "~overdrive");
    CHECK(obj->GetInputs() == 2);
    CHECK(obj->GetOutputs() == 1);
    for (unsigned int i = 0; i < 2; ++i)
      CHECK(obj->IsDSPInput(i));
    CHECK(obj->OutputDataType(0) == YSE::OUT_TYPE::BUFFER);

    std::unique_ptr<YSE::PATCHER::pObject> o(YSE::PATCHER::Register().Get(YSE::OBJ::D_OVERDRIVE));
    REQUIRE(o != nullptr);
    CHECK(o->GetCategory() == YSE::PATCHER::pCategory::MATH);
    CHECK(o->IsDSPObject());
    CHECK(o->GetParamCount() == 1);
  }

  TEST_CASE("~overdrive: signals and numbers against the DSP class (#1112, #1036)") {
    BufferSink out;
    dOverdrive obj;
    Wire(obj, out);
    overdrive reference;
    buffer objIn(N);
    buffer refIn(N);
    unsigned int start = 0;
    float drive = overdrive::DEFAULT_DRIVE;

    const auto step = [&](buffer* d = nullptr) {
      obj.ResetDSP(); // as the patcher does before every block
      Fill(objIn, start);
      Fill(refIn, start);
      start += N;
      obj.GetInlet(0)->SetBuffer(&objIn, YSE::T_GUI);
      if (d != nullptr) obj.GetInlet(1)->SetBuffer(d, YSE::T_GUI);
      obj.Calculate(YSE::T_DSP);
      REQUIRE(out.received != nullptr);
      buffer ds(N);
      ds = drive;
      if (d != nullptr) ds = *d;
      CHECK(TestHelpers::buffersNearlyEqual(*out.received, reference(refIn, ds), 0.f));
    };

    // Default: linear, clipped at full scale.
    step();
    {
      buffer expected(N);
      Fill(expected, 0);
      for (unsigned int i = 0; i < N; ++i) {
        CAPTURE(i);
        const float x = expected.getPtr()[i];
        CHECK(out.received->getPtr()[i] == (x >= 1.f ? 1.f : (x <= -1.f ? -1.f : x)));
      }
    }

    // Numbers in the right inlet, floats and ints.
    obj.GetInlet(1)->SetFloat(2.5f, YSE::T_GUI);
    drive = 2.5f;
    step();
    CHECK(reference.drive() == 2.5f);
    obj.GetInlet(1)->SetInt(6, YSE::T_GUI);
    drive = 6.f;
    step();
    CHECK(reference.drive() == 6.f);
    obj.GetInlet(1)->SetFloat(0.5f, YSE::T_GUI);
    drive = 0.5f;
    step();
    obj.GetInlet(1)->SetInt(-2, YSE::T_GUI);
    drive = -2.f;
    step();
    // Not finite: the DSP class keeps the last drive.
    obj.GetInlet(1)->SetFloat(std::nanf(""), YSE::T_GUI);
    drive = std::nanf("");
    step();
    CHECK(reference.drive() == -2.f);
    obj.GetInlet(1)->SetFloat(3.f, YSE::T_GUI);
    drive = 3.f;
    step();

    // A signal wins over the number and is read per sample.
    buffer d(N);
    for (unsigned int i = 0; i < N; ++i)
      d.getPtr()[i] = 1.f + static_cast<float>(i % 16) * 0.5f;
    step(&d);
    CHECK(reference.drive() == d.getPtr()[N - 1]);
    // Back to unconnected: the last number again.
    step();
    CHECK(reference.drive() == 3.f);

    // Unconnected input: silence.
    BufferSink idleOut;
    dOverdrive idle;
    Wire(idle, idleOut);
    idle.SetParams("4");
    idle.Calculate(YSE::T_DSP);
    REQUIRE(idleOut.received != nullptr);
    CHECK(idleOut.received->getPtr()[N - 1] == 0.f);
  }

  TEST_CASE("~overdrive: the argument is the drive (#1112)") {
    const auto check = [](const std::string& args, float drive) {
      CAPTURE(args);
      BufferSink out;
      dOverdrive obj;
      Wire(obj, out);
      obj.SetParams(args);
      CHECK(obj.GetParams() == args);
      overdrive reference;
      reference.drive(drive);
      buffer objIn(N);
      buffer refIn(N);
      for (unsigned int block = 0; block < 3; ++block) {
        CAPTURE(block);
        obj.ResetDSP();
        Fill(objIn, block * N);
        Fill(refIn, block * N);
        obj.GetInlet(0)->SetBuffer(&objIn, YSE::T_GUI);
        obj.Calculate(YSE::T_DSP);
        REQUIRE(out.received != nullptr);
        CHECK(TestHelpers::buffersNearlyEqual(*out.received, reference(refIn), 0.f));
      }
    };

    check("4", 4.f);
    check("1.5", 1.5f);
    check("0.25", 0.25f);
    check("10", 10.f);
    check("", overdrive::DEFAULT_DRIVE);
  }

  TEST_CASE("~overdrive: in a rendered patch, a ramp from ~phasor is shaped, with the drive "
            "from a receive, after a JSON round trip (#1112)") {
    TestHelpers::ScopedSampleRate rate(48000);
    std::string dump;
    {
      patcherImplementation src(2, nullptr);
      YSE::pHandle* ramp = src.CreateObject(YSE::OBJ::D_PHASOR, "375");
      YSE::pHandle* drive = src.CreateObject(YSE::OBJ::D_OVERDRIVE, "2");
      YSE::pHandle* amount = src.CreateObject(YSE::OBJ::G_RECEIVE, "drive1112");
      YSE::pHandle* dac = src.CreateObject(YSE::OBJ::D_DAC, "");
      REQUIRE(ramp != nullptr);
      REQUIRE(drive != nullptr);
      REQUIRE(amount != nullptr);
      REQUIRE(dac != nullptr);
      src.Connect(ramp, 0, drive, 0);
      src.Connect(amount, 0, drive, 1);
      src.Connect(drive, 0, dac, 0);
      dump = src.DumpJSON();
    }
    CHECK(dump.find("~overdrive") != std::string::npos);
    patcherImplementation dst(2, nullptr);
    dst.ParseJSON(dump);

    // The same ramp, made here: ~phasor 375 at 48 kHz rises 1/128 per sample.
    phasor rampRef;
    overdrive reference;
    reference.drive(2.f);
    for (int block = 0; block < 6; ++block) {
      CAPTURE(block);
      if (block == 3) {
        REQUIRE(dst.PassData(5.f, "drive1112", YSE::T_GUI));
        reference.drive(5.f);
      }
      dst.Calculate(YSE::T_DSP);
      buffer& expected = reference(rampRef(375.f, N));
      CHECK(TestHelpers::buffersNearlyEqual(dst.output[0], expected, 0.f));
    }
    // The first block by hand, at drive 2: 1 - (1 - x)^2 of the ramp.
    {
      patcherImplementation again(2, nullptr);
      again.ParseJSON(dump);
      again.Calculate(YSE::T_DSP);
      const float* y = again.output[0].getPtr();
      CHECK(y[0] == 0.f);
      CHECK(y[64] == doctest::Approx(0.75f).epsilon(1e-6)); // x = 0.5
      CHECK(y[96] == doctest::Approx(0.9375f).epsilon(1e-6)); // x = 0.75
      CHECK(y[32] == doctest::Approx(0.4375f).epsilon(1e-6)); // x = 0.25
    }
  }

  TEST_CASE("~overdrive: ~cycle -> ~* 3 -> ~overdrive 3 rounds a loud sine off into -1..1, "
            "after a JSON round trip (#1112)") {
    TestHelpers::ScopedSampleRate rate(48000);
    std::string dump;
    {
      patcherImplementation src(2, nullptr);
      YSE::pHandle* osc = src.CreateObject(YSE::OBJ::D_CYCLE, "375");
      YSE::pHandle* loud = src.CreateObject(YSE::OBJ::D_MULTIPLY, "3");
      YSE::pHandle* drive = src.CreateObject(YSE::OBJ::D_OVERDRIVE, "3");
      YSE::pHandle* dac = src.CreateObject(YSE::OBJ::D_DAC, "");
      REQUIRE(osc != nullptr);
      REQUIRE(loud != nullptr);
      REQUIRE(drive != nullptr);
      REQUIRE(dac != nullptr);
      src.Connect(osc, 0, loud, 0);
      src.Connect(loud, 0, drive, 0);
      src.Connect(drive, 0, dac, 0);
      dump = src.DumpJSON();
    }
    patcherImplementation dst(2, nullptr);
    dst.ParseJSON(dump);

    // 375 Hz at 48 kHz: one cycle per block. The output stays within -1..1,
    // reaches both ends, and only the samples close to the two zero
    // crossings are below 0.9: the drive 3 lifts everything else.
    for (int block = 0; block < 4; ++block) {
      CAPTURE(block);
      dst.Calculate(YSE::T_DSP);
      float highest = -2.f;
      float lowest = 2.f;
      unsigned int quiet = 0;
      for (unsigned int i = 0; i < N; ++i) {
        const float y = dst.output[0].getPtr()[i];
        CHECK(y <= 1.f);
        CHECK(y >= -1.f);
        highest = std::fmax(highest, y);
        lowest = std::fmin(lowest, y);
        if (std::fabs(y) < 0.9f) ++quiet;
      }
      CHECK(highest == 1.f);
      CHECK(lowest == -1.f);
      // Below 0.9 only for |3 sin| < 0.54, within about 3.6 samples of each
      // crossing: 7 or 8 samples per crossing.
      CHECK(quiet >= 12);
      CHECK(quiet <= 18);
    }
  }

  TEST_CASE("~overdrive: numbers and blocks on the audio thread allocate nothing (#1112)") {
    BufferSink out;
    dOverdrive obj;
    Wire(obj, out);
    buffer in(N);
    buffer d(N);
    in = 0.5f;
    d = 3.f;
    obj.Calculate(YSE::T_DSP);
    {
      TestHelpers::ProbeScope probe;
      obj.GetInlet(1)->SetFloat(2.f, YSE::T_DSP);
      obj.GetInlet(1)->SetInt(4, YSE::T_DSP);
      obj.Calculate(YSE::T_DSP);
      obj.ResetDSP();
      obj.GetInlet(0)->SetBuffer(&in, YSE::T_DSP);
      obj.GetInlet(1)->SetBuffer(&d, YSE::T_DSP);
      obj.Calculate(YSE::T_DSP);
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
  }

} // TEST_SUITE("patcher")
