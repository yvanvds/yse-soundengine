// Tests for the signal math objects ~abs ~sqrt ~pow ~log ~round ~trunc
// (patcher/math/dSignalMath), issue #1050. The sample math is DSP::abs,
// DSP::sqrt, DSP::pow, DSP::log (Tests/dsp/test_math_scalars.cpp) and
// DSP::round / DSP::trunc (Tests/dsp/test_rounding.cpp); these show that pins,
// messages and parameters reach it, in isolation and in a running patch.

#include <doctest/doctest.h>
#include <cmath>
#include <memory>
#include <string>
#include "patcher/patcher.hpp"
#include "patcher/patcherImplementation.h"
#include "patcher/pHandle.hpp"
#include "patcher/pObjectList.hpp"
#include "patcher/pRegistry.h"
#include "patcher/math/dSignalMath.h"
#include "headers/constants.hpp"
#include "support/alloc_probe.hpp"
#include "sinks.hpp"

using TestHelpers::BufferSink;
using YSE::PATCHER::patcherImplementation;

namespace {
  void Wire(YSE::PATCHER::pObject& op, BufferSink& sink) {
    op.ConnectOutlet(sink.GetInlet(0), 0);
    sink.ConnectInlet(op.GetOutlet(0), 0);
  }

  bool AllNear(YSE::DSP::buffer* b, float expected) {
    if (b == nullptr) return false;
    for (unsigned int i = 0; i < b->getLength(); ++i)
      if (b->getPtr()[i] != doctest::Approx(expected).epsilon(1e-3)) return false;
    return true;
  }

  // A number into inlet 0 of a fresh T (with creation arguments `args`), one
  // block out, its first sample. The sink is declared first so it outlives
  // the object.
  template <class T> float Number(float in, const char* args = "") {
    BufferSink sink;
    T op;
    Wire(op, sink);
    if (args[0] != '\0') op.SetParams(args);
    op.GetInlet(0)->SetFloat(in, YSE::T_GUI);
    op.Calculate(YSE::T_DSP);
    REQUIRE(sink.received != nullptr);
    REQUIRE(sink.received->getLength() == YSE::STANDARD_BUFFERSIZE);
    REQUIRE(AllNear(sink.received, sink.received->getPtr()[0]));
    return sink.received->getPtr()[0];
  }

  // 100 samples a cycle at whatever rate the test binary runs.
  std::string HundredSampleFrequency() {
    return std::to_string(static_cast<float>(YSE::SAMPLERATE) / 100.f);
  }

  struct Family {
    const char* type;
    const char* name;
    int inputs;
  };

  const Family kFamily[] = {
      {YSE::OBJ::D_ABS, "~abs", 1},     {YSE::OBJ::D_SQRT, "~sqrt", 1},
      {YSE::OBJ::D_POW, "~pow", 2},     {YSE::OBJ::D_LOG, "~log", 2},
      {YSE::OBJ::D_ROUND, "~round", 2}, {YSE::OBJ::D_TRUNC, "~trunc", 1},
  };
} // namespace

