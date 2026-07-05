// Tests for YSE::SYNTH::dspVoice (voice base) and YSE::SYNTH::sineVoice
// (reference sine + ADSR voice) — issue #152, implementing §3 of
// docs/design/synth_core.md.
//
// Coverage:
//   - intent-driven lifecycle transitions (WANTSTOPLAY -> PLAYING -> release
//     -> STOPPED, and retrigger),
//   - ADSR envelope shape (silent attack onset, sustained body, decaying
//     release tail),
//   - correct rendered frequency (FFT peak-bin check),
//   - clone() independence (distinct instances, distinct note state),
//   - no heap allocation in process() after warm-up.
//
// No audio device required; SAMPLERATE is initialised to 44100 by the
// portaudioDeviceManager translation unit at static-initialisation time.

#include <doctest/doctest.h>
#include <cmath>
#include <memory>
#include "synth/sineVoice.hpp"
#include "dsp/fourier/fft.hpp"
#include "support/audio_helpers.hpp"
#include "support/alloc_probe.hpp"

TEST_SUITE("dsp") {

  using YSE::SOUND_STATUS;
  using YSE::SYNTH::dspVoice;
  using YSE::SYNTH::sineVoice;

  // ─── lifecycle ────────────────────────────────────────────────────────────

  TEST_CASE("sineVoice: WANTSTOPLAY settles the intent to PLAYING") {
    sineVoice v;
    v.frequency(69.f);
    v.velocity(1.f);
    SOUND_STATUS intent = YSE::SS_WANTSTOPLAY;
    v.process(intent);
    CHECK(intent == YSE::SS_PLAYING);
  }

  TEST_CASE("sineVoice: sustains indefinitely while intent stays PLAYING") {
    sineVoice v;
    v.frequency(69.f);
    v.velocity(1.f);
    SOUND_STATUS intent = YSE::SS_WANTSTOPLAY;
    v.process(intent); // -> PLAYING
    for (int i = 0; i < 200; ++i)
      v.process(intent);
    CHECK(intent == YSE::SS_PLAYING);
    CHECK(TestHelpers::measureRms(v.samples[0]) > 0.05f);
  }

  TEST_CASE("sineVoice: WANTSTOSTOP releases and settles to STOPPED") {
    sineVoice v;
    v.frequency(69.f);
    v.velocity(1.f);
    v.attack(0.005f).decay(0.01f).sustain(0.8f).release(0.05f);

    SOUND_STATUS intent = YSE::SS_WANTSTOPLAY;
    v.process(intent); // -> PLAYING
    for (int i = 0; i < 20; ++i)
      v.process(intent); // reach sustain

    intent = YSE::SS_WANTSTOSTOP;
    int blocks = 0;
    const int kMaxBlocks = 500;
    while (intent != YSE::SS_STOPPED && blocks < kMaxBlocks) {
      v.process(intent);
      ++blocks;
    }
    CHECK(intent == YSE::SS_STOPPED);
    CHECK(blocks < kMaxBlocks);
    // Tail is silent once stopped.
    v.process(intent);
    CHECK(TestHelpers::decaysToSilence(v.samples[0], 1e-5f));
  }

  TEST_CASE("sineVoice: retriggers after a completed release") {
    sineVoice v;
    v.frequency(69.f);
    v.velocity(1.f);
    v.attack(0.005f).decay(0.01f).sustain(0.8f).release(0.02f);

    // Play then fully release.
    SOUND_STATUS intent = YSE::SS_WANTSTOPLAY;
    v.process(intent);
    for (int i = 0; i < 10; ++i)
      v.process(intent);
    intent = YSE::SS_WANTSTOSTOP;
    for (int i = 0; i < 500 && intent != YSE::SS_STOPPED; ++i)
      v.process(intent);
    REQUIRE(intent == YSE::SS_STOPPED);

    // Retrigger.
    intent = YSE::SS_WANTSTOPLAY;
    v.process(intent);
    CHECK(intent == YSE::SS_PLAYING);
    for (int i = 0; i < 20; ++i)
      v.process(intent);
    CHECK(TestHelpers::measureRms(v.samples[0]) > 0.05f);
  }

  // ─── envelope shape ───────────────────────────────────────────────────────

  TEST_CASE("sineVoice: attack onset starts from silence") {
    sineVoice v;
    v.frequency(69.f);
    v.velocity(1.f);
    v.attack(0.05f); // long attack so the block-0 onset is unambiguously near 0
    SOUND_STATUS intent = YSE::SS_WANTSTOPLAY;
    v.process(intent);
    CHECK(std::abs(v.samples[0].getPtr()[0]) < 1e-3f);
  }

  TEST_CASE("sineVoice: release tail decays monotonically toward silence") {
    sineVoice v;
    v.frequency(69.f);
    v.velocity(1.f);
    v.attack(0.005f).decay(0.01f).sustain(0.9f).release(0.2f);

    SOUND_STATUS intent = YSE::SS_WANTSTOPLAY;
    v.process(intent);
    for (int i = 0; i < 30; ++i)
      v.process(intent); // sustain
    float sustainRms = TestHelpers::measureRms(v.samples[0]);

    intent = YSE::SS_WANTSTOSTOP;
    v.process(intent); // first release block
    float earlyRelease = TestHelpers::measureRms(v.samples[0]);
    for (int i = 0; i < 20; ++i)
      v.process(intent);
    float lateRelease = TestHelpers::measureRms(v.samples[0]);

    CHECK(sustainRms > 0.1f);
    CHECK(lateRelease < earlyRelease);
    CHECK(lateRelease < sustainRms);
  }

  // ─── rendered frequency ───────────────────────────────────────────────────

  TEST_CASE("sineVoice: rendered pitch matches the requested note (FFT bin)") {
    // Short attack/decay so the voice is at steady sustain during capture.
    sineVoice v;
    v.attack(0.001f).decay(0.001f).sustain(1.f).release(0.05f);
    const float noteNumber = 69.f; // A4
    v.frequency(noteNumber);
    v.velocity(1.f);

    const float noteHz = YSE::DSP::MidiToFreq(noteNumber);

    SOUND_STATUS intent = YSE::SS_WANTSTOPLAY;
    v.process(intent);
    for (int i = 0; i < 8; ++i)
      v.process(intent); // settle into sustain

    const unsigned N = 2048;
    const unsigned block = YSE::STANDARD_BUFFERSIZE;
    YSE::DSP::buffer re(N), im(N);
    re = 0.f;
    im = 0.f;
    unsigned filled = 0;
    while (filled + block <= N) {
      v.process(intent); // stays PLAYING
      re.copyFrom(v.samples[0], 0, filled, block);
      filled += block;
    }

    YSE::DSP::fft f;
    f(re, im);
    unsigned peak = TestHelpers::peakBinIndex(f.getReal().getPtr(), f.getImaginary().getPtr(), N);

    const float binHz = static_cast<float>(YSE::SAMPLERATE) / static_cast<float>(N);
    const float peakHz = static_cast<float>(peak) * binHz;
    // Within one bin of the note frequency (rectangular-window leakage can
    // shift the peak to the nearest bin, never further for a bin-adjacent tone).
    CHECK(std::abs(peakHz - noteHz) < binHz);
  }

  // ─── clone() independence ─────────────────────────────────────────────────

  TEST_CASE("sineVoice: clone() yields a distinct, same-typed voice") {
    sineVoice proto;
    proto.attack(0.02f).release(0.3f).sustain(0.6f);

    std::unique_ptr<dspVoice> a(proto.clone());
    std::unique_ptr<dspVoice> b(proto.clone());

    CHECK(a.get() != nullptr);
    CHECK(b.get() != nullptr);
    CHECK(a.get() != b.get());
    CHECK(a.get() != static_cast<dspVoice*>(&proto));

    auto* as = dynamic_cast<sineVoice*>(a.get());
    REQUIRE(as != nullptr);
    // Envelope parameters carried across the clone.
    CHECK(as->attack() == doctest::Approx(0.02f));
    CHECK(as->release() == doctest::Approx(0.3f));
    CHECK(as->sustain() == doctest::Approx(0.6f));
  }

  TEST_CASE("sineVoice: clones hold independent note state") {
    sineVoice proto;
    std::unique_ptr<dspVoice> a(proto.clone());
    std::unique_ptr<dspVoice> b(proto.clone());

    a->frequency(60.f);
    a->velocity(0.25f);
    b->frequency(72.f);
    b->velocity(0.9f);

    CHECK(a->getFrequency() == doctest::Approx(YSE::DSP::MidiToFreq(60.f)));
    CHECK(b->getFrequency() == doctest::Approx(YSE::DSP::MidiToFreq(72.f)));
    CHECK(a->getVelocity() == doctest::Approx(0.25f));
    CHECK(b->getVelocity() == doctest::Approx(0.9f));

    // Rendering one clone must not touch the other's output.
    SOUND_STATUS ia = YSE::SS_WANTSTOPLAY;
    for (int i = 0; i < 5; ++i)
      a->process(ia);
    CHECK(b->samples[0].isSilent());
  }

  // ─── real-time discipline ─────────────────────────────────────────────────

  TEST_CASE("sineVoice: process() does not allocate after warm-up") {
    sineVoice v;
    v.frequency(69.f);
    v.velocity(1.f);

    // Warm-up: exercise every code path once (attack, sustain, release,
    // stop, retrigger) so any first-use lazy sizing is done before probing.
    SOUND_STATUS intent = YSE::SS_WANTSTOPLAY;
    v.process(intent);
    for (int i = 0; i < 30; ++i)
      v.process(intent);
    intent = YSE::SS_WANTSTOSTOP;
    for (int i = 0; i < 500 && intent != YSE::SS_STOPPED; ++i)
      v.process(intent);
    intent = YSE::SS_WANTSTOPLAY;
    v.process(intent);
    for (int i = 0; i < 5; ++i)
      v.process(intent);

    {
      TestHelpers::ProbeScope probe;
      // Steady sustain.
      for (int i = 0; i < 40; ++i)
        v.process(intent);
      // A full release cycle.
      intent = YSE::SS_WANTSTOSTOP;
      for (int i = 0; i < 500 && intent != YSE::SS_STOPPED; ++i)
        v.process(intent);
      // Silence and another retrigger.
      v.process(intent);
      intent = YSE::SS_WANTSTOPLAY;
      for (int i = 0; i < 10; ++i)
        v.process(intent);
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
  }

} // TEST_SUITE("dsp")
