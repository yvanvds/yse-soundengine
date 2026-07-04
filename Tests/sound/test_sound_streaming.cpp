// Tests for streaming-sound playback in the interleaved read path
// (YseEngine/internal/abstractSoundFile.cpp + lsfSoundfile.cpp).
//
// Regression coverage for issue #185: streaming refills must happen on the slow
// thread pool, never as blocking disk I/O on the audio callback. These tests
// drive INTERNAL::soundFile::read() directly (playing the audio thread's role)
// while the real slow pool fills the back buffer, and assert the produced audio
// is correct across buffer boundaries, at end-of-file, when looping, and after a
// stop/restart. They also exercise destruction while a refill is in flight.
//
// White-box access: this TU is compiled with LIBSOUNDFILE_BACKEND (set for just
// this file in Tests/CMakeLists.txt) so it can see INTERNAL::soundFile and use
// libsndfile to generate fixtures. The engine is initialised with the audio
// stream paused (TestHelpers::engineInit), so the test thread is the sole caller
// of read(); the slow pool still runs on its own worker threads.

#if LIBSOUNDFILE_BACKEND

#include <doctest/doctest.h>
#include <sndfile.hh>
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>

#include "yse.hpp"
#include "internal/lsfSoundfile.h"
#include "internal/time.h"
#include "dsp/buffer.hpp"
#include "support/null_device.hpp"

using YSE::SOUND_STATUS;
using YSE::INTERNAL::soundFile;
// Flt / UInt / Bool are global typedefs (headers/types.hpp).

namespace {

  // A distinct, position-sensitive sample value for frame n. Using a hash makes a
  // mis-swap (replaying the wrong buffer) produce clearly wrong values.
  float sampleAt(long n) {
    uint32_t h = static_cast<uint32_t>(n) * 2654435761u;
    return static_cast<float>(((h >> 8) & 0xFFFF) / 32768.0 - 1.0);
  }

  // Write a mono float WAV of `frames` frames at the engine sample rate and return
  // its path; also fills `src` with the exact samples written (float WAV is stored
  // losslessly, so playback should reproduce these values bit-for-bit).
  std::string writeWav(long frames, std::vector<float>& src) {
    src.resize(static_cast<size_t>(frames));
    for (long n = 0; n < frames; ++n)
      src[static_cast<size_t>(n)] = sampleAt(n);

    namespace fs = std::filesystem;
    fs::path p = fs::temp_directory_path() / ("yse_stream_" + std::to_string(frames) + ".wav");
    SndfileHandle h(p.string().c_str(), SFM_WRITE, SF_FORMAT_WAV | SF_FORMAT_FLOAT, 1,
                    static_cast<int>(YSE::SAMPLERATE));
    h.writef(src.data(), frames);
    // SndfileHandle flushes and closes on destruction (end of this function).
    return p.string();
  }

