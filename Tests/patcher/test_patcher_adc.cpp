// Tests for the ~adc patcher object and the patcher-as-insert host adapter
// (DSP::patcherInsert) — issue #167.
//
// Covers: the ~adc node in isolation (start point, buffer outlets, buffer
// injection), and the full insert flow — a passthrough graph is bit-transparent,
// a filter graph measurably processes, JSON round-trips a graph containing
// ~adc, and host/graph channel-count mismatches follow the documented contract.
// No audio device required.

#include <doctest/doctest.h>
#include <cmath>
#include <string>
#include <vector>
#include "patcher/patcher.hpp"
#include "patcher/pHandle.hpp"
#include "patcher/pObjectList.hpp"
#include "patcher/genericObjects/pAdc.h"
#include "dsp/patcherInsert.hpp"
#include "dsp/buffer.hpp"
#include "headers/defines.hpp"
#include "support/audio_helpers.hpp"
#include "patcher/sinks.hpp"

using TestHelpers::BufferSink;
using TestHelpers::measureRms;

static constexpr float kPi = 3.14159265358979323846f;

namespace {

  inline void fillSine(YSE::DSP::buffer& buf, float freq, float sr = 44100.0f) {
    float* p = buf.getPtr();
    for (unsigned i = 0; i < buf.getLength(); ++i)
      p[i] = std::sin(2.0f * kPi * freq * static_cast<float>(i) / sr);
  }

  // A distinctive, non-trivial signal so a bit-transparency check is meaningful.
  inline void fillPattern(YSE::DSP::buffer& buf, float phase = 0.f) {
    float* p = buf.getPtr();
    const unsigned n = buf.getLength();
    for (unsigned i = 0; i < n; ++i) {
      p[i] = 0.5f * std::sin(phase + 0.13f * static_cast<float>(i)) +
             (static_cast<float>(i) / static_cast<float>(n)) - 0.5f;
    }
  }

  inline bool exactlyEqual(YSE::DSP::buffer& a, YSE::DSP::buffer& b) {
    if (a.getLength() != b.getLength()) return false;
    float* pa = a.getPtr();
    float* pb = b.getPtr();
    for (unsigned i = 0; i < a.getLength(); ++i)
      if (pa[i] != pb[i]) return false;
    return true;
  }

} // anonymous namespace

