// Regression coverage for issue #286: `_length` must not be written from the
// render threads on a shared, buffer-backed soundFile.
//
// A DSP::buffer-backed soundFile is deliberately shared across sounds
// (SOUND::Manager::addFile dedups by buffer pointer), so two sounds in
// different channels can call read() concurrently on two render workers. The
// old code re-assigned `file->_length = _audioBuffer->getLength()` inside every
// read(), which is a concurrent non-atomic write of the plain `int _length` —
// formal UB and TSan-visible. The fix publishes `_length` once on the
// single-threaded create() path and reads a local inside read().
//
// White-box access (INTERNAL::soundFile) mirrors test_sound_streaming.cpp; this
// TU is compiled with LIBSOUNDFILE_BACKEND (set for just this file in
// Tests/CMakeLists.txt). The DSP::buffer path never touches libsndfile, but the
// concrete soundFile type lives behind that gate.
//
// The two-thread read below is the exact scenario the issue predicts. Under
// ThreadSanitizer it flags the old `_length` write and is clean after the fix;
// under a normal build the written value was always identical, so the race is
// benign there — hence the primary normal-build regression assertion is that
// length() reports the source length straight after create() (the old code left
// _length unwritten at that point).

#if LIBSOUNDFILE_BACKEND

#include <doctest/doctest.h>
#include <atomic>
#include <thread>
#include <vector>

#include "yse.hpp"
#include "internal/lsfSoundfile.h"
#include "dsp/buffer.hpp"
#include "support/null_device.hpp"

using YSE::SOUND_STATUS;
using YSE::INTERNAL::soundFile;
// Flt / UInt / Int / Bool are global typedefs (headers/types.hpp).

TEST_SUITE("sound") {

  TEST_CASE("soundFile: shared DSP::buffer read is race-free on _length (issue #286)") {
    if (!TestHelpers::engineInit()) return;

    // A distinct value per frame so a mis-read produces obviously wrong output.
    const UInt N = 1000;
    YSE::DSP::buffer src(N);
    {
      Flt* p = src.getPtr();
      for (UInt n = 0; n < N; ++n)
        p[n] = static_cast<Flt>(n) * 0.001f;
    }

    // One buffer-backed soundFile, played from two render threads below — the
    // shared-file situation addFile() produces in production.
    soundFile sf(&src);
    REQUIRE(sf.create(false));
    bool ready = (sf.getState() == YSE::INTERNAL::FILESTATE::READY);
    CHECK(ready);

    // _length is published on the create() path now, not lazily in read().
    bool lengthMatches = (sf.length() == N);
    CHECK(lengthMatches);

    // Render the shared file from two threads with independent play state.
    const UInt blockSize = 128;
    const int blocks = 60;
    auto render = [&](std::vector<Flt>& outSamples) {
      std::vector<YSE::DSP::buffer> fb(1);
      fb[0].resize(blockSize);
      Flt pos = 0.f;
      Flt volume = 1.f;
      SOUND_STATUS intent = YSE::SS_PLAYING_FULL_VOLUME;
      outSamples.reserve(static_cast<size_t>(blocks) * blockSize);
      for (int b = 0; b < blocks; ++b) {
        REQUIRE(sf.read(fb, pos, blockSize, 1.0f, /*loop=*/true, intent, volume));
        const Flt* o = fb[0].getPtr();
        for (UInt k = 0; k < blockSize; ++k)
          outSamples.push_back(o[k]);
      }
    };

    std::vector<Flt> a;
    std::vector<Flt> b;
    std::thread t1([&] { render(a); });
    std::thread t2([&] { render(b); });
    t1.join();
    t2.join();

    // Deterministic, identical output from both render passes: the hoist to a
    // local preserves the exact loop behaviour, and nothing is corrupted by the
    // concurrent access.
    bool sameSize = (a.size() == b.size());
    REQUIRE(sameSize);
    bool identical = (a == b);
    CHECK(identical);

    // Sanity: the first frame of the first block is the start of the source
    // ramp, so the read actually produced the source data.
    bool firstSampleOk = a.empty() ? false : (a[0] == doctest::Approx(0.0f));
    CHECK(firstSampleOk);
  }

} // TEST_SUITE("sound")

#endif // LIBSOUNDFILE_BACKEND
