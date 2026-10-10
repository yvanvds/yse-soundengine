// Regression tests for the LFO waveform tables versus the engine sample rate.
//
// Issue #48: LFO_SAW_REVERSED SIGSEGV on a Pixel 7 Pro at the Oboe-negotiated
// 48 kHz. Root cause was the static LfoSawTable being sized at first
// construction (then 44100) while per-sample wrap math used the live
// SAMPLERATE (48000) — reading past the table end.
//
// PR #71 fixed that by rebuilding the tables in the lfo *constructor* whenever
// their size diverged from SAMPLERATE. Issue #636: that guard sits on a path a
// later rate change cannot reach. Raise SAMPLERATE after the last lfo was
// constructed and nothing rebuilds anything, while operator() keeps wrapping
// the cursor on the new, larger rate — a 3900 sample out-of-bounds read at
// 44100 -> 48000.
//
// The fix decouples the table resolution from SAMPLERATE entirely (tables are
// a fixed 44100 samples per cycle, the per-sample advance is scaled by the live
// rate) so the wrap bound is always the length of the very buffer being
// indexed.
//
// Two cases below, deliberately from both sides of the rate change:
//   * rate raised after construction — the actual out-of-bounds read. Bounded
//     output is all that is observable without a sanitizer; under ASan (the
//     tests-asan preset) or Android MTE this is a hard heap-buffer-overflow.
//   * rate lowered after construction — the same invariant violation (wrap
//     bound != table length) in the direction where the consequence is a
//     deterministic wrong value instead of undefined behaviour. This one fails
//     with a plain CHECK on the unfixed engine.

#include <doctest/doctest.h>
#include "dsp/lfo.hpp"
#include "headers/constants.hpp"

namespace {

  // Restores the engine rate whatever the test does, so the shared test binary
  // is not left running at another rate.
  struct SampleRateGuard {
    UInt saved;
    SampleRateGuard() : saved(YSE::SAMPLERATE) {}
    ~SampleRateGuard() {
      YSE::SAMPLERATE = saved;
    }
  };

} // namespace

