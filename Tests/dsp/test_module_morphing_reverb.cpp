// Behavioural tests for the morphing reverb MODULE (issue #326).
//
// morphingReverb packages the engine's reverb core (INTERNAL::reverbDSP) as a
// chainable dspObject whose preset interpolation is a *control input*: two
// endpoint parameter sets (slot A / slot B, named presets or custom
// REVERB::presetValues) are linearly blended by morph(t) in [0, 1]. Writes are
// atomic stores; the audio thread applies the blend through the core's ~1 s
// faders, so morph moves are click-free.
//
// The acceptance criteria (issue #326) drive the cases below:
//   - the shared preset table carries exactly the values reverb::setPreset
//     has always applied (bit-compatibility of the extraction)
//   - the morph control input actually drives the blend (t = 0 sounds like A,
//     t = 1 sounds like B), is clamped, and survives sweeps without blowing up
//   - steady-state process() allocates nothing; a channel-count change is
//     tolerated (the one allocation-permitted path)
//
// No audio device required — SAMPLERATE is initialised by the
// portaudioDeviceManager translation unit at static-initialisation time.

#include <doctest/doctest.h>
#include <cmath>
#include "dsp/modules/morphingReverb.hpp"
#include "reverb/reverbPresets.hpp"
#include "headers/defines.hpp"
#include "support/alloc_probe.hpp"
#include "support/audio_helpers.hpp"

namespace {

  // The reverb core's faders ramp over 1000 ms = SAMPLERATE samples. With the
  // default 128-sample test blocks that is ~345 blocks at 44.1 kHz and ~375 at
  // 48 kHz; 500 blocks settles the faders at any supported rate.
  constexpr int kSettleBlocks = 500;

  // Process n silent blocks through the module (channels-count channels),
  // discarding output. The input is re-zeroed every block because process()
  // overwrites it with the reverb output.
  void runSilence(YSE::DSP::MODULES::morphingReverb& rev, int n, unsigned channels = 1) {
    MULTICHANNELBUFFER buf(channels);
    for (int i = 0; i < n; ++i) {
      for (unsigned ch = 0; ch < channels; ++ch)
        buf[ch] = 0.0f;
      rev.process(buf);
    }
  }

  // Feed one impulse, then integrate the wet output energy over `blocks`
  // silent blocks — a monotone proxy for how much reverb the module produces.
  float impulseTailEnergy(YSE::DSP::MODULES::morphingReverb& rev, int blocks) {
    MULTICHANNELBUFFER buf(1);
    buf[0] = 0.0f;
    buf[0].getPtr()[0] = 1.0f;
    rev.process(buf);

    float energy = 0.0f;
    for (int i = 0; i < blocks; ++i) {
      buf[0] = 0.0f;
      rev.process(buf);
      float rms = TestHelpers::measureRms(buf[0]);
      energy += rms * rms;
    }
    return energy;
  }

  bool sameValues(const YSE::REVERB::presetValues& a, const YSE::REVERB::presetValues& b) {
    bool same = a.roomsize == b.roomsize && a.damp == b.damp && a.dry == b.dry && a.wet == b.wet &&
                a.modFrequency == b.modFrequency && a.modWidth == b.modWidth;
    for (int i = 0; i < 4; ++i)
      same = same && a.earlyTime[i] == b.earlyTime[i] && a.earlyGain[i] == b.earlyGain[i];
    return same;
  }

} // anonymous namespace

