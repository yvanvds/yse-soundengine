// Tests for individual patcher node types (YseEngine/patcher/*/...).
// Covers: port metadata, parameter binding, float arithmetic (gMultiply),
// and DSP output generation (pSine, dSaw).
// No audio device required.

#include <doctest/doctest.h>
#include <algorithm>
#include <cmath>
#include <string>
#include "dsp/patcherInsert.hpp"
#include "headers/constants.hpp"
#include "headers/defines.hpp"
#include "patcher/patcher.hpp"
#include "patcher/pHandle.hpp"
#include "patcher/pObjectList.hpp"
#include "patcher/pObject.h"
#include "patcher/generatorObjects/pSine.h"
#include "patcher/generatorObjects/dSaw.h"
#include "patcher/math/gMultiply.h"
#include "dsp/buffer.hpp"
#include "patcher/sinks.hpp"

TEST_SUITE("patcher") {

  // ─── Port metadata ────────────────────────────────────────────────────────────

  TEST_CASE("pSine: type name, input/output count, and output type") {
    YSE::patcher p;
    p.create(2);
    YSE::pHandle* h = p.CreateObject(YSE::OBJ::D_SINE);
    REQUIRE(h != nullptr);
    CHECK(std::string(h->Type()) == "~sine");
    CHECK(h->GetInputs() == 1);
    CHECK(h->GetOutputs() == 1);
    CHECK(h->OutputDataType(0) == YSE::OUT_TYPE::BUFFER);
    CHECK(h->IsDSPInput(0) == true);
  }

  TEST_CASE("dSaw: type name, input/output count, and output type") {
    YSE::patcher p;
    p.create(2);
    YSE::pHandle* h = p.CreateObject(YSE::OBJ::D_SAW);
    REQUIRE(h != nullptr);
    CHECK(std::string(h->Type()) == "~saw");
    CHECK(h->GetInputs() == 1);
    CHECK(h->GetOutputs() == 1);
    CHECK(h->OutputDataType(0) == YSE::OUT_TYPE::BUFFER);
  }

  TEST_CASE("dAdd: type name, input/output count, and output type") {
    YSE::patcher p;
    p.create(2);
    YSE::pHandle* h = p.CreateObject(YSE::OBJ::D_ADD);
    REQUIRE(h != nullptr);
    CHECK(std::string(h->Type()) == "~+");
    CHECK(h->GetInputs() == 2);
    CHECK(h->GetOutputs() == 1);
    CHECK(h->OutputDataType(0) == YSE::OUT_TYPE::BUFFER);
  }

  TEST_CASE("gMultiply: type name, input/output count, and non-DSP output type") {
    YSE::patcher p;
    p.create(2);
    YSE::pHandle* h = p.CreateObject(YSE::OBJ::G_MULTIPLY);
    REQUIRE(h != nullptr);
    CHECK(std::string(h->Type()) == ".*");
    CHECK(h->GetInputs() == 2);
    CHECK(h->GetOutputs() == 1);
    CHECK(h->OutputDataType(0) == YSE::OUT_TYPE::FLOAT);
    CHECK(h->IsDSPInput(0) == false);
  }

  TEST_CASE("pLowpass: type name, input/output count, and output type") {
    YSE::patcher p;
    p.create(2);
    YSE::pHandle* h = p.CreateObject(YSE::OBJ::D_LOWPASS);
    REQUIRE(h != nullptr);
    CHECK(std::string(h->Type()) == "~lp");
    CHECK(h->GetInputs() == 2);
    CHECK(h->GetOutputs() == 1);
    CHECK(h->OutputDataType(0) == YSE::OUT_TYPE::BUFFER);
  }

  // ─── Parameter binding ────────────────────────────────────────────────────────

  TEST_CASE("pSine: construction with parameter string stores it") {
    YSE::patcher p;
    p.create(2);
    YSE::pHandle* h = p.CreateObject(YSE::OBJ::D_SINE, "880");
    REQUIRE(h != nullptr);
    CHECK(h->GetParams() == "880");
  }

  TEST_CASE("pHandle: SetParams updates the stored parameter string") {
    YSE::patcher p;
    p.create(2);
    YSE::pHandle* h = p.CreateObject(YSE::OBJ::D_SINE);
    REQUIRE(h != nullptr);
    h->SetParams("220");
    CHECK(h->GetParams() == "220");
  }

  // ─── Signal tests (direct pObject instantiation) ─────────────────────────────
  // These instantiate concrete node classes directly and connect a lightweight
  // local test-sink to observe output values, avoiding the need for a full
  // patcher::Calculate cycle or audio device initialisation.

  TEST_CASE("gMultiply: float arithmetic produces the correct product") {
    // Local sink: captures the float forwarded by gMultiply's outlet.
    struct FloatSink : TestHelpers::SinkBase {
      float received = 0.f;
      FloatSink() : SinkBase(false) {
        inputs.emplace_back(this, true, 0);
        inputs.back().RegisterFloat([this](float v, int, YSE::THREAD) { received = v; });
      }
      const char* Type() const override {
        return "test_float_sink";
      }
      void Calculate(YSE::THREAD) override {}
      void SetMessage(const std::string&, float) override {}
    };

    YSE::PATCHER::gMultiply mult;
    FloatSink sink;

    // Wire mult outlet 0 → sink inlet 0.
    mult.ConnectOutlet(sink.GetInlet(0), 0);
    sink.ConnectInlet(mult.GetOutlet(0), 0);

    // Inlet 1 is inactive: sets rightIn without triggering Calculate.
    mult.GetInlet(1)->SetFloat(4.0f, YSE::T_GUI);
    // Inlet 0 is active: sets leftIn and triggers Calculate → SendFloat(12.0).
    mult.GetInlet(0)->SetFloat(3.0f, YSE::T_GUI);

    CHECK(sink.received == doctest::Approx(12.0f));
  }

  TEST_CASE("pSine: produces non-silent DSP output after Calculate") {
    struct BufferSink : TestHelpers::SinkBase {
      YSE::DSP::buffer* received = nullptr;
      BufferSink() : SinkBase(false) {
        inputs.emplace_back(this, true, 0);
        inputs.back().RegisterBuffer(
            [this](YSE::DSP::buffer* b, int, YSE::THREAD) { received = b; });
      }
      const char* Type() const override {
        return "test_buffer_sink";
      }
      void Calculate(YSE::THREAD) override {}
      void SetMessage(const std::string&, float) override {}
    };

    YSE::PATCHER::pSine sine;
    BufferSink sink;

    sine.ConnectOutlet(sink.GetInlet(0), 0);
    sink.ConnectInlet(sine.GetOutlet(0), 0);

    sine.Calculate(YSE::T_DSP);

    REQUIRE(sink.received != nullptr);
    CHECK_FALSE(sink.received->isSilent());
  }

  TEST_CASE("dSaw: output samples are bipolar, within [-1, 1] (#955)") {
    struct BufferSink : TestHelpers::SinkBase {
      YSE::DSP::buffer* received = nullptr;
      BufferSink() : SinkBase(false) {
        inputs.emplace_back(this, true, 0);
        inputs.back().RegisterBuffer(
            [this](YSE::DSP::buffer* b, int, YSE::THREAD) { received = b; });
      }
      const char* Type() const override {
        return "test_buffer_sink";
      }
      void Calculate(YSE::THREAD) override {}
      void SetMessage(const std::string&, float) override {}
    };

    YSE::PATCHER::dSaw saw;
    BufferSink sink;

    saw.ConnectOutlet(sink.GetInlet(0), 0);
    sink.ConnectInlet(saw.GetOutlet(0), 0);

    saw.Calculate(YSE::T_DSP);

    REQUIRE(sink.received != nullptr);
    const float* ptr = sink.received->getPtr();
    unsigned int len = sink.received->getLength();
    // The ramp starts at phase 0, which a bipolar saw puts at -1.
    CHECK(ptr[0] == doctest::Approx(-1.0f));
    for (unsigned int i = 0; i < len; ++i) {
      CHECK(ptr[i] >= -1.0f);
      CHECK(ptr[i] <= 1.0f);
    }
  }

  TEST_CASE("~saw in a patch: renders a bipolar -1..1 sawtooth with no DC offset (#955)") {
    // The documented outlet range is -1.0 to 1.0. A 0..1 ramp (a phasor)
    // would carry a DC offset of 0.5 into the mix.
    YSE::patcher p;
    p.create(1);
    YSE::pHandle* saw = p.CreateObject("~saw", "375"); // ~35 periods over the 32 blocks below
    YSE::pHandle* dac = p.CreateObject("~dac");
    REQUIRE(saw != nullptr);
    REQUIRE(dac != nullptr);
    p.Connect(saw, 0, dac, 0);

    YSE::DSP::patcherInsert insert(p);
    MULTICHANNELBUFFER io(1);
    io[0].resize(YSE::STANDARD_BUFFERSIZE);

    float lo = 1.f;
    float hi = -1.f;
    double sum = 0.0;
    unsigned int count = 0;
    for (int block = 0; block < 32; ++block) {
      io[0] = 0.f;
      insert.process(io);
      const float* ptr = io[0].getPtr();
      for (unsigned int i = 0; i < io[0].getLength(); ++i) {
        lo = std::min(lo, ptr[i]);
        hi = std::max(hi, ptr[i]);
        sum += ptr[i];
        ++count;
      }
    }

    CHECK(lo >= -1.0f);
    CHECK(hi <= 1.0f);
    CHECK(lo < -0.9f);
    CHECK(hi > 0.9f);
    CHECK(std::fabs(sum / count) < 0.05);
  }

} // TEST_SUITE("patcher")
