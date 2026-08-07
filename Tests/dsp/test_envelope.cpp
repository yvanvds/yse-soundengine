// Tests for YSE::DSP::lfo, YSE::DSP::ADSRenvelope, and YSE::DSP::envelope.
// No audio device required; SAMPLERATE is initialised to 48000 by the
// portaudioDeviceManager translation unit at static-initialisation time.

#include <doctest/doctest.h>
#include <cmath>
#include <cstring>
#include <new>
#include "dsp/lfo.hpp"
#include "dsp/envelope.hpp"
#include "headers/constants.hpp"
#include "dsp/ADSRenvelope.hpp"
#include "support/audio_helpers.hpp"

TEST_SUITE("dsp") {

  // ─── lfo ──────────────────────────────────────────────────────────────────────

  TEST_CASE("lfo: LFO_NONE returns buffer of all 1.0") {
    YSE::DSP::lfo osc;
    YSE::DSP::buffer& buf = osc(YSE::DSP::LFO_NONE, 1.0f);
    float* ptr = buf.getPtr();
    for (unsigned i = 0; i < buf.getLength(); ++i)
      CHECK(ptr[i] == doctest::Approx(1.0f).epsilon(1e-5f));
  }

  TEST_CASE("lfo: zero frequency falls back to LFO_NONE (returns 1.0)") {
    // frequency == 0 → operator() forces type = LFO_NONE regardless of argument.
    YSE::DSP::lfo osc;
    YSE::DSP::buffer& buf = osc(YSE::DSP::LFO_SINE, 0.0f);
    float* ptr = buf.getPtr();
    for (unsigned i = 0; i < buf.getLength(); ++i)
      CHECK(ptr[i] == doctest::Approx(1.0f).epsilon(1e-5f));
  }

  // Helper — called from within TEST_CASE so CHECK macros report the right location.
  static void checkLfoBounded(YSE::DSP::LFO_TYPE type, float freq) {
    YSE::DSP::lfo osc;
    for (int call = 0; call < 10; ++call) {
      YSE::DSP::buffer& buf = osc(type, freq);
      float* ptr = buf.getPtr();
      for (unsigned i = 0; i < buf.getLength(); ++i) {
        CHECK(ptr[i] >= 0.0f);
        CHECK(ptr[i] <= 1.0f);
      }
    }
  }

  TEST_CASE("lfo: LFO_SINE output bounded in [0, 1]") {
    checkLfoBounded(YSE::DSP::LFO_SINE, 2.0f);
  }

  TEST_CASE("lfo: LFO_TRIANGLE output bounded in [0, 1]") {
    checkLfoBounded(YSE::DSP::LFO_TRIANGLE, 2.0f);
  }

  TEST_CASE("lfo: LFO_SAW output bounded in [0, 1]") {
    checkLfoBounded(YSE::DSP::LFO_SAW, 2.0f);
  }

  TEST_CASE("lfo: LFO_SAW_REVERSED output bounded in [0, 1]") {
    checkLfoBounded(YSE::DSP::LFO_SAW_REVERSED, 2.0f);
  }

  TEST_CASE("lfo: LFO_SQUARE output bounded in [0, 1]") {
    // At freq=2 Hz, phaseLength = SAMPLERATE/2*0.5 (12000 at 48000 Hz) >>
    // buffer size (128), so each call fills entirely with
    // currentLineValue ∈ {0.0, 1.0}.
    checkLfoBounded(YSE::DSP::LFO_SQUARE, 2.0f);
  }

  TEST_CASE("lfo: LFO_RANDOM output bounded in [0, 1]") {
    checkLfoBounded(YSE::DSP::LFO_RANDOM, 2.0f);
  }

  TEST_CASE("lfo: LFO_SINE renders the sine table, not the triangle (#643)") {
    // Regression for issue #643: LFO_SINE read LfoTriangleTable, so sine and
    // triangle output were byte-identical, and the LfoSineTable build loop's
    // unclamped copyFrom left the table's last 44100 % 128 = 68 samples at
    // zero (a notch reading 0.5 after the [0, 1] rescale).
    //
    // Sweep the whole 44100-sample table in a single 128-sample block: the
    // phase step is chosen so sample j reads table index ~= j * (44099 / 127),
    // putting sample 0 on index 0 and sample 127 on index 44098 — inside the
    // formerly zero tail. The engine's sine oscillator is cosine-phased
    // (Pd-style cos table), so table entry k holds 0.5 + 0.5*cos(2*pi*k / N).
    constexpr double tableLength = 44100.0; // LFO_TABLE_LENGTH in lfo.cpp
    constexpr unsigned blockSize = 128;
    const float step = 44099.0f / 127.0f;
    // lfoPhaseStep() derives the advance as freq * tableLength / SAMPLERATE.
    const float freq = step * static_cast<float>(YSE::SAMPLERATE) / static_cast<float>(tableLength);

    YSE::DSP::lfo osc;
    YSE::DSP::buffer& buf = osc(YSE::DSP::LFO_SINE, freq, blockSize);
    const float* ptr = buf.getPtr();
    REQUIRE(buf.getLength() == blockSize);

    constexpr double kTwoPi = 6.283185307179586;
    float maxErr = 0.0f;
    unsigned worst = 0;
    for (unsigned j = 0; j < blockSize; ++j) {
      const double index = std::floor(static_cast<double>(j) * static_cast<double>(step));
      const float expected = static_cast<float>(0.5 + 0.5 * std::cos(kTwoPi * index / tableLength));
      const float err = std::fabs(ptr[j] - expected);
      if (err > maxErr) {
        maxErr = err;
        worst = j;
      }
    }
    CAPTURE(worst);
    // Unfixed engine: sample 0 reads the triangle's 0.0 against an expected
    // 1.0. Tolerance covers the cos-table interpolation (~2e-5) and cursor
    // rounding drift (< 1 table sample ~= 7e-5) with margin.
    CHECK(maxErr < 1e-3f);
    // Sample 127 reads index 44098, inside the 68-sample tail the build loop
    // used to leave at zero: a sine-table switch without the fill fix would
    // return 0.5 here instead of ~1.0.
    CHECK(ptr[blockSize - 1] > 0.99f);
  }

  TEST_CASE("lfo: LFO_SQUARE first block is defined even from dirtied storage (#644)") {
    // Regression for issue #644: the constructor left lineLength,
    // currentLineValue and previousLineValue indeterminate, and the
    // LFO_SQUARE / LFO_RANDOM branch reads all three before its first-call
    // reset assigns them. lineLength and currentLineValue are overwritten by
    // the `previousType != type` reset, but when phaseLength fits inside one
    // block the anti-click ramp starts from the indeterminate
    // previousLineValue, so the first block could hold arbitrary garbage.
    //
    // Heap luck is not a test: construct the lfo in storage deliberately
    // filled with 0x41 bytes (0x41414141 as float ~= 12.08), so on the
    // unfixed engine the first ramp starts at ~12.08 and the bounds checks
    // below fail deterministically.
    alignas(YSE::DSP::lfo) unsigned char storage[sizeof(YSE::DSP::lfo)];
    std::memset(storage, 0x41, sizeof(storage));
    YSE::DSP::lfo* osc = new (storage) YSE::DSP::lfo();

    // freq = SAMPLERATE / 128 makes phaseLength = 128 * 0.5 = 64 <= the
    // 128-sample block, so the very first call takes the ramp-drawing branch
    // that reads previousLineValue.
    const float freq = static_cast<float>(YSE::SAMPLERATE) / 128.0f;
    YSE::DSP::buffer& buf = (*osc)(YSE::DSP::LFO_SQUARE, freq, 128);
    const float* ptr = buf.getPtr();
    REQUIRE(buf.getLength() == 128);

    // previousLineValue is initialised to 0, and drawLine writes its start
    // value verbatim: the first sample of the first ramp is exactly 0.
    CHECK(ptr[0] == 0.0f);
    for (unsigned i = 0; i < buf.getLength(); ++i) {
      CHECK(ptr[i] >= 0.0f);
      CHECK(ptr[i] <= 1.0f);
    }

    osc->~lfo();
  }

  // ─── ADSRenvelope ─────────────────────────────────────────────────────────────
  //
  // All ADSR tests construct the envelope in-place to avoid copying raw-pointer
  // members (phase, envelopeEnd) set by generate().
  //
  // Envelope spec: 0 → 1 linear ramp over 0.1 s.
  // 0.1 × 48000 = 4800 samples.  ceil(4800/128) = 38 buffer-calls to exhaust
  // at the default rate; blocksToExhaustTenthSecond() below derives the count
  // from the live SAMPLERATE.

  TEST_CASE("ADSRenvelope: ATTACK output starts at zero") {
    YSE::DSP::ADSRenvelope adsr;
    adsr.addPoint({0.0f, 0.0f, 1.0f});
    adsr.addPoint({0.1f, 1.0f, 1.0f});
    adsr.generate();
    YSE::DSP::buffer& buf = adsr(YSE::DSP::ADSRenvelope::ATTACK);
    CHECK(buf.getPtr()[0] == doctest::Approx(0.0f).epsilon(1e-4f));
  }

  TEST_CASE("ADSRenvelope: ATTACK output bounded in [0, 1]") {
    YSE::DSP::ADSRenvelope adsr;
    adsr.addPoint({0.0f, 0.0f, 1.0f});
    adsr.addPoint({0.1f, 1.0f, 1.0f});
    adsr.generate();
    YSE::DSP::buffer& buf = adsr(YSE::DSP::ADSRenvelope::ATTACK);
    float* ptr = buf.getPtr();
    for (unsigned i = 0; i < buf.getLength(); ++i) {
      CHECK(ptr[i] >= 0.0f);
      CHECK(ptr[i] <= 1.0f);
    }
  }

  TEST_CASE("ADSRenvelope: RESUME continues the ramp beyond ATTACK position") {
    YSE::DSP::ADSRenvelope adsr;
    adsr.addPoint({0.0f, 0.0f, 1.0f});
    adsr.addPoint({0.1f, 1.0f, 1.0f});
    adsr.generate();
    YSE::DSP::buffer& attack_buf = adsr(YSE::DSP::ADSRenvelope::ATTACK);
    // Save value before the next call overwrites the internal result buffer.
    float last_attack = attack_buf.getPtr()[127];
    YSE::DSP::buffer& resume_buf = adsr(YSE::DSP::ADSRenvelope::RESUME);
    float first_resume = resume_buf.getPtr()[0];
    CHECK(first_resume > last_attack);
  }

  TEST_CASE("ADSRenvelope: ATTACK resets phase to the beginning") {
    YSE::DSP::ADSRenvelope adsr;
    adsr.addPoint({0.0f, 0.0f, 1.0f});
    adsr.addPoint({0.1f, 1.0f, 1.0f});
    adsr.generate();
    adsr(YSE::DSP::ADSRenvelope::ATTACK);
    adsr(YSE::DSP::ADSRenvelope::RESUME);
    // Second ATTACK must restart from sample 0 (value = 0).
    YSE::DSP::buffer& buf = adsr(YSE::DSP::ADSRenvelope::ATTACK);
    CHECK(buf.getPtr()[0] == doctest::Approx(0.0f).epsilon(1e-4f));
  }

  // 0.1 s envelope length expressed in 128-sample blocks at the live SAMPLERATE.
  // ceil(0.1 * SAMPLERATE / STANDARD_BUFFERSIZE):
  //   - 44100 Hz: 4410 samples → 35 blocks (block 34 still <4410; block 35 crosses)
  //   - 48000 Hz: 4800 samples → 38 blocks
  static inline int blocksToExhaustTenthSecond() {
    const unsigned samples = (unsigned)(0.1f * (float)YSE::SAMPLERATE);
    return (int)((samples + YSE::STANDARD_BUFFERSIZE - 1) / YSE::STANDARD_BUFFERSIZE);
  }

  TEST_CASE("ADSRenvelope: isAtEnd false during playback, true after envelope exhausted") {
    YSE::DSP::ADSRenvelope adsr;
    adsr.addPoint({0.0f, 0.0f, 1.0f});
    adsr.addPoint({0.1f, 1.0f, 1.0f});
    adsr.generate();
    const int total = blocksToExhaustTenthSecond();
    adsr(YSE::DSP::ADSRenvelope::ATTACK); // counts as block 1
    CHECK(!adsr.isAtEnd());
    // Process up to but not including the block that crosses envelopeEnd.
    for (int i = 1; i < total - 1; ++i)
      adsr(YSE::DSP::ADSRenvelope::RESUME);
    CHECK(!adsr.isAtEnd());
    adsr(YSE::DSP::ADSRenvelope::RESUME); // crossing block
    CHECK(adsr.isAtEnd());
  }

  TEST_CASE("ADSRenvelope: output is silent after envelope exhausted") {
    YSE::DSP::ADSRenvelope adsr;
    adsr.addPoint({0.0f, 0.0f, 1.0f});
    adsr.addPoint({0.1f, 1.0f, 1.0f});
    adsr.generate();
    const int total = blocksToExhaustTenthSecond();
    adsr(YSE::DSP::ADSRenvelope::ATTACK);
    for (int i = 1; i < total; ++i)
      adsr(YSE::DSP::ADSRenvelope::RESUME);
    REQUIRE(adsr.isAtEnd());
    YSE::DSP::buffer& buf = adsr(YSE::DSP::ADSRenvelope::RESUME);
    float* ptr = buf.getPtr();
    for (unsigned i = 0; i < buf.getLength(); ++i)
      CHECK(ptr[i] == doctest::Approx(0.0f).epsilon(1e-5f));
  }

  TEST_CASE("ADSRenvelope: table re-renders at the live rate on ATTACK (issue #637)") {
    // generate() bakes SAMPLERATE into the rendered table's sample counts, so
    // an envelope generated in one session used to keep the old rate's duration
    // after a close()/init() cycle — a 0.1 s ramp lasted 0.05 s of wall clock
    // after reopening at double the rate. The ATTACK edge now re-renders the
    // table when the session rate changed (the accepted device-restart
    // allocation path).
    TestHelpers::ScopedSampleRate at44(44100);
    YSE::DSP::ADSRenvelope adsr;
    adsr.addPoint({0.0f, 0.0f, 1.0f});
    adsr.addPoint({0.1f, 1.0f, 1.0f});
    adsr.generate(); // rendered for 44100: 4410 samples

    {
      TestHelpers::ScopedSampleRate at88(88200);
      // 0.1 s at 88200 = 8820 samples = 69 blocks of 128 — twice the count the
      // stale 44100-rendered table would need.
      const int total = blocksToExhaustTenthSecond();
      CHECK(total == 69);
      int blocks = 1;
      adsr(YSE::DSP::ADSRenvelope::ATTACK);
      while (!adsr.isAtEnd() && blocks < 1000) {
        adsr(YSE::DSP::ADSRenvelope::RESUME);
        ++blocks;
      }
      CHECK(blocks == total);
    }

    // Back at 44100 the next note re-renders again: 4410 samples = 35 blocks.
    int blocks = 1;
    adsr(YSE::DSP::ADSRenvelope::ATTACK);
    while (!adsr.isAtEnd() && blocks < 1000) {
      adsr(YSE::DSP::ADSRenvelope::RESUME);
      ++blocks;
    }
    CHECK(blocks == blocksToExhaustTenthSecond());
    CHECK(blocks == 35);
  }

  // Regression tests for #300: a RELEASE (or RESUME) issued before any ATTACK
  // must not dereference the uninitialised `phase` pointer. This happens for a
  // voice released before it ever renders a block (NOTE_ON + NOTE_OFF draining
  // in one audio block, or an all-notes-off before the first render). Without
  // the guard these crash / trip ASan; with it they settle to silence + isAtEnd.

  TEST_CASE("ADSRenvelope: RELEASE before ATTACK is silent and at-end (no loop)") {
    YSE::DSP::ADSRenvelope adsr;
    adsr.addPoint({0.0f, 0.0f, 1.0f});
    adsr.addPoint({0.1f, 1.0f, 1.0f});
    adsr.generate();
    // No ATTACK has primed `phase`.
    YSE::DSP::buffer& buf = adsr(YSE::DSP::ADSRenvelope::RELEASE);
    float* ptr = buf.getPtr();
    for (unsigned i = 0; i < buf.getLength(); ++i)
      CHECK(ptr[i] == doctest::Approx(0.0f).epsilon(1e-5f));
    CHECK(adsr.isAtEnd());
  }

  TEST_CASE("ADSRenvelope: RELEASE before ATTACK is silent and at-end (sustain loop)") {
    // A sustain loop makes generate() set loopEnd != nullptr, which drives the
    // RELEASE branch's `while (*phase != *search) search--;` — the exact path
    // from #300 that dereferences the uninitialised `phase`.
    YSE::DSP::ADSRenvelope adsr;
    adsr.addPoint({0.0f, 0.0f, 1.0f});
    adsr.addPoint({0.05f, 1.0f, 1.0f, /*loopStart*/ true});
    adsr.addPoint({0.1f, 1.0f, 1.0f, /*loopStart*/ false, /*loopEnd*/ true});
    adsr.generate();
    YSE::DSP::buffer& buf = adsr(YSE::DSP::ADSRenvelope::RELEASE);
    float* ptr = buf.getPtr();
    for (unsigned i = 0; i < buf.getLength(); ++i)
      CHECK(ptr[i] == doctest::Approx(0.0f).epsilon(1e-5f));
    CHECK(adsr.isAtEnd());
  }

  TEST_CASE("ADSRenvelope: RESUME before ATTACK is silent and at-end") {
    YSE::DSP::ADSRenvelope adsr;
    adsr.addPoint({0.0f, 0.0f, 1.0f});
    adsr.addPoint({0.1f, 1.0f, 1.0f});
    adsr.generate();
    YSE::DSP::buffer& buf = adsr(YSE::DSP::ADSRenvelope::RESUME);
    float* ptr = buf.getPtr();
    for (unsigned i = 0; i < buf.getLength(); ++i)
      CHECK(ptr[i] == doctest::Approx(0.0f).epsilon(1e-5f));
    CHECK(adsr.isAtEnd());
  }

  // Regression tests for #642: the RELEASE branch scans backwards from loopEnd
  // for a sample equal to *phase with no lower bound. While phase is inside
  // the sustain region the scan terminates (at worst on phase itself), but
  // once phase is past loopEnd — a repeated RELEASE, or a table whose loopEnd
  // has no loopStart — the current value may occur nowhere before loopEnd and
  // the scan walked off the front of the allocation. The fix floors the scan
  // at the table start and keeps the current phase when no match exists.

  TEST_CASE("ADSRenvelope: repeated RELEASE with phase past loopEnd stays in bounds (issue #642)") {
    // Sustain sits flat at 0.5; the release tail ramps 0.5 → 1.0, so every
    // tail value is strictly greater than anything at or before loopEnd and
    // the exact-match scan cannot succeed once phase is in the tail.
    YSE::DSP::ADSRenvelope adsr;
    adsr.addPoint({0.0f, 0.0f, 1.0f});
    adsr.addPoint({0.05f, 0.5f, 1.0f, /*loopStart*/ true});
    adsr.addPoint({0.1f, 0.5f, 1.0f, /*loopStart*/ false, /*loopEnd*/ true});
    adsr.addPoint({0.15f, 1.0f, 1.0f});
    adsr.generate();

    adsr(YSE::DSP::ADSRenvelope::ATTACK);
    for (int i = 0; i < 40; ++i)
      adsr(YSE::DSP::ADSRenvelope::RESUME); // loop the sustain region a while
    REQUIRE(!adsr.isAtEnd());

    // Drive RELEASE on every block, as a caller is free to do. The first call
    // jumps phase to the matching sample near loopEnd (glitch-avoidance); every
    // later call re-enters the scan with phase in the tail, where no exact
    // match exists before loopEnd.
    bool first = true;
    int blocks = 0;
    while (!adsr.isAtEnd() && blocks < 1000) {
      YSE::DSP::buffer& buf = adsr(YSE::DSP::ADSRenvelope::RELEASE);
      float* ptr = buf.getPtr();
      if (first) {
        // The glitch-avoidance jump still lands on the sustain value.
        CHECK(ptr[0] == doctest::Approx(0.5f).epsilon(1e-4f));
        first = false;
      }
      for (unsigned i = 0; i < buf.getLength(); ++i) {
        CHECK(ptr[i] >= 0.0f);
        CHECK(ptr[i] <= 1.0f);
      }
      ++blocks;
    }
    // The 0.05 s tail must finish; if a bad fallback restarted the table every
    // block this never reaches the end.
    CHECK(adsr.isAtEnd());
    const int tailBlocks =
        (int)(((unsigned)(0.05f * (float)YSE::SAMPLERATE) + YSE::STANDARD_BUFFERSIZE - 1) /
              YSE::STANDARD_BUFFERSIZE);
    CHECK(blocks <= tailBlocks + 1);
  }

  TEST_CASE("ADSRenvelope: RELEASE past a loopEnd with no loopStart stays in bounds (issue #642)") {
    // Without a loopStart the envelope never loops, so playback runs straight
    // through loopEnd into the 0.5 → 1.0 tail. A first RELEASE issued there
    // scans for a tail value that never occurs at or before loopEnd.
    YSE::DSP::ADSRenvelope adsr;
    adsr.addPoint({0.0f, 0.0f, 1.0f});
    adsr.addPoint({0.05f, 0.5f, 1.0f, /*loopStart*/ false, /*loopEnd*/ true});
    adsr.addPoint({0.1f, 1.0f, 1.0f});
    adsr.generate();

    adsr(YSE::DSP::ADSRenvelope::ATTACK);
    // Advance until the block's last sample is clearly inside the tail.
    float last = 0.f;
    int guard = 0;
    while (last < 0.6f && !adsr.isAtEnd() && ++guard < 1000) {
      YSE::DSP::buffer& buf = adsr(YSE::DSP::ADSRenvelope::RESUME);
      last = buf.getPtr()[buf.getLength() - 1];
    }
    REQUIRE(last > 0.6f);

    YSE::DSP::buffer& buf = adsr(YSE::DSP::ADSRenvelope::RELEASE);
    float* ptr = buf.getPtr();
    // The release continues from the current tail position — no backward jump.
    CHECK(ptr[0] >= last);
    for (unsigned i = 0; i < buf.getLength(); ++i) {
      CHECK(ptr[i] >= 0.0f);
      CHECK(ptr[i] <= 1.0f);
    }
  }

  // ─── envelope (breakpoint extractor) ─────────────────────────────────────────

  TEST_CASE("envelope: create from buffer extracts non-empty breakpoint list") {
    YSE::DSP::envelope env;
    const unsigned bufLen = 3 * YSE::SAMPLERATE; // 3 s → 2 complete 1-second windows
    YSE::DSP::buffer src(bufLen);
    src = 0.5f;
    bool ok = env.create(src, 1000);
    CHECK(ok);
    CHECK(env.elms() > 0);
  }

  TEST_CASE("envelope: breakpoint values match source amplitude") {
    YSE::DSP::envelope env;
    const unsigned bufLen = 3 * YSE::SAMPLERATE;
    YSE::DSP::buffer src(bufLen);
    src = 0.5f;
    env.create(src, 1000);
    for (unsigned i = 0; i < env.elms(); ++i)
      CHECK(env[i].value == doctest::Approx(0.5f).epsilon(1e-5f));
  }

  // Regression tests for #641: create() used to compute the window as
  // (Int)windowDuration * SAMPLERATE, truncating to zero for any window under
  // one second — the analysis loop then never advanced and breakPoints grew
  // until OOM. The cast must bind to the product, with a one-sample floor.

  TEST_CASE("envelope: sub-second window terminates and covers the buffer (issue #641)") {
    YSE::DSP::envelope env;
    const unsigned bufLen = YSE::SAMPLERATE; // 1 s of audio
    YSE::DSP::buffer src(bufLen);
    src = 0.5f;
    bool ok = env.create(src, 100); // 100 ms window — hung before the fix
    CHECK(ok);
    // Loop runs while pos + window < bufLen with window = 0.1 s of samples.
    const unsigned window = (unsigned)(0.1f * (float)YSE::SAMPLERATE);
    REQUIRE(window > 0);
    const unsigned expected = (bufLen - 1) / window; // 9 at any common rate
    CHECK(env.elms() == expected);
    for (unsigned i = 0; i < env.elms(); ++i) {
      CHECK(env[i].time == doctest::Approx(i * 0.1f).epsilon(1e-4f));
      CHECK(env[i].value == doctest::Approx(0.5f).epsilon(1e-5f));
    }
  }

  TEST_CASE("envelope: sub-sample window is floored to one sample (issue #641)") {
    // At a (pathologically) low sample rate a 1 ms window is less than one
    // sample; the floor keeps the loop advancing instead of stalling on a
    // zero-sample window.
    TestHelpers::ScopedSampleRate lowRate(500); // 1 ms → 0.5 samples → floored to 1
    YSE::DSP::envelope env;
    const unsigned bufLen = 100;
    YSE::DSP::buffer src(bufLen);
    src = 0.25f;
    bool ok = env.create(src, 1);
    CHECK(ok);
    CHECK(env.elms() == bufLen - 1); // window == 1 → one breakpoint per sample
  }

  TEST_CASE("envelope: normalize scales max breakpoint value to 1.0") {
    YSE::DSP::envelope env;
    const unsigned bufLen = 3 * YSE::SAMPLERATE;
    YSE::DSP::buffer src(bufLen);
    src = 0.5f;
    env.create(src, 1000);
    env.normalize();
    for (unsigned i = 0; i < env.elms(); ++i)
      CHECK(env[i].value == doctest::Approx(1.0f).epsilon(1e-5f));
  }

} // TEST_SUITE("dsp")
