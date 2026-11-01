// Tests for the ~degrade patcher object (patcher/math/dDegrade), issue #1106.
// The degrading is DSP::degrade's and is tested in Tests/dsp/test_degrade.cpp;
// these show that the signals, numbers and arguments reach it, alone and in a
// rendered patch after a JSON round trip, bit-exact on a ramp from ~phasor.

#include <doctest/doctest.h>
#include <cmath>
#include <memory>
#include <string>
#include "patcher/patcher.hpp"
#include "patcher/patcherImplementation.h"
#include "patcher/pHandle.hpp"
#include "patcher/pObjectList.hpp"
#include "patcher/pRegistry.h"
#include "patcher/math/dDegrade.h"
#include "dsp/operators/degrade.hpp"
#include "dsp/generators/phasor.hpp"
#include "headers/constants.hpp"
#include "support/alloc_probe.hpp"
#include "support/audio_helpers.hpp"
#include "sinks.hpp"

using TestHelpers::BufferSink;
using YSE::DSP::buffer;
using YSE::DSP::degrade;
using YSE::DSP::phasor;
using YSE::PATCHER::dDegrade;
using YSE::PATCHER::patcherImplementation;

namespace {
  constexpr unsigned int N = YSE::STANDARD_BUFFERSIZE;

  void Wire(dDegrade& obj, BufferSink& out) {
    obj.ConnectOutlet(out.GetInlet(0), 0);
    out.ConnectInlet(obj.GetOutlet(0), 0);
  }

  void Fill(buffer& b, unsigned int start) {
    for (unsigned int i = 0; i < b.getLength(); ++i) {
      const auto n = static_cast<float>(start + i);
      b.getPtr()[i] = 0.8f * std::sin(n * 0.01f) + 0.15f * std::sin(n * 2.1f);
    }
  }
} // namespace