TEST_SUITE("patcher") {

  TEST_CASE("signal math: registry, names and shape (#1050)") {
    YSE::patcher p;
    p.create(1);
    for (const Family& f : kFamily) {
      CAPTURE(f.name);
      CHECK(std::string(f.type) == f.name);
      YSE::pHandle* h = p.CreateObject(f.type);
      REQUIRE(h != nullptr);
      CHECK(std::string(h->Type()) == f.name);
      CHECK(h->GetInputs() == f.inputs);
      CHECK(h->GetOutputs() == 1);
      CHECK(h->OutputDataType(0) == YSE::OUT_TYPE::BUFFER);

      std::unique_ptr<YSE::PATCHER::pObject> obj(YSE::PATCHER::Register().Get(f.type));
      REQUIRE(obj != nullptr);
      CHECK(obj->GetCategory() == YSE::PATCHER::pCategory::MATH);
      CHECK(obj->IsDSPObject());
      CHECK(std::string(obj->Type()) == f.name);
    }
  }

  TEST_CASE("signal math: a number reaches each one-inlet function (#1050)") {
    using namespace YSE::PATCHER;
    CHECK(Number<dAbs>(-2.5f) == 2.5f);
    CHECK(Number<dAbs>(3.f) == 3.f);
    CHECK(Number<dSqrt>(9.f) == doctest::Approx(3.f).epsilon(1e-3));
    CHECK(Number<dSqrt>(-4.f) == 0.f); // no NaN
    CHECK(Number<dTrunc>(1.7f) == 1.f);
    CHECK(Number<dTrunc>(-1.7f) == -1.f);
  }

  TEST_CASE("signal math: ~pow takes the exponent left and the base right, as pow~ (#1050)") {
    using namespace YSE::PATCHER;
    // Creation argument 1 is the base: 2 to the power of the left inlet.
    CHECK(Number<dPow>(3.f, "2") == doctest::Approx(8.f));
    CHECK(Number<dPow>(0.5f, "16") == doctest::Approx(4.f));
    CHECK(Number<dPow>(3.f) == 0.f); // default base 0
    CHECK(Number<dPow>(3.f, "-2") == 0.f); // non-positive base: 0, no NaN
    CHECK(Number<dPow>(1000.f, "10") == 0.f); // overflow: 0, no infinity

    BufferSink sink;
    dPow op;
    Wire(op, sink);
    op.GetInlet(1)->SetInt(3, YSE::T_GUI);
    op.GetInlet(0)->SetInt(2, YSE::T_GUI);
    op.Calculate(YSE::T_DSP);
    CHECK(AllNear(sink.received, 9.f));

    // A signal exponent against the number base: 3 ** x, sample by sample.
    YSE::DSP::buffer x(YSE::STANDARD_BUFFERSIZE);
    for (unsigned int i = 0; i < x.getLength(); ++i)
      x.getPtr()[i] = static_cast<float>(i) / 32.f - 2.f;
    op.GetInlet(0)->SetBuffer(&x, YSE::T_GUI);
    op.Calculate(YSE::T_DSP);
    REQUIRE(sink.received != nullptr);
    for (unsigned int i = 0; i < sink.received->getLength(); ++i) {
      CAPTURE(i);
      CHECK(sink.received->getPtr()[i] == doctest::Approx(std::pow(3.f, x.getPtr()[i])));
    }

    // A signal base too: base ** x.
    YSE::DSP::buffer base(YSE::STANDARD_BUFFERSIZE);
    for (unsigned int i = 0; i < base.getLength(); ++i)
      base.getPtr()[i] = 0.5f + static_cast<float>(i) / 64.f;
    op.GetInlet(1)->SetBuffer(&base, YSE::T_GUI);
    op.GetInlet(0)->SetBuffer(&x, YSE::T_GUI);
    op.Calculate(YSE::T_DSP);
    for (unsigned int i = 0; i < sink.received->getLength(); ++i) {
      CAPTURE(i);
      CHECK(sink.received->getPtr()[i] ==
            doctest::Approx(std::pow(base.getPtr()[i], x.getPtr()[i])));
    }
  }

  TEST_CASE("signal math: ~log defaults to base e, takes a base, never NaN (#1050)") {
    using namespace YSE::PATCHER;
    CHECK(Number<dLog>(std::exp(2.f)) == doctest::Approx(2.f));
    CHECK(Number<dLog>(1000.f, "10") == doctest::Approx(3.f));
    CHECK(Number<dLog>(8.f, "2") == doctest::Approx(3.f));
    CHECK(Number<dLog>(0.f) == 0.f);
    CHECK(Number<dLog>(-5.f, "10") == 0.f);
    CHECK(Number<dLog>(std::exp(1.f), "1") == doctest::Approx(1.f)); // base 1: natural log

    BufferSink sink;
    dLog op;
    Wire(op, sink);
    op.GetInlet(0)->SetInt(81, YSE::T_GUI);
    op.GetInlet(1)->SetInt(3, YSE::T_GUI);
    op.Calculate(YSE::T_DSP);
    CHECK(AllNear(sink.received, 4.f));

    // Signals on both inlets win over the numbers.
    YSE::DSP::buffer in(YSE::STANDARD_BUFFERSIZE), base(YSE::STANDARD_BUFFERSIZE);
    for (unsigned int i = 0; i < in.getLength(); ++i) {
      in.getPtr()[i] = 1.f + static_cast<float>(i);
      base.getPtr()[i] = (i % 2 == 0) ? 2.f : 10.f;
    }
    op.GetInlet(1)->SetBuffer(&base, YSE::T_GUI);
    op.GetInlet(0)->SetBuffer(&in, YSE::T_GUI);
    op.Calculate(YSE::T_DSP);
    REQUIRE(sink.received != nullptr);
    for (unsigned int i = 0; i < sink.received->getLength(); ++i) {
      CAPTURE(i);
      const float expected = std::log(in.getPtr()[i]) / std::log(base.getPtr()[i]);
      CHECK(sink.received->getPtr()[i] == doctest::Approx(expected));
    }
  }

  TEST_CASE("signal math: ~round's step, 'nearest' message and parameter (#1050)") {
    using namespace YSE::PATCHER;
    CHECK(Number<dRound>(2.5f) == 3.f); // default step 1, nearest
    CHECK(Number<dRound>(-2.5f) == -3.f);
    CHECK(Number<dRound>(0.3f, "0.25") == 0.25f);
    CHECK(Number<dRound>(1.37f, "0") == 1.37f); // step 0: pass through
    CHECK(Number<dRound>(0.9f, "1 0") == 0.f); // nearest 0: round down
    CHECK(Number<dRound>(-0.1f, "1 0") == -1.f);
    CHECK(Number<dRound>(7.f, "3") == 6.f);

    BufferSink sink;
    dRound op;
    Wire(op, sink);
    op.GetInlet(0)->SetFloat(5.9f, YSE::T_GUI);
    op.GetInlet(1)->SetInt(2, YSE::T_GUI);
    op.Calculate(YSE::T_DSP);
    CHECK(AllNear(sink.received, 6.f));
    op.GetInlet(0)->SetMessage("nearest 0", YSE::T_GUI);
    op.Calculate(YSE::T_DSP);
    CHECK(AllNear(sink.received, 4.f));
    op.GetInlet(0)->SetMessage("nearest nonsense", YSE::T_GUI); // ignored
    op.Calculate(YSE::T_DSP);
    CHECK(AllNear(sink.received, 4.f));
    op.GetInlet(0)->SetMessage("nearest 1", YSE::T_GUI);
    op.Calculate(YSE::T_DSP);
    CHECK(AllNear(sink.received, 6.f));

    // A step signal wins over the number.
    YSE::DSP::buffer in(YSE::STANDARD_BUFFERSIZE), step(YSE::STANDARD_BUFFERSIZE);
    for (unsigned int i = 0; i < in.getLength(); ++i) {
      in.getPtr()[i] = static_cast<float>(i) * 0.37f - 20.f;
      step.getPtr()[i] = static_cast<float>(i % 4);
    }
    op.GetInlet(1)->SetBuffer(&step, YSE::T_GUI);
    op.GetInlet(0)->SetBuffer(&in, YSE::T_GUI);
    op.Calculate(YSE::T_DSP);
    REQUIRE(sink.received != nullptr);
    for (unsigned int i = 0; i < sink.received->getLength(); ++i) {
      CAPTURE(i);
      const float s = step.getPtr()[i];
      const float expected = s == 0.f ? in.getPtr()[i] : std::round(in.getPtr()[i] / s) * s;
      CHECK(sink.received->getPtr()[i] == doctest::Approx(expected));
    }
  }

  TEST_CASE("signal math: parameters survive a DumpJSON / ParseJSON round trip (#1050)") {
    std::string dump;
    {
      patcherImplementation src(3, nullptr);
      YSE::pHandle* pw = src.CreateObject(YSE::OBJ::D_POW, "2");
      YSE::pHandle* lg = src.CreateObject(YSE::OBJ::D_LOG, "10");
      YSE::pHandle* rd = src.CreateObject(YSE::OBJ::D_ROUND, "0.5 0");
      // Constant signals in: ~+ with an unconnected left inlet outputs its
      // argument.
      YSE::pHandle* three = src.CreateObject(YSE::OBJ::D_ADD, "3");
      YSE::pHandle* hundred = src.CreateObject(YSE::OBJ::D_ADD, "100");
      YSE::pHandle* nine = src.CreateObject(YSE::OBJ::D_ADD, "0.9");
      YSE::pHandle* dac = src.CreateObject(YSE::OBJ::D_DAC, "");
      src.Connect(three, 0, pw, 0);
      src.Connect(hundred, 0, lg, 0);
      src.Connect(nine, 0, rd, 0);
      src.Connect(pw, 0, dac, 0);
      src.Connect(lg, 0, dac, 1);
      src.Connect(rd, 0, dac, 2);
      dump = src.DumpJSON();
    }
    patcherImplementation dst(3, nullptr);
    dst.ParseJSON(dump);
    dst.Calculate(YSE::T_DSP);
    CHECK(dst.output[0].getPtr()[0] == doctest::Approx(8.f)); // base 2: 2 ** 3
    CHECK(dst.output[1].getPtr()[0] == doctest::Approx(2.f)); // base 10: log10(100)
    CHECK(dst.output[2].getPtr()[0] == 0.5f); // step 0.5, nearest 0: 0.9 down to 0.5
  }

  TEST_CASE("signal math: in a patch, a sine through ~abs ~sqrt ~pow ~log ~round ~trunc "
            "(#1050)") {
    patcherImplementation p(6, nullptr);
    YSE::pHandle* ramp = p.CreateObject(YSE::OBJ::D_PHASOR, HundredSampleFrequency());
    YSE::pHandle* cos = p.CreateObject(YSE::OBJ::D_COS, "");
    YSE::pHandle* gain = p.CreateObject(YSE::OBJ::D_MULTIPLY, "4");
    YSE::pHandle* abs = p.CreateObject(YSE::OBJ::D_ABS, "");
    YSE::pHandle* sqrt = p.CreateObject(YSE::OBJ::D_SQRT, "");
    YSE::pHandle* pow = p.CreateObject(YSE::OBJ::D_POW, "2");
    YSE::pHandle* log = p.CreateObject(YSE::OBJ::D_LOG, "2");
    YSE::pHandle* round = p.CreateObject(YSE::OBJ::D_ROUND, "0.5");
    YSE::pHandle* trunc = p.CreateObject(YSE::OBJ::D_TRUNC, "");
    YSE::pHandle* dac = p.CreateObject(YSE::OBJ::D_DAC, "");

    // c = 4 cos(2 pi phase); abs -> sqrt; 2 ** c; log2(c) (0 where c <= 0);
    // round to halves; trunc.
    p.Connect(ramp, 0, cos, 0);
    p.Connect(cos, 0, gain, 0);
    p.Connect(gain, 0, abs, 0);
    p.Connect(abs, 0, sqrt, 0);
    p.Connect(gain, 0, pow, 0);
    p.Connect(gain, 0, log, 0);
    p.Connect(gain, 0, round, 0);
    p.Connect(gain, 0, trunc, 0);
    p.Connect(abs, 0, dac, 0);
    p.Connect(sqrt, 0, dac, 1);
    p.Connect(pow, 0, dac, 2);
    p.Connect(log, 0, dac, 3);
    p.Connect(round, 0, dac, 4);
    p.Connect(trunc, 0, dac, 5);
    p.Calculate(YSE::T_DSP);

    const float pi = 3.14159265f;
    for (unsigned int i : {0u, 10u, 20u, 25u, 40u, 50u, 60u, 75u, 99u}) {
      CAPTURE(i);
      const float phase = static_cast<float>(i) / 100.f;
      const float c = 4.f * std::cos(2.f * pi * phase);
      const float out[6] = {p.output[0].getPtr()[i], p.output[1].getPtr()[i],
                            p.output[2].getPtr()[i], p.output[3].getPtr()[i],
                            p.output[4].getPtr()[i], p.output[5].getPtr()[i]};
      CHECK(out[0] == doctest::Approx(std::fabs(c)).epsilon(1e-3).scale(1.f));
      CHECK(out[1] == doctest::Approx(std::sqrt(std::fabs(c))).epsilon(1e-2).scale(1.f));
      CHECK(out[2] == doctest::Approx(std::pow(2.f, c)).epsilon(1e-2));
      if (c > 0.01f)
        CHECK(out[3] == doctest::Approx(std::log2(c)).epsilon(1e-2).scale(1.f));
      else if (c <= 0.f)
        CHECK(out[3] == 0.f);
      // Rounding is discontinuous: compare against the actual sample.
      const float sample = p.output[0].getPtr()[i] * (c < 0.f ? -1.f : 1.f);
      CHECK(out[4] == std::round(sample / 0.5f) * 0.5f);
      CHECK(out[5] == std::trunc(sample));
    }
    for (unsigned int ch = 0; ch < 6; ++ch)
      for (unsigned int i = 0; i < p.output[ch].getLength(); ++i)
        CHECK(std::isfinite(p.output[ch].getPtr()[i]));
  }

  TEST_CASE("signal math: numbers, messages and blocks on the audio thread allocate nothing "
            "(#1050)") {
    using namespace YSE::PATCHER;
    BufferSink sinks[6]; // declared first so they outlive the objects
    dAbs ab;
    dSqrt sq;
    dPow pw;
    dLog lg;
    dRound rd;
    dTrunc tr;
    pObject* all[] = {&ab, &sq, &pw, &lg, &rd, &tr};
    pObject* twoInlets[] = {&pw, &lg, &rd};
    for (unsigned int i = 0; i < 6; ++i) {
      Wire(*all[i], sinks[i]);
      all[i]->Calculate(YSE::T_DSP);
    }
    YSE::DSP::buffer in(YSE::STANDARD_BUFFERSIZE);
    in = 0.5f;
    {
      TestHelpers::ProbeScope probe;
      for (pObject* op : all) {
        op->GetInlet(0)->SetFloat(0.3f, YSE::T_DSP);
        op->GetInlet(0)->SetInt(2, YSE::T_DSP);
        op->Calculate(YSE::T_DSP);
        op->GetInlet(0)->SetBuffer(&in, YSE::T_DSP);
        op->Calculate(YSE::T_DSP);
      }
      rd.GetInlet(0)->SetMessage("nearest 0", YSE::T_DSP);
      for (pObject* op : twoInlets) {
        op->GetInlet(1)->SetFloat(0.7f, YSE::T_DSP);
        op->GetInlet(1)->SetInt(3, YSE::T_DSP);
        op->Calculate(YSE::T_DSP);
        op->GetInlet(1)->SetBuffer(&in, YSE::T_DSP);
        op->GetInlet(0)->SetBuffer(&in, YSE::T_DSP);
        op->Calculate(YSE::T_DSP);
      }
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
  }

} // TEST_SUITE("patcher")