TEST_SUITE("dsp") {

  TEST_CASE("lfo: LFO_SAW_REVERSED stays in bounds at the current SAMPLERATE") {
    // Construct ten LFO instances and pump 128 samples through each — the
    // first construction sizes the static tables, subsequent constructions hit
    // the size-match early-out path, and every operator() exercises the cursor
    // wrap. Any OOB read here would either crash under ASAN/MTE or read
    // garbage > 1.0.
    for (int call = 0; call < 10; ++call) {
      YSE::DSP::lfo osc;
      YSE::DSP::buffer& buf = osc(YSE::DSP::LFO_SAW_REVERSED, 2.0f);
      float* ptr = buf.getPtr();
      for (unsigned i = 0; i < buf.getLength(); ++i) {
        CHECK(ptr[i] >= 0.0f);
        CHECK(ptr[i] <= 1.0f);
      }
    }
  }

  TEST_CASE("lfo: raising SAMPLERATE after construction does not read past the table") {
    // Issue #636. Construct at 44100, then raise the rate the way a device
    // restart does, and keep driving the *same* instance — nothing constructs a
    // new lfo, so the constructor's rebuild guard never runs.
    //
    // Before the fix the cursor wrapped at 48000 in a 44100-long table and the
    // last 3900 indices of every cycle read heap past the allocation. ASan
    // reports heap-buffer-overflow at the read; without a sanitizer the values
    // are merely garbage, so this also range-checks the output.
    SampleRateGuard guard;

    YSE::SAMPLERATE = 44100;
    YSE::DSP::lfo osc;

    YSE::SAMPLERATE = 48000;

    // 100 Hz over 8 blocks of 128 = 1024 samples: more than one full cycle, so
    // the cursor passes 44100 while the table is only 44100 long.
    for (int block = 0; block < 8; ++block) {
      YSE::DSP::buffer& buf = osc(YSE::DSP::LFO_SAW, 100.0f, 128);
      float* ptr = buf.getPtr();
      for (unsigned i = 0; i < buf.getLength(); ++i) {
        CHECK(ptr[i] >= 0.0f);
        CHECK(ptr[i] <= 1.0f);
      }
    }
  }

  TEST_CASE("lfo: the whole waveform is traversed after SAMPLERATE changes") {
    // The deterministic half of issue #636, and the one that fails on the
    // unfixed engine without needing a sanitizer.
    //
    // Construct at 48000 (which, before the fix, sized the tables to 48000),
    // then lower the rate to 44100. The read path then wrapped the cursor at
    // 44100 inside a 48000-long table, so the last 3900 entries — which hold
    // the end of the descending ramp and the whole anti-click flyback — were
    // never reached: the saw bottomed out at ~0.077 instead of 0.0.
    //
    // After the fix the table is a fixed 44100 samples and the advance is
    // scaled by the live rate, so exactly one full cycle is traversed at any
    // rate and the minimum is 0.
    SampleRateGuard guard;

    YSE::SAMPLERATE = 48000;
    YSE::DSP::lfo osc;

    YSE::SAMPLERATE = 44100;

    // 100 Hz at 44100 is 441 samples per cycle; 512 samples covers a full one.
    // The advance is exactly 100.0f, so the sampled table indices are exact
    // multiples of 100 and land on 43900 — the bottom of the ramp.
    float minValue = 2.0f;
    float maxValue = -1.0f;
    for (int block = 0; block < 4; ++block) {
      YSE::DSP::buffer& buf = osc(YSE::DSP::LFO_SAW, 100.0f, 128);
      float* ptr = buf.getPtr();
      for (unsigned i = 0; i < buf.getLength(); ++i) {
        if (ptr[i] < minValue) minValue = ptr[i];
        if (ptr[i] > maxValue) maxValue = ptr[i];
      }
    }

    // Unfixed engine: minValue == 1 - 44000/47800 == 0.0795.
    CHECK(minValue < 0.01f);
    CHECK(maxValue > 0.99f);
    CHECK(minValue >= 0.0f);
    CHECK(maxValue <= 1.0f);
  }

  TEST_CASE("lfo: table length is independent of the engine sample rate") {
    // The structural invariant behind the fix: nothing about the tables is
    // derived from SAMPLERATE any more, so no rate change can leave a stale
    // size behind for the read path to overrun. One cycle always spans the
    // same 44100-sample table, whatever rate the lfo happens to be built at,
    // and the period in *samples* therefore tracks the live rate.
    SampleRateGuard guard;

    YSE::SAMPLERATE = 96000;
    YSE::DSP::lfo osc;

    // One cycle at 1 Hz and 96000 Hz is 96000 samples; the flyback occupies the
    // last 200/44100 of the cycle, so sample 90000 is still on the descending
    // ramp and must be well below the value at sample 0.
    YSE::DSP::buffer& first = osc(YSE::DSP::LFO_SAW, 1.0f, 128);
    const float start = first.getPtr()[0];
    CHECK(start > 0.99f);

    float last = start;
    // 96000 / 128 = 750 blocks per cycle; stop just short of the flyback.
    for (int block = 1; block < 700; ++block) {
      YSE::DSP::buffer& buf = osc(YSE::DSP::LFO_SAW, 1.0f, 128);
      last = buf.getPtr()[buf.getLength() - 1];
      CHECK(last >= 0.0f);
      CHECK(last <= 1.0f);
    }
    // 700 * 128 = 89600 samples in, i.e. 93.3% through the cycle: the ramp is
    // down to ~0.067. A table still sized to a different rate would put this
    // somewhere else entirely (or out of bounds).
    CHECK(last < 0.10f);
    CHECK(last > 0.02f);
  }

} // TEST_SUITE("dsp")
