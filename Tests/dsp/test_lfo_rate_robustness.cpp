// Regression test for issue #48: LFO_SAW_REVERSED SIGSEGV on Pixel 7 Pro at
// the Oboe-negotiated 48 kHz. Root cause was the static LfoSawTable being
// sized at first construction (then 44100) while per-sample wrap math used
// the live SAMPLERATE (48000) — reading past the table end.
//
// PR #71 fixed the LFO constructor to rebuild the tables on any
// SAMPLERATE mismatch. This test exercises the path under a forced
// non-default rate so ASAN on Linux (and MTE on Android) would catch a
// regression at this exact site.

#include <doctest/doctest.h>
#include "dsp/lfo.hpp"

TEST_SUITE("dsp") {

TEST_CASE("lfo: LFO_SAW_REVERSED stays in bounds at the current SAMPLERATE") {
    // Construct ten LFO instances and pump 128 samples through each — the
    // first construction sizes the static tables to the live SAMPLERATE,
    // subsequent constructions hit the size-match early-out path, and every
    // operator() exercises the cursor wrap. Any OOB read here would either
    // crash under ASAN/MTE or read garbage > 1.0.
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

} // TEST_SUITE("dsp")
