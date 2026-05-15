// Tests for patcher math nodes: gAdd, gSubstract, gDivide, gCounter, gRandom,
// dClip, dDivide, dSubstract.
// No audio device required.

#include <doctest/doctest.h>
#include "patcher/patcher.hpp"
#include "patcher/pHandle.hpp"
#include "patcher/pObjectList.hpp"
#include "patcher/math/gAdd.h"
#include "patcher/math/gSubstract.h"
#include "patcher/math/gDivide.h"
#include "patcher/math/gCounter.h"
#include "patcher/math/gRandom.h"
#include "patcher/math/dClip.h"
#include "patcher/math/dDivide.h"
#include "patcher/math/dSubstract.h"
#include "dsp/buffer.hpp"
#include "patcher/sinks.hpp"

using TestHelpers::FloatSink;
using TestHelpers::IntSink;
using TestHelpers::BufferSink;

TEST_SUITE("patcher") {

// ─── gAdd ─────────────────────────────────────────────────────────────────────

TEST_CASE("gAdd: type name, input/output count, and output type") {
    YSE::patcher p;
    p.create(2);
    YSE::pHandle* h = p.CreateObject(YSE::OBJ::G_ADD);
    REQUIRE(h != nullptr);
    CHECK(std::string(h->Type()) == ".+");
    CHECK(h->GetInputs()  == 2);
    CHECK(h->GetOutputs() == 1);
    CHECK(h->OutputDataType(0) == YSE::OUT_TYPE::FLOAT);
}

TEST_CASE("gAdd: float addition produces correct sum") {
    YSE::PATCHER::gAdd add;
    FloatSink sink;
    add.ConnectOutlet(sink.GetInlet(0), 0);
    sink.ConnectInlet(add.GetOutlet(0), 0);

    add.GetInlet(1)->SetFloat(4.0f, YSE::T_GUI);
    add.GetInlet(0)->SetFloat(3.0f, YSE::T_GUI);

    CHECK(sink.received == doctest::Approx(7.0f));
}

TEST_CASE("gAdd: int inputs produce correct sum") {
    YSE::PATCHER::gAdd add;
    FloatSink sink;
    add.ConnectOutlet(sink.GetInlet(0), 0);
    sink.ConnectInlet(add.GetOutlet(0), 0);

    add.GetInlet(1)->SetInt(3, YSE::T_GUI);
    add.GetInlet(0)->SetInt(5, YSE::T_GUI);

    CHECK(sink.received == doctest::Approx(8.0f));
}

// ─── gSubstract ───────────────────────────────────────────────────────────────

TEST_CASE("gSubstract: type name, input/output count, and output type") {
    YSE::patcher p;
    p.create(2);
    YSE::pHandle* h = p.CreateObject(YSE::OBJ::G_SUBSTRACT);
    REQUIRE(h != nullptr);
    CHECK(std::string(h->Type()) == ".-");
    CHECK(h->GetInputs()  == 2);
    CHECK(h->GetOutputs() == 1);
    CHECK(h->OutputDataType(0) == YSE::OUT_TYPE::FLOAT);
}

TEST_CASE("gSubstract: float subtraction produces correct difference") {
    YSE::PATCHER::gSubstract sub;
    FloatSink sink;
    sub.ConnectOutlet(sink.GetInlet(0), 0);
    sink.ConnectInlet(sub.GetOutlet(0), 0);

    sub.GetInlet(1)->SetFloat(3.0f, YSE::T_GUI);
    sub.GetInlet(0)->SetFloat(10.0f, YSE::T_GUI);

    CHECK(sink.received == doctest::Approx(7.0f));
}

TEST_CASE("gSubstract: result can be negative") {
    YSE::PATCHER::gSubstract sub;
    FloatSink sink;
    sub.ConnectOutlet(sink.GetInlet(0), 0);
    sink.ConnectInlet(sub.GetOutlet(0), 0);

    sub.GetInlet(1)->SetFloat(5.0f, YSE::T_GUI);
    sub.GetInlet(0)->SetFloat(0.0f, YSE::T_GUI);

    CHECK(sink.received == doctest::Approx(-5.0f));
}

TEST_CASE("gSubstract: int inputs produce correct difference") {
    YSE::PATCHER::gSubstract sub;
    FloatSink sink;
    sub.ConnectOutlet(sink.GetInlet(0), 0);
    sink.ConnectInlet(sub.GetOutlet(0), 0);

    sub.GetInlet(1)->SetInt(2, YSE::T_GUI);
    sub.GetInlet(0)->SetInt(9, YSE::T_GUI);

    CHECK(sink.received == doctest::Approx(7.0f));
}

// ─── gDivide ──────────────────────────────────────────────────────────────────

TEST_CASE("gDivide: type name, input/output count, and output type") {
    YSE::patcher p;
    p.create(2);
    YSE::pHandle* h = p.CreateObject(YSE::OBJ::G_DIVIDE);
    REQUIRE(h != nullptr);
    CHECK(std::string(h->Type()) == "./");
    CHECK(h->GetInputs()  == 2);
    CHECK(h->GetOutputs() == 1);
    CHECK(h->OutputDataType(0) == YSE::OUT_TYPE::FLOAT);
}

TEST_CASE("gDivide: float division produces correct quotient") {
    YSE::PATCHER::gDivide div;
    FloatSink sink;
    div.ConnectOutlet(sink.GetInlet(0), 0);
    sink.ConnectInlet(div.GetOutlet(0), 0);

    div.GetInlet(1)->SetFloat(4.0f, YSE::T_GUI);
    div.GetInlet(0)->SetFloat(12.0f, YSE::T_GUI);

    CHECK(sink.received == doctest::Approx(3.0f));
}

TEST_CASE("gDivide: divide by zero yields zero") {
    YSE::PATCHER::gDivide div;
    FloatSink sink;
    div.ConnectOutlet(sink.GetInlet(0), 0);
    sink.ConnectInlet(div.GetOutlet(0), 0);

    div.GetInlet(1)->SetFloat(0.0f, YSE::T_GUI);
    div.GetInlet(0)->SetFloat(10.0f, YSE::T_GUI);

    CHECK(sink.received == doctest::Approx(0.0f));
}

TEST_CASE("gDivide: int inputs produce correct quotient") {
    YSE::PATCHER::gDivide div;
    FloatSink sink;
    div.ConnectOutlet(sink.GetInlet(0), 0);
    sink.ConnectInlet(div.GetOutlet(0), 0);

    div.GetInlet(1)->SetInt(3, YSE::T_GUI);
    div.GetInlet(0)->SetInt(9, YSE::T_GUI);

    CHECK(sink.received == doctest::Approx(3.0f));
}

// ─── gCounter ─────────────────────────────────────────────────────────────────

TEST_CASE("gCounter: type name, input/output count, and output type") {
    YSE::patcher p;
    p.create(2);
    YSE::pHandle* h = p.CreateObject(YSE::OBJ::G_COUNTER);
    REQUIRE(h != nullptr);
    CHECK(std::string(h->Type()) == ".counter");
    CHECK(h->GetInputs()  == 2);
    CHECK(h->GetOutputs() == 1);
    CHECK(h->OutputDataType(0) == YSE::OUT_TYPE::INT);
}

TEST_CASE("gCounter: bang increments and sends the counter") {
    YSE::PATCHER::gCounter counter;
    IntSink sink;
    counter.ConnectOutlet(sink.GetInlet(0), 0);
    sink.ConnectInlet(counter.GetOutlet(0), 0);

    counter.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK(sink.received == 1);
    counter.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK(sink.received == 2);
}

TEST_CASE("gCounter: SetInt on inlet 0 resets to value and sends it") {
    YSE::PATCHER::gCounter counter;
    IntSink sink;
    counter.ConnectOutlet(sink.GetInlet(0), 0);
    sink.ConnectInlet(counter.GetOutlet(0), 0);

    counter.GetInlet(0)->SetInt(10, YSE::T_GUI);
    CHECK(sink.received == 10);
}

TEST_CASE("gCounter: list 'reset' restores startValue") {
    YSE::PATCHER::gCounter counter;
    IntSink sink;
    counter.ConnectOutlet(sink.GetInlet(0), 0);
    sink.ConnectInlet(counter.GetOutlet(0), 0);

    counter.GetInlet(0)->SetInt(5, YSE::T_GUI);   // startValue=5, currentValue=5, sends 5
    counter.GetInlet(0)->SetBang(YSE::T_GUI);       // currentValue=6, sends 6
    CHECK(sink.received == 6);
    counter.GetInlet(0)->SetList("reset", YSE::T_GUI);  // currentValue back to 5, no send
    counter.GetInlet(0)->SetBang(YSE::T_GUI);            // currentValue=6, sends 6
    CHECK(sink.received == 6);
}

// ─── gRandom ──────────────────────────────────────────────────────────────────

TEST_CASE("gRandom: type name, input/output count, and output type") {
    YSE::patcher p;
    p.create(2);
    YSE::pHandle* h = p.CreateObject(YSE::OBJ::G_RANDOM);
    REQUIRE(h != nullptr);
    CHECK(std::string(h->Type()) == ".random");
    CHECK(h->GetInputs()  == 2);
    CHECK(h->GetOutputs() == 1);
    CHECK(h->OutputDataType(0) == YSE::OUT_TYPE::INT);
}

TEST_CASE("gRandom: bang produces values within int range") {
    YSE::PATCHER::gRandom rng;
    IntSink sink;
    rng.ConnectOutlet(sink.GetInlet(0), 0);
    sink.ConnectInlet(rng.GetOutlet(0), 0);

    rng.GetInlet(1)->SetInt(10, YSE::T_GUI);
    for (int i = 0; i < 20; ++i) {
        rng.GetInlet(0)->SetBang(YSE::T_GUI);
        CHECK(sink.received >= 0);
        CHECK(sink.received < 10);
    }
}

TEST_CASE("gRandom: float range input is truncated to int") {
    YSE::PATCHER::gRandom rng;
    IntSink sink;
    rng.ConnectOutlet(sink.GetInlet(0), 0);
    sink.ConnectInlet(rng.GetOutlet(0), 0);

    rng.GetInlet(1)->SetFloat(5.9f, YSE::T_GUI);
    for (int i = 0; i < 10; ++i) {
        rng.GetInlet(0)->SetBang(YSE::T_GUI);
        CHECK(sink.received >= 0);
        CHECK(sink.received < 5);
    }
}

// ─── dClip ───────────────────────────────────────────────────────────────────
// dClip is not in the patcher registry, so these tests use direct instantiation.

TEST_CASE("dClip: type name, input/output count, and output type") {
    YSE::PATCHER::dClip clip;
    CHECK(std::string(clip.Type()) == "~clip");
    CHECK(clip.NumInputs()  == 3);
    CHECK(clip.NumOutputs() == 1);
    CHECK(clip.GetOutputType(0) == YSE::OUT_TYPE::BUFFER);
}

TEST_CASE("dClip: samples outside [-0.5, 0.5] are clamped") {
    YSE::PATCHER::dClip clip;
    BufferSink sink;
    clip.ConnectOutlet(sink.GetInlet(0), 0);
    sink.ConnectInlet(clip.GetOutlet(0), 0);

    clip.GetInlet(1)->SetFloat(-0.5f, YSE::T_GUI);
    clip.GetInlet(2)->SetFloat( 0.5f, YSE::T_GUI);

    YSE::DSP::buffer in(128);
    float* p = in.getPtr();
    for (unsigned i = 0; i < 128; ++i)
        p[i] = static_cast<float>(i) * 0.02f - 1.0f;

    clip.GetInlet(0)->SetBuffer(&in, YSE::T_GUI);
    clip.Calculate(YSE::T_DSP);

    REQUIRE(sink.received != nullptr);
    const float* out = sink.received->getPtr();
    for (unsigned i = 0; i < 128; ++i) {
        CHECK(out[i] >= -0.5f);
        CHECK(out[i] <=  0.5f);
    }
}

TEST_CASE("dClip: samples within range are passed through unchanged") {
    YSE::PATCHER::dClip clip;
    BufferSink sink;
    clip.ConnectOutlet(sink.GetInlet(0), 0);
    sink.ConnectInlet(clip.GetOutlet(0), 0);

    clip.GetInlet(1)->SetFloat(-1.0f, YSE::T_GUI);
    clip.GetInlet(2)->SetFloat( 1.0f, YSE::T_GUI);

    YSE::DSP::buffer in(128);
    in = 0.5f;

    clip.GetInlet(0)->SetBuffer(&in, YSE::T_GUI);
    clip.Calculate(YSE::T_DSP);

    REQUIRE(sink.received != nullptr);
    const float* out = sink.received->getPtr();
    for (unsigned i = 0; i < 128; ++i)
        CHECK(out[i] == doctest::Approx(0.5f));
}

// ─── dSubstract ───────────────────────────────────────────────────────────────

TEST_CASE("dSubstract: type name, input/output count, and output type") {
    YSE::patcher p;
    p.create(2);
    YSE::pHandle* h = p.CreateObject(YSE::OBJ::D_SUBSTRACT);
    REQUIRE(h != nullptr);
    CHECK(std::string(h->Type()) == "~-");
    CHECK(h->GetInputs()  == 2);
    CHECK(h->GetOutputs() == 1);
    CHECK(h->OutputDataType(0) == YSE::OUT_TYPE::BUFFER);
}

TEST_CASE("dSubstract: null left buffer produces no output") {
    YSE::PATCHER::dSubstract sub;
    BufferSink sink;
    sub.ConnectOutlet(sink.GetInlet(0), 0);
    sink.ConnectInlet(sub.GetOutlet(0), 0);

    sub.Calculate(YSE::T_DSP);

    CHECK(sink.received == nullptr);
}

TEST_CASE("dSubstract: subtracts scalar from all buffer samples") {
    YSE::PATCHER::dSubstract sub;
    BufferSink sink;
    sub.ConnectOutlet(sink.GetInlet(0), 0);
    sink.ConnectInlet(sub.GetOutlet(0), 0);

    sub.GetInlet(1)->SetFloat(1.0f, YSE::T_GUI);

    YSE::DSP::buffer in(128);
    in = 3.0f;

    sub.GetInlet(0)->SetBuffer(&in, YSE::T_GUI);
    sub.Calculate(YSE::T_DSP);

    REQUIRE(sink.received != nullptr);
    const float* out = sink.received->getPtr();
    for (unsigned i = 0; i < 128; ++i)
        CHECK(out[i] == doctest::Approx(2.0f));
}

TEST_CASE("dSubstract: subtracts buffer from buffer sample-by-sample") {
    YSE::PATCHER::dSubstract sub;
    BufferSink sink;
    sub.ConnectOutlet(sink.GetInlet(0), 0);
    sink.ConnectInlet(sub.GetOutlet(0), 0);

    YSE::DSP::buffer left(128), right(128);
    left  = 5.0f;
    right = 2.0f;

    sub.GetInlet(1)->SetBuffer(&right, YSE::T_GUI);
    sub.GetInlet(0)->SetBuffer(&left,  YSE::T_GUI);
    sub.Calculate(YSE::T_DSP);

    REQUIRE(sink.received != nullptr);
    const float* out = sink.received->getPtr();
    for (unsigned i = 0; i < 128; ++i)
        CHECK(out[i] == doctest::Approx(3.0f));
}

// ─── dDivide ──────────────────────────────────────────────────────────────────

TEST_CASE("dDivide: type name, input/output count, and output type") {
    YSE::patcher p;
    p.create(2);
    YSE::pHandle* h = p.CreateObject(YSE::OBJ::D_DIVIDE);
    REQUIRE(h != nullptr);
    CHECK(std::string(h->Type()) == "~/");
    CHECK(h->GetInputs()  == 2);
    CHECK(h->GetOutputs() == 1);
    CHECK(h->OutputDataType(0) == YSE::OUT_TYPE::BUFFER);
}

TEST_CASE("dDivide: null left buffer produces no output") {
    YSE::PATCHER::dDivide div;
    BufferSink sink;
    div.ConnectOutlet(sink.GetInlet(0), 0);
    sink.ConnectInlet(div.GetOutlet(0), 0);

    div.Calculate(YSE::T_DSP);

    CHECK(sink.received == nullptr);
}

TEST_CASE("dDivide: divides buffer by scalar") {
    YSE::PATCHER::dDivide div;
    BufferSink sink;
    div.ConnectOutlet(sink.GetInlet(0), 0);
    sink.ConnectInlet(div.GetOutlet(0), 0);

    div.GetInlet(1)->SetFloat(2.0f, YSE::T_GUI);

    YSE::DSP::buffer in(128);
    in = 6.0f;

    div.GetInlet(0)->SetBuffer(&in, YSE::T_GUI);
    div.Calculate(YSE::T_DSP);

    REQUIRE(sink.received != nullptr);
    const float* out = sink.received->getPtr();
    for (unsigned i = 0; i < 128; ++i)
        CHECK(out[i] == doctest::Approx(3.0f));
}

TEST_CASE("dDivide: divides buffer by buffer sample-by-sample") {
    YSE::PATCHER::dDivide div;
    BufferSink sink;
    div.ConnectOutlet(sink.GetInlet(0), 0);
    sink.ConnectInlet(div.GetOutlet(0), 0);

    YSE::DSP::buffer left(128), right(128);
    left  = 9.0f;
    right = 3.0f;

    div.GetInlet(1)->SetBuffer(&right, YSE::T_GUI);
    div.GetInlet(0)->SetBuffer(&left,  YSE::T_GUI);
    div.Calculate(YSE::T_DSP);

    REQUIRE(sink.received != nullptr);
    const float* out = sink.received->getPtr();
    for (unsigned i = 0; i < 128; ++i)
        CHECK(out[i] == doctest::Approx(3.0f));
}

} // TEST_SUITE("patcher")
