// Tests for patcher math wrappers: pFrequencyToMidi, pMidiToFrequency,
// dMultiply.  No audio device required.

#include <doctest/doctest.h>
#include <cmath>
#include <string>
#include "patcher/patcher.hpp"
#include "patcher/pHandle.hpp"
#include "patcher/pObjectList.hpp"
#include "patcher/math/pFrequencyToMidi.h"
#include "patcher/math/pMidiToFrequency.h"
#include "patcher/math/dMultiply.h"
#include "dsp/buffer.hpp"
#include "dsp/math.hpp"
#include "patcher/sinks.hpp"

using TestHelpers::BufferSink;
using TestHelpers::FloatSink;
using TestHelpers::Wire;

TEST_SUITE("patcher") {

  // ─── pFrequencyToMidi ─────────────────────────────────────────────────────────

  TEST_CASE("pFrequencyToMidi: type name, input/output count, and output type") {
    YSE::patcher p;
    p.create(2);
    YSE::pHandle* h = p.CreateObject(YSE::OBJ::FREQUENCYTOMIDI);
    REQUIRE(h != nullptr);
    CHECK(std::string(h->Type()) == ".ftom");
    CHECK(h->GetInputs() == 1);
    CHECK(h->GetOutputs() == 1);
    CHECK(h->OutputDataType(0) == YSE::OUT_TYPE::FLOAT);
  }

  TEST_CASE("pFrequencyToMidi: 440 Hz maps to MIDI note 69") {
    YSE::PATCHER::pFrequencyToMidi node;
    FloatSink sink;
    Wire(node, 0, sink);

    node.GetInlet(0)->SetFloat(440.0f, YSE::T_GUI);

    CHECK(sink.gotFloat);
    CHECK(sink.received == doctest::Approx(69.0f).epsilon(1e-3f));
  }

  TEST_CASE("pFrequencyToMidi: int input is converted to float") {
    YSE::PATCHER::pFrequencyToMidi node;
    FloatSink sink;
    Wire(node, 0, sink);

    node.GetInlet(0)->SetInt(440, YSE::T_GUI);

    CHECK(sink.gotFloat);
    CHECK(sink.received == doctest::Approx(69.0f).epsilon(1e-3f));
  }

  TEST_CASE("pFrequencyToMidi: 880 Hz maps roughly one octave above 440 Hz") {
    YSE::PATCHER::pFrequencyToMidi a, b;
    FloatSink sa, sb;
    Wire(a, 0, sa);
    Wire(b, 0, sb);

    a.GetInlet(0)->SetFloat(440.0f, YSE::T_GUI);
    b.GetInlet(0)->SetFloat(880.0f, YSE::T_GUI);

    CHECK(sb.received - sa.received == doctest::Approx(12.0f).epsilon(1e-3f));
  }

  // ─── pMidiToFrequency ─────────────────────────────────────────────────────────

  TEST_CASE("pMidiToFrequency: type name, input/output count, and output type") {
    YSE::patcher p;
    p.create(2);
    YSE::pHandle* h = p.CreateObject(YSE::OBJ::MIDITOFREQUENCY);
    REQUIRE(h != nullptr);
    CHECK(std::string(h->Type()) == ".mtof");
    CHECK(h->GetInputs() == 1);
    CHECK(h->GetOutputs() == 1);
    CHECK(h->OutputDataType(0) == YSE::OUT_TYPE::FLOAT);
  }

  TEST_CASE("pMidiToFrequency: MIDI note 69 maps to 440 Hz") {
    YSE::PATCHER::pMidiToFrequency node;
    FloatSink sink;
    Wire(node, 0, sink);

    node.GetInlet(0)->SetFloat(69.0f, YSE::T_GUI);

    CHECK(sink.gotFloat);
    CHECK(sink.received == doctest::Approx(440.0f).epsilon(1e-3f));
  }

  TEST_CASE("pMidiToFrequency: int note input is converted to float") {
    YSE::PATCHER::pMidiToFrequency node;
    FloatSink sink;
    Wire(node, 0, sink);

    node.GetInlet(0)->SetInt(69, YSE::T_GUI);

    CHECK(sink.gotFloat);
    CHECK(sink.received == doctest::Approx(440.0f).epsilon(1e-3f));
  }

  TEST_CASE("pMidiToFrequency followed by pFrequencyToMidi is identity") {
    YSE::PATCHER::pMidiToFrequency mtof;
    YSE::PATCHER::pFrequencyToMidi ftom;
    FloatSink sink;

    Wire(mtof, 0, ftom);

    Wire(ftom, 0, sink);

    mtof.GetInlet(0)->SetFloat(60.0f, YSE::T_GUI);

    CHECK(sink.received == doctest::Approx(60.0f).epsilon(1e-3f));
  }

  // ─── dMultiply ────────────────────────────────────────────────────────────────

  TEST_CASE("dMultiply: type name, input/output count, and output type") {
    YSE::patcher p;
    p.create(2);
    YSE::pHandle* h = p.CreateObject(YSE::OBJ::D_MULTIPLY);
    REQUIRE(h != nullptr);
    CHECK(std::string(h->Type()) == "~*");
    CHECK(h->GetInputs() == 2);
    CHECK(h->GetOutputs() == 1);
    CHECK(h->OutputDataType(0) == YSE::OUT_TYPE::BUFFER);
  }

  TEST_CASE("dMultiply: null left buffer produces no output") {
    YSE::PATCHER::dMultiply mul;
    BufferSink sink;
    Wire(mul, 0, sink);

    mul.Calculate(YSE::T_DSP);

    CHECK(sink.received == nullptr);
  }

  TEST_CASE("dMultiply: multiplies buffer by scalar") {
    YSE::PATCHER::dMultiply mul;
    BufferSink sink;
    Wire(mul, 0, sink);

    mul.GetInlet(1)->SetFloat(2.5f, YSE::T_GUI);

    YSE::DSP::buffer in(128);
    in = 4.0f;

    mul.GetInlet(0)->SetBuffer(&in, YSE::T_GUI);
    mul.Calculate(YSE::T_DSP);

    REQUIRE(sink.received != nullptr);
    const float* out = sink.received->getPtr();
    for (unsigned i = 0; i < 128; ++i)
      CHECK(out[i] == doctest::Approx(10.0f));
  }

  TEST_CASE("dMultiply: multiplies buffer by buffer sample-by-sample") {
    YSE::PATCHER::dMultiply mul;
    BufferSink sink;
    Wire(mul, 0, sink);

    YSE::DSP::buffer left(128), right(128);
    left = 3.0f;
    right = 2.0f;

    mul.GetInlet(1)->SetBuffer(&right, YSE::T_GUI);
    mul.GetInlet(0)->SetBuffer(&left, YSE::T_GUI);
    mul.Calculate(YSE::T_DSP);

    REQUIRE(sink.received != nullptr);
    const float* out = sink.received->getPtr();
    for (unsigned i = 0; i < 128; ++i)
      CHECK(out[i] == doctest::Approx(6.0f));
  }

  TEST_CASE("dMultiply: scalar of 1 leaves the buffer unchanged") {
    YSE::PATCHER::dMultiply mul;
    BufferSink sink;
    Wire(mul, 0, sink);

    YSE::DSP::buffer in(128);
    float* p = in.getPtr();
    for (unsigned i = 0; i < 128; ++i)
      p[i] = 0.1f * static_cast<float>(i);

    // default right-side scalar should already be 1, but set it explicitly
    mul.GetInlet(1)->SetFloat(1.0f, YSE::T_GUI);
    mul.GetInlet(0)->SetBuffer(&in, YSE::T_GUI);
    mul.Calculate(YSE::T_DSP);

    REQUIRE(sink.received != nullptr);
    const float* out = sink.received->getPtr();
    for (unsigned i = 0; i < 128; ++i)
      CHECK(out[i] == doctest::Approx(0.1f * static_cast<float>(i)));
  }

  TEST_CASE("dMultiply: ResetDSP restores null-buffer early-return behaviour") {
    YSE::PATCHER::dMultiply mul;
    BufferSink sink;
    Wire(mul, 0, sink);

    YSE::DSP::buffer in(128);
    in = 4.0f;
    mul.GetInlet(1)->SetFloat(2.0f, YSE::T_GUI);
    mul.GetInlet(0)->SetBuffer(&in, YSE::T_GUI);
    mul.Calculate(YSE::T_DSP);
    REQUIRE(sink.received != nullptr);

    sink.received = nullptr;
    mul.ResetDSP();
    mul.Calculate(YSE::T_DSP);
    CHECK(sink.received == nullptr);
  }

} // TEST_SUITE("patcher")