  // Wait for the slow-pool load to finish (state == READY) or time out.
  bool waitReady(soundFile& f) {
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (std::chrono::steady_clock::now() < deadline) {
      if (f.getState() == YSE::INTERNAL::READY) return true;
      std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    return f.getState() == YSE::INTERNAL::READY;
  }

  // Read one STANDARD_BUFFERSIZE block, transparently retrying a transient underrun
  // (a fully silent block while still playing) after giving the slow pool time to
  // land the refill. Returns true once the stream has stopped (EOF reached).
  bool readBlock(soundFile& f, Flt& pos, Bool loop, SOUND_STATUS& intent, Flt& vol,
                 std::vector<float>& out) {
    for (int retry = 0; retry < 500; ++retry) {
      std::vector<YSE::DSP::buffer> fb(1); // one mono output buffer of length STANDARD_BUFFERSIZE
      f.read(fb, pos, YSE::STANDARD_BUFFERSIZE, 1.0f, loop, intent, vol);
      const Flt* p = fb[0].getPtr();
      out.assign(p, p + YSE::STANDARD_BUFFERSIZE);
      if (intent == YSE::SS_STOPPED) return true;
      bool allZero = std::all_of(out.begin(), out.end(), [](float v) { return v == 0.0f; });
      if (!allZero) return false;
      std::this_thread::sleep_for(std::chrono::milliseconds(3)); // underrun: let the refill land
    }
    return false;
  }

  // Small periodic pause so the slow pool always fills the next buffer well before
  // the play cursor reaches it — keeps the tests free of (allowed but timing-
  // dependent) underruns so frame accounting stays exact.
  void breathe(int block) {
    if ((block & 7) == 0) std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }

  const long S = static_cast<long>(YSE::STREAM_BUFFERSIZE); // 44100

} // namespace

TEST_SUITE("sound") {

  // 1. Audio stays sample-accurate across buffer boundaries (the core swap path).
  TEST_CASE("streaming: audio is continuous across buffer boundaries") {
    if (!TestHelpers::engineInit()) return;

    std::vector<float> src;
    std::string path = writeWav(2 * S + 10000, src); // spans two buffer boundaries
    soundFile f(path);
    f.create(true);
    REQUIRE(waitReady(f));

    Flt pos = 0.f, vol = 1.f;
    SOUND_STATUS intent = YSE::SS_PLAYING_FULL_VOLUME;
    std::vector<float> out;
    long frame = 0;
    const long verifyTo = 2 * S + 4000; // stays before EOF
    for (int b = 0; frame < verifyTo; ++b) {
      bool stopped = readBlock(f, pos, false, intent, vol, out);
      REQUIRE_FALSE(stopped);
      for (UInt j = 0; j < YSE::STANDARD_BUFFERSIZE && frame < static_cast<long>(src.size());
           ++j, ++frame) {
        CHECK(out[j] == doctest::Approx(src[static_cast<size_t>(frame)]).epsilon(0.0001));
      }
      breathe(b);
    }
  }

  // 2. A non-looping stream longer than one buffer plays its full tail, then stops.
  //    (The pre-#185 code dropped the final partial buffer — up to ~1 s of tail.)
  TEST_CASE("streaming: non-looping stream plays its full tail then stops") {
    if (!TestHelpers::engineInit()) return;

    const long r = 12345; // 0 < r < S and not a multiple of STANDARD_BUFFERSIZE
    std::vector<float> src;
    std::string path = writeWav(S + r, src);
    soundFile f(path);
    f.create(true);
    REQUIRE(waitReady(f));

    Flt pos = 0.f, vol = 1.f;
    SOUND_STATUS intent = YSE::SS_PLAYING_FULL_VOLUME;
    std::vector<float> out;
    long frame = 0;
    bool stopped = false;
    for (int b = 0; !stopped && b < 4000; ++b) {
      stopped = readBlock(f, pos, false, intent, vol, out);
      for (UInt j = 0; j < YSE::STANDARD_BUFFERSIZE; ++j) {
        if (frame < static_cast<long>(src.size())) {
          CHECK(out[j] == doctest::Approx(src[static_cast<size_t>(frame)]).epsilon(0.0001));
          ++frame;
        } else {
          CHECK(out[j] == 0.0f); // past true EOF: silence
        }
      }
      breathe(b);
    }

    CHECK(stopped);
    CHECK(frame == static_cast<long>(src.size())); // every real frame played — no dropped tail
  }

  // 3. A looping stream wraps seamlessly and keeps producing the right samples.
  TEST_CASE("streaming: looping stream wraps seamlessly") {
    if (!TestHelpers::engineInit()) return;

    const long len = S + 12345; // loop period straddling a buffer boundary
    std::vector<float> src;
    std::string path = writeWav(len, src);
    soundFile f(path);
    f.create(true);
    REQUIRE(waitReady(f));

    Flt pos = 0.f, vol = 1.f;
    SOUND_STATUS intent = YSE::SS_PLAYING_FULL_VOLUME;
    std::vector<float> out;
    long frame = 0;
    const long verifyTo = len + S + 4000; // read well past the first wrap
    for (int b = 0; frame < verifyTo; ++b) {
      bool stopped = readBlock(f, pos, true, intent, vol, out);
      REQUIRE_FALSE(stopped); // a looping stream never stops
      for (UInt j = 0; j < YSE::STANDARD_BUFFERSIZE && frame < verifyTo; ++j, ++frame) {
        CHECK(out[j] == doctest::Approx(src[static_cast<size_t>(frame % len)]).epsilon(0.0001));
      }
      breathe(b);
    }
  }

  // 4. Stopping a stream re-primes it; the next play starts from frame 0 again.
  TEST_CASE("streaming: stop re-primes so restart plays from the start") {
    if (!TestHelpers::engineInit()) return;

    std::vector<float> src;
    std::string path = writeWav(2 * S, src);
    soundFile f(path);
    f.create(true);
    REQUIRE(waitReady(f));

    Flt pos = 0.f, vol = 1.f;
    SOUND_STATUS intent = YSE::SS_PLAYING_FULL_VOLUME;
    std::vector<float> out;

    // Play past the first boundary (into buffer 1).
    for (int b = 0; b < 500; ++b) {
      readBlock(f, pos, false, intent, vol, out);
      breathe(b);
    }

    // Emulate the audio-thread stop action (mirrors dspFunc_parseIntent's
    // file->reset() on a paused→stopped streaming sound).
    f.reset();

    // Restart: playback must resume from frame 0.
    pos = 0.f;
    vol = 1.f;
    intent = YSE::SS_PLAYING_FULL_VOLUME;
    long frame = 0;
    for (int b = 0; b < 40; ++b) {
      bool stopped = readBlock(f, pos, false, intent, vol, out);
      REQUIRE_FALSE(stopped);
      for (UInt j = 0; j < YSE::STANDARD_BUFFERSIZE; ++j, ++frame) {
        CHECK(out[j] == doctest::Approx(src[static_cast<size_t>(frame)]).epsilon(0.0001));
      }
      breathe(b);
    }
  }

  // 6. Seeking a streaming sound to an absolute frame re-primes the stream from
  //    that frame (issue #217). Before the fix, setFilePos assigned an absolute
  //    frame to the buffer-local filePtr and never re-seeked the handle, so
  //    playback landed somewhere in the resident 1 s window instead of the target.
  TEST_CASE("streaming: seek re-primes the stream from the requested frame") {
    if (!TestHelpers::engineInit()) return;

    std::vector<float> src;
    std::string path = writeWav(3 * S, src); // long enough to seek well past buffer 0
    soundFile f(path);
    f.create(true);
    REQUIRE(waitReady(f));

    Flt pos = 0.f, vol = 1.f;
    SOUND_STATUS intent = YSE::SS_PLAYING_FULL_VOLUME;
    std::vector<float> out;

    // Play a little from the start so we are mid-stream, then seek forward to an
    // absolute frame that lies beyond the initially-primed front buffer.
    for (int b = 0; b < 4; ++b) {
      readBlock(f, pos, false, intent, vol, out);
      breathe(b);
    }

    const long target = 2 * S + 5000; // absolute frame, past buffer 0's window
    f.seek(target, false);
    // Mirror the audio thread: filePtr (pos) resets to the new front buffer start.
    pos = 0.f;
    vol = 1.f;
    intent = YSE::SS_PLAYING_FULL_VOLUME;

    // Playback must resume with the samples at `target`, sample-accurate.
    long frame = target;
    const long verifyTo = target + 3 * static_cast<long>(YSE::STANDARD_BUFFERSIZE);
    for (int b = 0; frame < verifyTo; ++b) {
      bool stopped = readBlock(f, pos, false, intent, vol, out);
      REQUIRE_FALSE(stopped);
      for (UInt j = 0; j < YSE::STANDARD_BUFFERSIZE && frame < verifyTo; ++j, ++frame) {
        CHECK(out[j] == doctest::Approx(src[static_cast<size_t>(frame)]).epsilon(0.0001));
      }
      breathe(b);
    }
  }

  // 7. Seeking backward (to a frame before the current window) also re-seeks the
  //    handle, and a subsequent stop/reset still returns to frame 0 — i.e. the
  //    seek target does not leak into the reset path (issue #217).
  TEST_CASE("streaming: seek backward then reset returns to frame 0") {
    if (!TestHelpers::engineInit()) return;

    std::vector<float> src;
    std::string path = writeWav(3 * S, src);
    soundFile f(path);
    f.create(true);
    REQUIRE(waitReady(f));

    Flt pos = 0.f, vol = 1.f;
    SOUND_STATUS intent = YSE::SS_PLAYING_FULL_VOLUME;
    std::vector<float> out;

    // Advance past the first buffer boundary.
    for (int b = 0; b < 400; ++b) {
      readBlock(f, pos, false, intent, vol, out);
      breathe(b);
    }

    // Seek back to a small non-zero frame and verify the samples there.
    const long target = 1234;
    f.seek(target, false);
    pos = 0.f;
    vol = 1.f;
    intent = YSE::SS_PLAYING_FULL_VOLUME;
    long frame = target;
    for (int b = 0; b < 8; ++b) {
      bool stopped = readBlock(f, pos, false, intent, vol, out);
      REQUIRE_FALSE(stopped);
      for (UInt j = 0; j < YSE::STANDARD_BUFFERSIZE; ++j, ++frame) {
        CHECK(out[j] == doctest::Approx(src[static_cast<size_t>(frame)]).epsilon(0.0001));
      }
      breathe(b);
    }

    // A stop/reset must still return to frame 0 (seek target cleared by reset()).
    f.reset();
    pos = 0.f;
    vol = 1.f;
    intent = YSE::SS_PLAYING_FULL_VOLUME;
    frame = 0;
    for (int b = 0; b < 8; ++b) {
      bool stopped = readBlock(f, pos, false, intent, vol, out);
      REQUIRE_FALSE(stopped);
      for (UInt j = 0; j < YSE::STANDARD_BUFFERSIZE; ++j, ++frame) {
        CHECK(out[j] == doctest::Approx(src[static_cast<size_t>(frame)]).epsilon(0.0001));
      }
      breathe(b);
    }
  }

  // 5. Destroying a streaming soundFile while a refill is in flight must not crash
  //    or touch freed memory (the dtor joins the refill job). Run under ASan/TSan
  //    in CI for the real teardown-race coverage.
  TEST_CASE("streaming: destruction with a refill in flight is clean") {
    if (!TestHelpers::engineInit()) return;

    std::vector<float> src;
    std::string path = writeWav(3 * S, src);

    for (int iter = 0; iter < 8; ++iter) {
      soundFile f(path);
      f.create(true);
      REQUIRE(waitReady(f)); // load finished; only the back-buffer refill may be in flight
      Flt pos = 0.f, vol = 1.f;
      SOUND_STATUS intent = YSE::SS_PLAYING_FULL_VOLUME;
      std::vector<YSE::DSP::buffer> fb(1);
      // One read schedules the buffer-1 refill; drop the file immediately so the
      // refill is very likely still queued/running when ~soundFile joins it.
      f.read(fb, pos, YSE::STANDARD_BUFFERSIZE, 1.0f, false, intent, vol);
    } // ~soundFile joins the refill job before freeing the handle/buffers

    CHECK(true); // reaching here without a crash / ASan report is the assertion
  }

} // TEST_SUITE("sound")

#endif // LIBSOUNDFILE_BACKEND