TEST_SUITE("dsp") {

  // ─── the shared preset table (issue #326 extraction) ──────────────────────────

  TEST_CASE("morphingReverb: preset table matches the historical setPreset values") {
    // Spot-check the extracted table against the values reverb::setPreset has
    // always applied (and that the interface tests in test_reverb_dsp.cpp
    // still verify through the reverb interface).
    const YSE::REVERB::presetValues& off = YSE::REVERB::getPresetValues(YSE::REVERB_OFF);
    CHECK(off.roomsize == doctest::Approx(0.0f));
    CHECK(off.damp == doctest::Approx(0.0f));
    CHECK(off.dry == doctest::Approx(1.0f));
    CHECK(off.wet == doctest::Approx(0.0f));

    const YSE::REVERB::presetValues& generic = YSE::REVERB::getPresetValues(YSE::REVERB_GENERIC);
    CHECK(generic.roomsize == doctest::Approx(0.5f));
    CHECK(generic.damp == doctest::Approx(0.5f));
    CHECK(generic.dry == doctest::Approx(0.6f));
    CHECK(generic.wet == doctest::Approx(0.4f));

    const YSE::REVERB::presetValues& hall = YSE::REVERB::getPresetValues(YSE::REVERB_HALL);
    CHECK(hall.roomsize == doctest::Approx(0.7f));
    CHECK(hall.damp == doctest::Approx(0.4f));
    CHECK(hall.dry == doctest::Approx(0.5f));
    CHECK(hall.wet == doctest::Approx(0.5f));

    const YSE::REVERB::presetValues& cave = YSE::REVERB::getPresetValues(YSE::REVERB_CAVE);
    CHECK(cave.roomsize == doctest::Approx(1.0f));
    CHECK(cave.wet == doctest::Approx(0.7f));
    CHECK(cave.dry == doctest::Approx(0.3f));
    CHECK(cave.earlyTime[0] == doctest::Approx(100.0f));
    CHECK(cave.earlyGain[0] == doctest::Approx(0.8f));

    const YSE::REVERB::presetValues& pipe = YSE::REVERB::getPresetValues(YSE::REVERB_SEWERPIPE);
    CHECK(pipe.modFrequency == doctest::Approx(3.5f));
    CHECK(pipe.modWidth == doctest::Approx(20.0f));
    CHECK(pipe.earlyTime[2] == doctest::Approx(1100.0f));
    CHECK(pipe.earlyGain[2] == doctest::Approx(0.01f));
  }

  TEST_CASE("morphingReverb: preset lookup is range-safe") {
    // Out-of-range enum values fall back to REVERB_OFF instead of reading
    // past the table.
    const YSE::REVERB::presetValues& bogus =
        YSE::REVERB::getPresetValues(static_cast<YSE::REVERB_PRESET>(9999));
    CHECK(sameValues(bogus, YSE::REVERB::getPresetValues(YSE::REVERB_OFF)));
  }

  // ─── the interpolation core ───────────────────────────────────────────────────

  TEST_CASE("morphingReverb: morph(a, b, 0) returns a and morph(a, b, 1) returns b") {
    const YSE::REVERB::presetValues& a = YSE::REVERB::getPresetValues(YSE::REVERB_CAVE);
    const YSE::REVERB::presetValues& b = YSE::REVERB::getPresetValues(YSE::REVERB_PADDED);

    CHECK(sameValues(YSE::REVERB::morph(a, b, 0.0f), a));
    CHECK(sameValues(YSE::REVERB::morph(a, b, 1.0f), b));
  }

  TEST_CASE("morphingReverb: morph interpolates every field linearly") {
    const YSE::REVERB::presetValues& a = YSE::REVERB::getPresetValues(YSE::REVERB_OFF);
    const YSE::REVERB::presetValues& b = YSE::REVERB::getPresetValues(YSE::REVERB_CAVE);

    YSE::REVERB::presetValues mid = YSE::REVERB::morph(a, b, 0.5f);
    CHECK(mid.roomsize == doctest::Approx(0.5f)); // 0 → 1
    CHECK(mid.dry == doctest::Approx(0.65f)); // 1 → 0.3
    CHECK(mid.wet == doctest::Approx(0.35f)); // 0 → 0.7
    CHECK(mid.earlyTime[3] == doctest::Approx(400.0f)); // 0 → 800
    CHECK(mid.earlyGain[3] == doctest::Approx(0.25f)); // 0 → 0.5
  }

  TEST_CASE("morphingReverb: morph clamps t to [0, 1]") {
    const YSE::REVERB::presetValues& a = YSE::REVERB::getPresetValues(YSE::REVERB_OFF);
    const YSE::REVERB::presetValues& b = YSE::REVERB::getPresetValues(YSE::REVERB_CAVE);

    CHECK(sameValues(YSE::REVERB::morph(a, b, -3.0f), a));
    CHECK(sameValues(YSE::REVERB::morph(a, b, 42.0f), b));
  }

  // ─── module parameters ────────────────────────────────────────────────────────

  TEST_CASE("morphingReverb: sensible defaults") {
    YSE::DSP::MODULES::morphingReverb rev;
    CHECK(rev.morph() == doctest::Approx(0.0f));
    CHECK(sameValues(rev.presetA(), YSE::REVERB::getPresetValues(YSE::REVERB_GENERIC)));
    CHECK(sameValues(rev.presetB(), YSE::REVERB::getPresetValues(YSE::REVERB_HALL)));
  }

  TEST_CASE("morphingReverb: preset slots round-trip (named and custom)") {
    YSE::DSP::MODULES::morphingReverb rev;
    rev.presetA(YSE::REVERB_CAVE).presetB(YSE::REVERB_OFF);
    CHECK(sameValues(rev.presetA(), YSE::REVERB::getPresetValues(YSE::REVERB_CAVE)));
    CHECK(sameValues(rev.presetB(), YSE::REVERB::getPresetValues(YSE::REVERB_OFF)));

    YSE::REVERB::presetValues custom = YSE::REVERB::getPresetValues(YSE::REVERB_HALL);
    custom.dry = 0.0f; // send/return flavour: fully wet
    custom.wet = 1.0f;
    rev.presetB(custom);
    CHECK(sameValues(rev.presetB(), custom));
  }

  TEST_CASE("morphingReverb: morph control input is clamped") {
    YSE::DSP::MODULES::morphingReverb rev;
    rev.morph(7.5f);
    CHECK(rev.morph() == doctest::Approx(1.0f));
    rev.morph(-2.0f);
    CHECK(rev.morph() == doctest::Approx(0.0f));
    rev.morph(0.25f);
    CHECK(rev.morph() == doctest::Approx(0.25f));
  }

  // ─── behaviour: the control input drives the blend ────────────────────────────

  TEST_CASE("morphingReverb: morph position selects the audible space") {
    // A = CAVE (long, wet), B = OFF (dry pass-through). At morph 0 an impulse
    // must leave a reverb tail; at morph 1 it must not.
    float wetTail = 0.0f;
    {
      YSE::DSP::MODULES::morphingReverb rev;
      rev.presetA(YSE::REVERB_CAVE).presetB(YSE::REVERB_OFF).morph(0.0f);
      runSilence(rev, kSettleBlocks);
      wetTail = impulseTailEnergy(rev, 60);
    }

    float dryTail = 0.0f;
    {
      YSE::DSP::MODULES::morphingReverb rev;
      rev.presetA(YSE::REVERB_CAVE).presetB(YSE::REVERB_OFF).morph(1.0f);
      runSilence(rev, kSettleBlocks);
      dryTail = impulseTailEnergy(rev, 60);
    }

    CHECK(wetTail > 0.0f);
    CHECK(dryTail < wetTail * 0.01f);
  }

  TEST_CASE("morphingReverb: fully morphed to REVERB_OFF passes input through") {
    YSE::DSP::MODULES::morphingReverb rev;
    rev.presetA(YSE::REVERB_CAVE).presetB(YSE::REVERB_OFF).morph(1.0f);
    runSilence(rev, kSettleBlocks); // settle faders on the OFF blend

    MULTICHANNELBUFFER buf(1);
    buf[0] = 0.5f;
    rev.process(buf);

    // OFF is dry = 1, wet = 0: the buffer must come back (essentially)
    // unchanged.
    float* ptr = buf[0].getPtr();
    for (unsigned i = 0; i < buf[0].getLength(); ++i)
      CHECK(ptr[i] == doctest::Approx(0.5f).epsilon(1e-3f));
  }

  TEST_CASE("morphingReverb: control-rate morph sweep stays bounded and finite") {
    // Sweep the morph input over its full range while audio runs — the
    // click-free contract at minimum means no instability: every sample stays
    // finite and inside a sane amplitude bound.
    YSE::DSP::MODULES::morphingReverb rev;
    rev.presetA(YSE::REVERB_CAVE).presetB(YSE::REVERB_OFF);
    runSilence(rev, kSettleBlocks);

    MULTICHANNELBUFFER buf(1);
    const int sweepBlocks = 200;
    bool allFinite = true;
    float maxAbs = 0.0f;
    for (int i = 0; i < sweepBlocks; ++i) {
      rev.morph(static_cast<Flt>(i) / static_cast<Flt>(sweepBlocks - 1));
      buf[0] = 0.5f;
      rev.process(buf);
      float* ptr = buf[0].getPtr();
      for (unsigned k = 0; k < buf[0].getLength(); ++k) {
        if (!std::isfinite(ptr[k])) allFinite = false;
        float a = std::fabs(ptr[k]);
        if (a > maxAbs) maxAbs = a;
      }
    }

    CHECK(allFinite);
    CHECK(maxAbs < 4.0f);
  }

  // ─── RT discipline ────────────────────────────────────────────────────────────

  TEST_CASE("morphingReverb: steady-state process allocates nothing") {
    YSE::DSP::MODULES::morphingReverb rev;
    rev.presetA(YSE::REVERB_GENERIC).presetB(YSE::REVERB_HALL).morph(0.3f);

    // Warm up: first process sizes the per-channel state (the one permitted
    // allocation) and the faders start ramping.
    runSilence(rev, 40, 2);

    MULTICHANNELBUFFER buf(2);
    {
      TestHelpers::ProbeScope probe;
      for (int i = 0; i < 20; ++i) {
        rev.morph(0.3f + 0.02f * static_cast<Flt>(i)); // live control writes
        buf[0] = 0.25f;
        buf[1] = 0.25f;
        rev.process(buf);
      }
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
  }

  TEST_CASE("morphingReverb: tolerates a changing channel count between calls") {
    YSE::DSP::MODULES::morphingReverb rev;
    rev.presetA(YSE::REVERB_CAVE).presetB(YSE::REVERB_OFF).morph(0.0f);

    MULTICHANNELBUFFER mono(1);
    mono[0] = 0.5f;
    rev.process(mono);

    MULTICHANNELBUFFER stereo(2);
    stereo[0] = 0.5f;
    stereo[1] = 0.5f;
    rev.process(stereo);

    MULTICHANNELBUFFER backToMono(1);
    backToMono[0] = 0.5f;
    rev.process(backToMono);

    // Survived the resizes and still produces finite output.
    float* ptr = backToMono[0].getPtr();
    bool finite = true;
    for (unsigned i = 0; i < backToMono[0].getLength(); ++i)
      if (!std::isfinite(ptr[i])) finite = false;
    CHECK(finite);
  }

} // TEST_SUITE("dsp")