TEST_SUITE("patcher") {

  // ─── pAdc node ──────────────────────────────────────────────────────────────

  TEST_CASE("pAdc: type name, no inlets, N buffer outlets") {
    YSE::patcher p;
    p.create(2);
    YSE::pHandle* h = p.CreateObject(YSE::OBJ::D_ADC);
    REQUIRE(h != nullptr);
    CHECK(std::string(h->Type()) == "~adc");
    CHECK(h->GetInputs() == 0);
    CHECK(h->GetOutputs() == 2);
    CHECK(h->OutputDataType(0) == YSE::OUT_TYPE::BUFFER);
    CHECK(h->OutputDataType(1) == YSE::OUT_TYPE::BUFFER);
  }

  TEST_CASE("pAdc: is a DSP start point (no active DSP input)") {
    YSE::PATCHER::pAdc adc(2);
    CHECK(adc.NumChannels() == 2);
    CHECK(adc.NumInputs() == 0);
    CHECK(adc.IsDSPStartPoint());
  }

  TEST_CASE("pAdc: injected channel buffer is sent through the matching outlet") {
    YSE::PATCHER::pAdc adc(2);
    BufferSink sink0, sink1;
    adc.ConnectOutlet(sink0.GetInlet(0), 0);
    sink0.ConnectInlet(adc.GetOutlet(0), 0);
    adc.ConnectOutlet(sink1.GetInlet(0), 1);
    sink1.ConnectInlet(adc.GetOutlet(1), 0);

    YSE::DSP::buffer ch0(128), ch1(128);
    ch0 = 1.0f;
    ch1 = 2.0f;
    adc.SetChannelBuffer(0, &ch0);
    adc.SetChannelBuffer(1, &ch1);

    adc.Calculate(YSE::T_DSP);

    CHECK(sink0.received == &ch0);
    CHECK(sink1.received == &ch1);
  }

  TEST_CASE("pAdc: a null channel sends nothing on that outlet") {
    YSE::PATCHER::pAdc adc(2);
    BufferSink sink0, sink1;
    adc.ConnectOutlet(sink0.GetInlet(0), 0);
    sink0.ConnectInlet(adc.GetOutlet(0), 0);
    adc.ConnectOutlet(sink1.GetInlet(0), 1);
    sink1.ConnectInlet(adc.GetOutlet(1), 0);

    YSE::DSP::buffer ch0(128);
    ch0 = 1.0f;
    adc.SetChannelBuffer(0, &ch0);
    // channel 1 deliberately left null

    adc.Calculate(YSE::T_DSP);

    CHECK(sink0.received == &ch0);
    CHECK(sink1.received == nullptr);
  }

  TEST_CASE("pAdc: SetChannelBuffer ignores out-of-range channels") {
    YSE::PATCHER::pAdc adc(2);
    YSE::DSP::buffer buf(128);
    // Must not crash / write out of bounds.
    adc.SetChannelBuffer(5, &buf);
    CHECK(adc.NumChannels() == 2);
  }

  // ─── patcherInsert: passthrough is bit-transparent ────────────────────────────

  TEST_CASE("patcherInsert: ~adc -> ~dac passthrough is bit-transparent") {
    YSE::patcher p;
    p.create(2);
    YSE::pHandle* adc = p.CreateObject(YSE::OBJ::D_ADC);
    YSE::pHandle* dac = p.CreateObject(YSE::OBJ::D_DAC);
    REQUIRE(adc != nullptr);
    REQUIRE(dac != nullptr);
    p.Connect(adc, 0, dac, 0);
    p.Connect(adc, 1, dac, 1);

    YSE::DSP::patcherInsert insert(p);

    MULTICHANNELBUFFER io;
    io.resize(2);
    io[0].resize(128);
    io[1].resize(128);
    fillPattern(io[0], 0.0f);
    fillPattern(io[1], 1.7f);

    YSE::DSP::buffer dry0(io[0]);
    YSE::DSP::buffer dry1(io[1]);

    insert.process(io);

    CHECK(exactlyEqual(io[0], dry0));
    CHECK(exactlyEqual(io[1], dry1));
  }

  // ─── patcherInsert: a filter graph measurably processes ───────────────────────

  TEST_CASE("patcherInsert: ~adc -> ~lp -> ~dac attenuates a high tone") {
    YSE::patcher p;
    p.create(1);
    YSE::pHandle* adc = p.CreateObject(YSE::OBJ::D_ADC);
    YSE::pHandle* lp = p.CreateObject(YSE::OBJ::D_LOWPASS, "200"); // 200 Hz cutoff
    YSE::pHandle* dac = p.CreateObject(YSE::OBJ::D_DAC);
    REQUIRE(adc != nullptr);
    REQUIRE(lp != nullptr);
    REQUIRE(dac != nullptr);
    p.Connect(adc, 0, lp, 0); // adc audio -> lowpass audio in
    p.Connect(lp, 0, dac, 0); // lowpass out -> dac

    YSE::DSP::patcherInsert insert(p);

    MULTICHANNELBUFFER io;
    io.resize(1);
    io[0].resize(128);

    YSE::DSP::buffer dry(128);
    fillSine(dry, 8000.0f);
    const float dryRms = measureRms(dry);
    REQUIRE(dryRms > 0.0f);

    // Feed the tone block after block so the filter settles, measuring the last
    // processed block.
    for (int iter = 0; iter < 40; ++iter) {
      fillSine(io[0], 8000.0f);
      insert.process(io);
    }

    const float wetRms = measureRms(io[0]);
    CHECK(wetRms < dryRms * 0.5f); // clearly attenuated
    CHECK_FALSE(exactlyEqual(io[0], dry)); // signal was modified
  }

  // ─── patcherInsert: JSON round-trip of a graph containing ~adc ─────────────────

  TEST_CASE("patcherInsert: JSON round-trip preserves an ~adc passthrough graph") {
    YSE::patcher src;
    src.create(2);
    YSE::pHandle* adc = src.CreateObject(YSE::OBJ::D_ADC);
    YSE::pHandle* dac = src.CreateObject(YSE::OBJ::D_DAC);
    src.Connect(adc, 0, dac, 0);
    src.Connect(adc, 1, dac, 1);
    std::string json = src.DumpJSON();
    CHECK(json.find("~adc") != std::string::npos);

    // Rebuild from JSON into a fresh patcher.
    YSE::patcher loaded;
    loaded.create(2);
    loaded.ParseJSON(json);
    CHECK(loaded.Objects() == 2);

    bool hasAdc = false;
    for (unsigned i = 0; i < loaded.Objects(); ++i) {
      YSE::pHandle* h = loaded.GetHandleFromList(i);
      if (h != nullptr && std::string(h->Type()) == "~adc") hasAdc = true;
    }
    CHECK(hasAdc);

    // The restored graph must still work as a bit-transparent insert, which
    // proves the ~adc -> ~dac connections survived the round-trip.
    YSE::DSP::patcherInsert insert(loaded);
    MULTICHANNELBUFFER io;
    io.resize(2);
    io[0].resize(128);
    io[1].resize(128);
    fillPattern(io[0], 0.3f);
    fillPattern(io[1], 2.1f);
    YSE::DSP::buffer dry0(io[0]);
    YSE::DSP::buffer dry1(io[1]);

    insert.process(io);

    CHECK(exactlyEqual(io[0], dry0));
    CHECK(exactlyEqual(io[1], dry1));
  }

  // ─── patcherInsert: channel-count mismatch contract ───────────────────────────

  TEST_CASE("patcherInsert: host has fewer channels than the graph") {
    // 2-channel graph, 1-channel host buffer. Channel 0 is processed; the graph
    // never touches a channel the host does not provide.
    YSE::patcher p;
    p.create(2);
    YSE::pHandle* adc = p.CreateObject(YSE::OBJ::D_ADC);
    YSE::pHandle* dac = p.CreateObject(YSE::OBJ::D_DAC);
    p.Connect(adc, 0, dac, 0);
    p.Connect(adc, 1, dac, 1);

    YSE::DSP::patcherInsert insert(p);

    MULTICHANNELBUFFER io;
    io.resize(1);
    io[0].resize(128);
    fillPattern(io[0], 0.5f);
    YSE::DSP::buffer dry0(io[0]);

    insert.process(io); // must not read io[1]

    REQUIRE(io.size() == 1);
    CHECK(exactlyEqual(io[0], dry0)); // channel 0 passed through
  }

  TEST_CASE("patcherInsert: host has more channels than the graph produces") {
    // 1-channel graph, 2-channel host buffer. Channel 0 is replaced by the
    // graph; channel 1 (beyond the graph's output count) passes through dry.
    YSE::patcher p;
    p.create(1);
    YSE::pHandle* adc = p.CreateObject(YSE::OBJ::D_ADC);
    YSE::pHandle* dac = p.CreateObject(YSE::OBJ::D_DAC);
    p.Connect(adc, 0, dac, 0);

    YSE::DSP::patcherInsert insert(p);

    MULTICHANNELBUFFER io;
    io.resize(2);
    io[0].resize(128);
    io[1].resize(128);
    fillPattern(io[0], 0.2f);
    fillPattern(io[1], 1.1f);
    YSE::DSP::buffer dry0(io[0]);
    YSE::DSP::buffer dry1(io[1]);

    insert.process(io);

    CHECK(exactlyEqual(io[0], dry0)); // passthrough graph replaced ch0 with itself
    CHECK(exactlyEqual(io[1], dry1)); // ch1 left unchanged (dry)
  }

  TEST_CASE("patcherInsert: null / uncreated patcher process is a safe no-op") {
    YSE::patcher p; // create() never called -> pimpl is null
    YSE::DSP::patcherInsert insert(p);

    MULTICHANNELBUFFER io;
    io.resize(1);
    io[0].resize(128);
    fillPattern(io[0], 0.0f);
    YSE::DSP::buffer dry(io[0]);

    insert.process(io); // must not crash

    CHECK(exactlyEqual(io[0], dry));
  }

} // TEST_SUITE("patcher")
