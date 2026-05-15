// Tests for patcher DSP/generic objects: dNoise, pLine, pBandpass, pHighpass,
// dVcf, pDac.  No audio device required.
//
// Note: dVcf::Calculate dereferences a null DSP::buffer pointer
// (tracked in issue #30). Until that bug is fixed, this file only exercises
// dVcf's metadata and its null-input early-return paths.

#include <doctest/doctest.h>
#include <cmath>
#include <string>
#include "patcher/patcher.hpp"
#include "patcher/pHandle.hpp"
#include "patcher/pObjectList.hpp"
#include "patcher/generatorObjects/dNoise.h"
#include "patcher/genericObjects/pLine.h"
#include "patcher/genericObjects/pDac.h"
#include "patcher/filters/pBandpass.h"
#include "patcher/filters/pHighpass.h"
#include "patcher/filters/dVcf.h"
#include "dsp/buffer.hpp"
#include "support/audio_helpers.hpp"
#include "patcher/sinks.hpp"

using TestHelpers::BufferSink;

static constexpr float kPi = 3.14159265358979323846f;

namespace {

inline void fillSine(YSE::DSP::buffer & buf, float freq, float sr = 44100.0f) {
    float* p = buf.getPtr();
    for (unsigned i = 0; i < buf.getLength(); ++i)
        p[i] = std::sin(2.0f * kPi * freq * static_cast<float>(i) / sr);
}

inline float maxAbs(YSE::DSP::buffer & b) {
    float* p = b.getPtr();
    float m = 0.0f;
    for (unsigned i = 0; i < b.getLength(); ++i) {
        float a = std::abs(p[i]);
        if (a > m) m = a;
    }
    return m;
}

} // anonymous namespace