TEST_SUITE("patcher") {

  TEST_CASE("~degrade: registry and shape (#1106)") {
    YSE::patcher p;
    p.create(1);
    YSE::pHandle* obj = p.CreateObject(YSE::OBJ::D_DEGRADE);
    REQUIRE(obj != nullptr);
    CHECK(std::string(obj->Type()) == "~degrade");
    CHECK(obj->GetInputs() == 3);
    CHECK(obj->GetOutputs() == 1);
    for (unsigned int i = 0; i < 3; ++i)
      CHECK(obj->IsDSPInput(i));
    CHECK(obj->OutputDataType(0) == YSE::OUT_TYPE::BUFFER);

    std::unique_ptr<YSE::PATCHER::pObject> o(YSE::PATCHER::Register().Get(YSE::OBJ::D_DEGRADE));
    REQUIRE(o != nullptr);
    CHECK(o->GetCategory() == YSE::PATCHER::pCategory::MATH);
    CHECK(o->IsDSPObject());
    CHECK(o->GetParamCount() == 2);
  }

  TEST_CASE("~degrade: signals and numbers against the DSP class (#1106, #1036)") {
    BufferSink out;
    dDegrade obj;
    Wire(obj, out);
    degrade reference;
    buffer objIn(N);
    buffer refIn(N);
    unsigned int start = 0;
    float ratio = degrade::DEFAULT_RATIO;
    auto bits = static_cast<float>(degrade::DEFAULT_BITS);

    const auto step = [&](buffer* r = nullptr, buffer* b = nullptr) {
      obj.ResetDSP(); // as the patcher does before every block
      Fill(objIn, start);
      Fill(refIn, start);
      start += N;
      obj.GetInlet(0)->SetBuffer(&objIn, YSE::T_GUI);
      if (r != nullptr) obj.GetInlet(1)->SetBuffer(r, YSE::T_GUI);
      if (b != nullptr) obj.GetInlet(2)->SetBuffer(b, YSE::T_GUI);
      obj.Calculate(YSE::T_DSP);
      REQUIRE(out.received != nullptr);
      buffer rs(N);
      buffer bs(N);
      rs = ratio;
      bs = bits;
      if (r != nullptr) rs = *r;
      if (b != nullptr) bs = *b;
      CHECK(TestHelpers::buffersNearlyEqual(*out.received, reference(refIn, rs, bs), 0.f));
    };

    // Default: every sample at 24 bits.
    step();
    {
      buffer expected(N);
      Fill(expected, 0);
      for (unsigned int i = 0; i < N; ++i)
        CHECK(std::fabs(out.received->getPtr()[i] - expected.getPtr()[i]) < 1.2e-7f);
    }

    // Numbers in the two right inlets, floats and ints.
    obj.GetInlet(1)->SetFloat(0.25f, YSE::T_GUI);
    ratio = 0.25f;
    step();
    obj.GetInlet(2)->SetInt(3, YSE::T_GUI);
    bits = 3.f;
    step();
    // 0.25 and 3 bits: every value a multiple of 1/4, held four samples.
    for (unsigned int i = 0; i < N; ++i) {
      CAPTURE(i);
      const float y = out.received->getPtr()[i];
      CHECK(y * 4.f == std::floor(y * 4.f));
      if (i % 4 != 0) CHECK(y == out.received->getPtr()[i - 1]);
    }
    obj.GetInlet(1)->SetInt(1, YSE::T_GUI);
    ratio = 1.f;
    step();
    obj.GetInlet(2)->SetFloat(6.5f, YSE::T_GUI);
    bits = 6.5f;
    step();
    // Out of range: the DSP class clamps.
    obj.GetInlet(1)->SetFloat(5.f, YSE::T_GUI);
    obj.GetInlet(2)->SetInt(99, YSE::T_GUI);
    ratio = 5.f;
    bits = 99.f;
    step();
    CHECK(reference.ratio() == 1.f);
    CHECK(reference.bits() == 24);
    obj.GetInlet(1)->SetFloat(0.5f, YSE::T_GUI);
    obj.GetInlet(2)->SetInt(4, YSE::T_GUI);
    ratio = 0.5f;
    bits = 4.f;
    step();

    // Signals win over the numbers and are read per sample.
    buffer r(N);
    buffer b(N);
    for (unsigned int i = 0; i < N; ++i) {
      r.getPtr()[i] = i < N / 2 ? 0.125f : 0.75f;
      b.getPtr()[i] = 2.f + static_cast<float>(i % 8);
    }
    step(&r, &b);
    CHECK(reference.ratio() == 0.75f);
    CHECK(reference.bits() == 9);
    // Only the bits connected: the ratio number again.
    step(nullptr, &b);
    // Back to unconnected: the last numbers again.
    step();
    CHECK(reference.ratio() == 0.5f);
    CHECK(reference.bits() == 4);

    // Unconnected input: silence.
    BufferSink idleOut;
    dDegrade idle;
    Wire(idle, idleOut);
    idle.SetParams("0.5 4");
    idle.Calculate(YSE::T_DSP);
    REQUIRE(idleOut.received != nullptr);
    CHECK(idleOut.received->getPtr()[N - 1] == 0.f);
  }

  TEST_CASE("~degrade: arguments are the ratio and the bits (#1106)") {
    const auto check = [](const std::string& args, float ratio, float bits) {
      CAPTURE(args);
      BufferSink out;
      dDegrade obj;
      Wire(obj, out);
      obj.SetParams(args);
      CHECK(obj.GetParams() == args);
      degrade reference;
      buffer objIn(N);
      buffer refIn(N);
      buffer rs(N);
      buffer bs(N);
      rs = ratio;
      bs = bits;
      for (unsigned int block = 0; block < 3; ++block) {
        CAPTURE(block);
        obj.ResetDSP();
        Fill(objIn, block * N);
        Fill(refIn, block * N);
        obj.GetInlet(0)->SetBuffer(&objIn, YSE::T_GUI);
        obj.Calculate(YSE::T_DSP);
        REQUIRE(out.received != nullptr);
        CHECK(TestHelpers::buffersNearlyEqual(*out.received, reference(refIn, rs, bs), 0.f));
      }
    };

    check("0.25", 0.25f, 24.f);
    check("0.25 8", 0.25f, 8.f);
    check("1 1", 1.f, 1.f);
    check("0.1 12", 0.1f, 12.f);
    check("", degrade::DEFAULT_RATIO, static_cast<float>(degrade::DEFAULT_BITS));
  }

  TEST_CASE("~degrade: in a rendered patch, a ramp from ~phasor is held and quantized, with "
            "numbers from receives, after a JSON round trip (#1106)") {
    TestHelpers::ScopedSampleRate rate(48000);
    std::string dump;
    {
      patcherImplementation src(2, nullptr);
      YSE::pHandle* ramp = src.CreateObject(YSE::OBJ::D_PHASOR, "375");
      YSE::pHandle* crush = src.CreateObject(YSE::OBJ::D_DEGRADE, "0.25 3");
      YSE::pHandle* ratio = src.CreateObject(YSE::OBJ::G_RECEIVE, "ratio1106");
      YSE::pHandle* bits = src.CreateObject(YSE::OBJ::G_RECEIVE, "bits1106");
      YSE::pHandle* dac = src.CreateObject(YSE::OBJ::D_DAC, "");
      REQUIRE(ramp != nullptr);
      REQUIRE(crush != nullptr);
      REQUIRE(ratio != nullptr);
      REQUIRE(bits != nullptr);
      REQUIRE(dac != nullptr);
      src.Connect(ramp, 0, crush, 0);
      src.Connect(ratio, 0, crush, 1);
      src.Connect(bits, 0, crush, 2);
      src.Connect(crush, 0, dac, 0);
      dump = src.DumpJSON();
    }
    CHECK(dump.find("~degrade") != std::string::npos);
    CHECK(dump.find("0.25 3") != std::string::npos);
    patcherImplementation dst(2, nullptr);
    dst.ParseJSON(dump);

    // The same ramp, made here: ~phasor 375 at 48 kHz rises 1/128 per sample.
    phasor rampRef;
    degrade reference;
    reference.ratio(0.25f).bits(3);
    for (int block = 0; block < 8; ++block) {
      CAPTURE(block);
      if (block == 3) {
        REQUIRE(dst.PassData(8, "bits1106", YSE::T_GUI));
        reference.bits(8);
      }
      if (block == 5) {
        REQUIRE(dst.PassData(0.5f, "ratio1106", YSE::T_GUI));
        reference.ratio(0.5f);
      }
      dst.Calculate(YSE::T_DSP);
      buffer& expected = reference(rampRef(375.f, N));
      CHECK(TestHelpers::buffersNearlyEqual(dst.output[0], expected, 0.f));

      // Held for 1 / ratio samples, a multiple of the step 2^(1 - bits).
      const unsigned int hold = reference.ratio() == 0.25f ? 4 : 2;
      const float steps = std::ldexp(1.f, reference.bits() - 1);
      const float* y = dst.output[0].getPtr();
      for (unsigned int i = 0; i < N; ++i) {
        CAPTURE(i);
        CHECK(y[i] * steps == std::floor(y[i] * steps));
        if (i % hold != 0) CHECK(y[i] == y[i - 1]);
      }
    }
    // The first block by hand, at 0.25 and 3 bits: the ramp at samples 0, 4,
    // 8 ... (0, 4/128, 8/128 ...) rounded down to quarters.
    {
      patcherImplementation again(2, nullptr);
      again.ParseJSON(dump);
      again.Calculate(YSE::T_DSP);
      const float* y = again.output[0].getPtr();
      CHECK(y[0] == 0.f);
      CHECK(y[31] == 0.f);
      CHECK(y[32] == 0.25f); // 32 / 128
      CHECK(y[63] == 0.25f);
      CHECK(y[64] == 0.5f);
      CHECK(y[96] == 0.75f);
      CHECK(y[127] == 0.75f);
    }
  }

  TEST_CASE("~degrade: numbers and blocks on the audio thread allocate nothing (#1106)") {
    BufferSink out;
    dDegrade obj;
    Wire(obj, out);
    buffer in(N);
    buffer r(N);
    buffer b(N);
    in = 0.5f;
    r = 0.5f;
    b = 8.f;
    obj.Calculate(YSE::T_DSP);
    {
      TestHelpers::ProbeScope probe;
      obj.GetInlet(1)->SetFloat(0.25f, YSE::T_DSP);
      obj.GetInlet(1)->SetInt(1, YSE::T_DSP);
      obj.GetInlet(2)->SetFloat(4.f, YSE::T_DSP);
      obj.GetInlet(2)->SetInt(12, YSE::T_DSP);
      obj.Calculate(YSE::T_DSP);
      obj.ResetDSP();
      obj.GetInlet(0)->SetBuffer(&in, YSE::T_DSP);
      obj.GetInlet(1)->SetBuffer(&r, YSE::T_DSP);
      obj.GetInlet(2)->SetBuffer(&b, YSE::T_DSP);
      obj.Calculate(YSE::T_DSP);
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
  }

} // TEST_SUITE("patcher")