TEST_SUITE("patcher") {

// ─── dNoise ───────────────────────────────────────────────────────────────────

TEST_CASE("dNoise: type name, input/output count, and output type") {
    YSE::patcher p;
    p.create(2);
    YSE::pHandle* h = p.CreateObject(YSE::OBJ::D_NOISE);
    REQUIRE(h != nullptr);
    CHECK(std::string(h->Type()) == "~noise");
    CHECK(h->GetInputs()  == 0);
    CHECK(h->GetOutputs() == 1);
    CHECK(h->OutputDataType(0) == YSE::OUT_TYPE::BUFFER);
}

TEST_CASE("dNoise: emits a non-silent, bounded buffer") {
    YSE::PATCHER::dNoise node;
    BufferSink sink;
    node.ConnectOutlet(sink.GetInlet(0), 0);
    sink.ConnectInlet(node.GetOutlet(0), 0);

    node.Calculate(YSE::T_DSP);
    REQUIRE(sink.received != nullptr);

    float peak = maxAbs(*sink.received);
    CHECK(peak > 0.0f);
    CHECK(peak <= 1.5f);
    CHECK_FALSE(sink.received->isSilent());
}

TEST_CASE("dNoise: successive calls produce different samples") {
    YSE::PATCHER::dNoise node;
    BufferSink sink;
    node.ConnectOutlet(sink.GetInlet(0), 0);
    sink.ConnectInlet(node.GetOutlet(0), 0);

    node.Calculate(YSE::T_DSP);
    REQUIRE(sink.received != nullptr);
    YSE::DSP::buffer first(*sink.received);

    node.Calculate(YSE::T_DSP);
    REQUIRE(sink.received != nullptr);

    // At least one sample should differ between calls.
    bool anyDifferent = false;
    float* a = first.getPtr();
    float* b = sink.received->getPtr();
    unsigned n = first.getLength();
    for (unsigned i = 0; i < n; ++i) {
        if (a[i] != b[i]) { anyDifferent = true; break; }
    }
    CHECK(anyDifferent);
}

// ─── pLine ────────────────────────────────────────────────────────────────────

TEST_CASE("pLine: type name, input/output count, and output type") {
    YSE::patcher p;
    p.create(2);
    YSE::pHandle* h = p.CreateObject(YSE::OBJ::D_LINE);
    REQUIRE(h != nullptr);
    CHECK(std::string(h->Type()) == "~line");
    CHECK(h->GetInputs()  == 2);
    CHECK(h->GetOutputs() == 1);
    CHECK(h->OutputDataType(0) == YSE::OUT_TYPE::BUFFER);
}

TEST_CASE("pLine: with time=0 reaches target immediately") {
    YSE::PATCHER::pLine line;
    BufferSink sink;
    line.ConnectOutlet(sink.GetInlet(0), 0);
    sink.ConnectInlet(line.GetOutlet(0), 0);

    line.GetInlet(1)->SetFloat(0.0f, YSE::T_GUI);  // time
    line.GetInlet(0)->SetFloat(0.75f, YSE::T_GUI); // target
    line.Calculate(YSE::T_DSP);

    REQUIRE(sink.received != nullptr);
    // last sample should equal target since time was 0
    float last = sink.received->getPtr()[sink.received->getLength() - 1];
    CHECK(last == doctest::Approx(0.75f));
}

TEST_CASE("pLine: ramps toward target over a finite time") {
    YSE::PATCHER::pLine line;
    BufferSink sink;
    line.ConnectOutlet(sink.GetInlet(0), 0);
    sink.ConnectInlet(line.GetOutlet(0), 0);

    // Take a long time so the first frame should still be well below target.
    line.GetInlet(1)->SetFloat(1000.0f, YSE::T_GUI);
    line.GetInlet(0)->SetFloat(1.0f, YSE::T_GUI);

    line.Calculate(YSE::T_DSP);
    REQUIRE(sink.received != nullptr);
    float firstFrameLast = sink.received->getPtr()[sink.received->getLength() - 1];
    CHECK(firstFrameLast < 1.0f);
    CHECK(firstFrameLast >= 0.0f);

    // After enough iterations, ramp should converge close to target.
    for (int i = 0; i < 5000; ++i) line.Calculate(YSE::T_DSP);
    float settled = sink.received->getPtr()[sink.received->getLength() - 1];
    CHECK(settled == doctest::Approx(1.0f).epsilon(1e-3f));
}

TEST_CASE("pLine: 'stop' message halts the ramp at the current value") {
    YSE::PATCHER::pLine line;
    BufferSink sink;
    line.ConnectOutlet(sink.GetInlet(0), 0);
    sink.ConnectInlet(line.GetOutlet(0), 0);

    line.GetInlet(1)->SetFloat(1000.0f, YSE::T_GUI);
    line.GetInlet(0)->SetFloat(1.0f, YSE::T_GUI);

    // Run a few iterations so the ramp has advanced but isn't done.
    for (int i = 0; i < 4; ++i) line.Calculate(YSE::T_DSP);
    REQUIRE(sink.received != nullptr);
    float midValue = sink.received->getPtr()[sink.received->getLength() - 1];
    REQUIRE(midValue < 1.0f);

    line.SetMessage("stop", 0.f);
    line.Calculate(YSE::T_DSP);
    line.Calculate(YSE::T_DSP);

    float afterStop = sink.received->getPtr()[sink.received->getLength() - 1];
    CHECK(afterStop < 1.0f);  // should not have advanced to target
}

// ─── pBandpass ────────────────────────────────────────────────────────────────

TEST_CASE("pBandpass: type name, input/output count, and output type") {
    YSE::patcher p;
    p.create(2);
    YSE::pHandle* h = p.CreateObject(YSE::OBJ::D_BANDPASS);
    REQUIRE(h != nullptr);
    CHECK(std::string(h->Type()) == "~bp");
    CHECK(h->GetInputs()  == 3);
    CHECK(h->GetOutputs() == 1);
    CHECK(h->OutputDataType(0) == YSE::OUT_TYPE::BUFFER);
}

TEST_CASE("pBandpass: null input buffer produces no output") {
    YSE::PATCHER::pBandpass bp;
    BufferSink sink;
    bp.ConnectOutlet(sink.GetInlet(0), 0);
    sink.ConnectInlet(bp.GetOutlet(0), 0);

    bp.Calculate(YSE::T_DSP);
    CHECK(sink.received == nullptr);
}

TEST_CASE("pBandpass: center-frequency tone is preserved more than far-off tone") {
    YSE::PATCHER::pBandpass onBand, offBand;
    BufferSink sinkOn, sinkOff;
    onBand.ConnectOutlet(sinkOn.GetInlet(0), 0);
    sinkOn.ConnectInlet(onBand.GetOutlet(0), 0);
    offBand.ConnectOutlet(sinkOff.GetInlet(0), 0);
    sinkOff.ConnectInlet(offBand.GetOutlet(0), 0);

    onBand.GetInlet(1)->SetFloat(1000.0f, YSE::T_GUI);
    onBand.GetInlet(2)->SetFloat(2.0f, YSE::T_GUI);
    offBand.GetInlet(1)->SetFloat(1000.0f, YSE::T_GUI);
    offBand.GetInlet(2)->SetFloat(2.0f, YSE::T_GUI);

    YSE::DSP::buffer onIn(128), offIn(128);
    for (int iter = 0; iter < 30; ++iter) {
        fillSine(onIn, 1000.0f);
        fillSine(offIn, 8000.0f);
        onBand.GetInlet(0)->SetBuffer(&onIn, YSE::T_GUI);
        offBand.GetInlet(0)->SetBuffer(&offIn, YSE::T_GUI);
        onBand.Calculate(YSE::T_DSP);
        offBand.Calculate(YSE::T_DSP);
    }

    REQUIRE(sinkOn.received != nullptr);
    REQUIRE(sinkOff.received != nullptr);
    CHECK(TestHelpers::measureRms(*sinkOn.received) > TestHelpers::measureRms(*sinkOff.received));
}

TEST_CASE("pBandpass: ResetDSP clears the buffer pointer") {
    YSE::PATCHER::pBandpass bp;
    BufferSink sink;
    bp.ConnectOutlet(sink.GetInlet(0), 0);
    sink.ConnectInlet(bp.GetOutlet(0), 0);

    bp.GetInlet(1)->SetFloat(1000.0f, YSE::T_GUI);
    bp.GetInlet(2)->SetFloat(1.0f, YSE::T_GUI);

    YSE::DSP::buffer in(128);
    fillSine(in, 1000.0f);
    bp.GetInlet(0)->SetBuffer(&in, YSE::T_GUI);
    bp.Calculate(YSE::T_DSP);
    REQUIRE(sink.received != nullptr);

    sink.received = nullptr;
    bp.ResetDSP();
    bp.Calculate(YSE::T_DSP);
    CHECK(sink.received == nullptr);
}

// ─── pHighpass ────────────────────────────────────────────────────────────────

TEST_CASE("pHighpass: type name, input/output count, and output type") {
    YSE::patcher p;
    p.create(2);
    YSE::pHandle* h = p.CreateObject(YSE::OBJ::D_HIGHPASS);
    REQUIRE(h != nullptr);
    CHECK(std::string(h->Type()) == "~hp");
    CHECK(h->GetInputs()  == 2);
    CHECK(h->GetOutputs() == 1);
    CHECK(h->OutputDataType(0) == YSE::OUT_TYPE::BUFFER);
}

TEST_CASE("pHighpass: null input buffer produces no output") {
    YSE::PATCHER::pHighpass hp;
    BufferSink sink;
    hp.ConnectOutlet(sink.GetInlet(0), 0);
    sink.ConnectInlet(hp.GetOutlet(0), 0);

    hp.Calculate(YSE::T_DSP);
    CHECK(sink.received == nullptr);
}

TEST_CASE("pHighpass: attenuates a DC input after settling") {
    YSE::PATCHER::pHighpass hp;
    BufferSink sink;
    hp.ConnectOutlet(sink.GetInlet(0), 0);
    sink.ConnectInlet(hp.GetOutlet(0), 0);

    hp.GetInlet(1)->SetFloat(500.0f, YSE::T_GUI);

    YSE::DSP::buffer in(128);
    in = 1.0f;
    for (int iter = 0; iter < 20; ++iter) {
        hp.GetInlet(0)->SetBuffer(&in, YSE::T_GUI);
        hp.Calculate(YSE::T_DSP);
    }

    REQUIRE(sink.received != nullptr);
    float last = sink.received->getPtr()[sink.received->getLength() - 1];
    CHECK(std::abs(last) < 0.1f);
}

TEST_CASE("pHighpass: passes high-frequency content more than DC") {
    YSE::PATCHER::pHighpass hpDC, hpHi;
    BufferSink sinkDC, sinkHi;
    hpDC.ConnectOutlet(sinkDC.GetInlet(0), 0);
    sinkDC.ConnectInlet(hpDC.GetOutlet(0), 0);
    hpHi.ConnectOutlet(sinkHi.GetInlet(0), 0);
    sinkHi.ConnectInlet(hpHi.GetOutlet(0), 0);

    hpDC.GetInlet(1)->SetFloat(500.0f, YSE::T_GUI);
    hpHi.GetInlet(1)->SetFloat(500.0f, YSE::T_GUI);

    YSE::DSP::buffer bufDC(128), bufHi(128);
    for (int iter = 0; iter < 20; ++iter) {
        bufDC = 1.0f;
        fillSine(bufHi, 5000.0f);
        hpDC.GetInlet(0)->SetBuffer(&bufDC, YSE::T_GUI);
        hpHi.GetInlet(0)->SetBuffer(&bufHi, YSE::T_GUI);
        hpDC.Calculate(YSE::T_DSP);
        hpHi.Calculate(YSE::T_DSP);
    }

    REQUIRE(sinkDC.received != nullptr);
    REQUIRE(sinkHi.received != nullptr);
    CHECK(TestHelpers::measureRms(*sinkHi.received) > TestHelpers::measureRms(*sinkDC.received));
}

TEST_CASE("pHighpass: ResetDSP clears the buffer pointer") {
    YSE::PATCHER::pHighpass hp;
    BufferSink sink;
    hp.ConnectOutlet(sink.GetInlet(0), 0);
    sink.ConnectInlet(hp.GetOutlet(0), 0);

    hp.GetInlet(1)->SetFloat(500.0f, YSE::T_GUI);
    YSE::DSP::buffer in(128);
    fillSine(in, 1000.0f);
    hp.GetInlet(0)->SetBuffer(&in, YSE::T_GUI);
    hp.Calculate(YSE::T_DSP);
    REQUIRE(sink.received != nullptr);

    sink.received = nullptr;
    hp.ResetDSP();
    hp.Calculate(YSE::T_DSP);
    CHECK(sink.received == nullptr);
}

// ─── dVcf ─────────────────────────────────────────────────────────────────────
// NOTE: dVcf::Calculate cannot be fully tested until the null-out2 bug is fixed.
// Tracked in issue #30.

TEST_CASE("dVcf: type name, input/output count, and output type") {
    YSE::patcher p;
    p.create(2);
    YSE::pHandle* h = p.CreateObject(YSE::OBJ::D_VCF);
    REQUIRE(h != nullptr);
    CHECK(std::string(h->Type()) == "~vcf");
    CHECK(h->GetInputs()  == 3);
    CHECK(h->GetOutputs() == 2);
    CHECK(h->OutputDataType(0) == YSE::OUT_TYPE::BUFFER);
    CHECK(h->OutputDataType(1) == YSE::OUT_TYPE::BUFFER);
}

TEST_CASE("dVcf: null input buffer produces no output") {
    YSE::PATCHER::dVcf vcf;
    BufferSink out1, out2;
    vcf.ConnectOutlet(out1.GetInlet(0), 0);
    out1.ConnectInlet(vcf.GetOutlet(0), 0);
    vcf.ConnectOutlet(out2.GetInlet(0), 1);
    out2.ConnectInlet(vcf.GetOutlet(1), 0);

    vcf.Calculate(YSE::T_DSP);

    CHECK(out1.received == nullptr);
    CHECK(out2.received == nullptr);
}

TEST_CASE("dVcf: null center buffer produces no output") {
    YSE::PATCHER::dVcf vcf;
    BufferSink out1, out2;
    vcf.ConnectOutlet(out1.GetInlet(0), 0);
    out1.ConnectInlet(vcf.GetOutlet(0), 0);
    vcf.ConnectOutlet(out2.GetInlet(0), 1);
    out2.ConnectInlet(vcf.GetOutlet(1), 0);

    YSE::DSP::buffer in(128);
    fillSine(in, 1000.0f);
    vcf.GetInlet(0)->SetBuffer(&in, YSE::T_GUI);
    vcf.Calculate(YSE::T_DSP);

    CHECK(out1.received == nullptr);
    CHECK(out2.received == nullptr);
}

TEST_CASE("dVcf: ResetDSP clears both buffer pointers") {
    YSE::PATCHER::dVcf vcf;
    YSE::DSP::buffer in(128), center(128);
    fillSine(in, 1000.0f);
    center = 1000.0f;
    vcf.GetInlet(0)->SetBuffer(&in, YSE::T_GUI);
    vcf.GetInlet(1)->SetBuffer(&center, YSE::T_GUI);

    vcf.ResetDSP();

    BufferSink out1, out2;
    vcf.ConnectOutlet(out1.GetInlet(0), 0);
    out1.ConnectInlet(vcf.GetOutlet(0), 0);
    vcf.ConnectOutlet(out2.GetInlet(0), 1);
    out2.ConnectInlet(vcf.GetOutlet(1), 0);
    vcf.Calculate(YSE::T_DSP);

    CHECK(out1.received == nullptr);
    CHECK(out2.received == nullptr);
}

// ─── pDac ─────────────────────────────────────────────────────────────────────

TEST_CASE("pDac: constructs with N channels and accepts N buffer inputs") {
    YSE::PATCHER::pDac dac(2);
    CHECK(std::string(dac.Type()) == "~dac");
    CHECK(dac.NumInputs()  == 2);
    CHECK(dac.NumOutputs() == 0);
}

TEST_CASE("pDac: stores per-channel input buffers and returns them via GetBuffer") {
    YSE::PATCHER::pDac dac(2);

    YSE::DSP::buffer ch0(128), ch1(128);
    ch0 = 1.0f;
    ch1 = 2.0f;

    dac.GetInlet(0)->SetBuffer(&ch0, YSE::T_GUI);
    dac.GetInlet(1)->SetBuffer(&ch1, YSE::T_GUI);

    CHECK(dac.GetBuffer(0) == &ch0);
    CHECK(dac.GetBuffer(1) == &ch1);
}

TEST_CASE("pDac: GetBuffer returns nullptr for out-of-range channel") {
    YSE::PATCHER::pDac dac(2);
    CHECK(dac.GetBuffer(5) == nullptr);
}

TEST_CASE("pDac: ResetDSP clears every stored channel buffer pointer") {
    YSE::PATCHER::pDac dac(2);
    YSE::DSP::buffer ch0(128), ch1(128);
    dac.GetInlet(0)->SetBuffer(&ch0, YSE::T_GUI);
    dac.GetInlet(1)->SetBuffer(&ch1, YSE::T_GUI);
    REQUIRE(dac.GetBuffer(0) != nullptr);
    REQUIRE(dac.GetBuffer(1) != nullptr);

    dac.ResetDSP();

    CHECK(dac.GetBuffer(0) == nullptr);
    CHECK(dac.GetBuffer(1) == nullptr);
}

} // TEST_SUITE("patcher")
