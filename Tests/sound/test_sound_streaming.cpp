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
#include <functional>
#include <string>
#include <thread>
#include <vector>

#include "yse.hpp"
#include "internal/lsfSoundfile.h"
#include "internal/time.h"
#include "sound/soundManager.h"
#include "dsp/buffer.hpp"
#include "support/null_device.hpp"

using YSE::SOUND_STATUS;
using YSE::INTERNAL::soundFile;
// Flt / UInt / Bool are global typedefs (headers/types.hpp).

namespace {

  // A distinct, position-sensitive sample value for frame n. Using a hash makes a
  // mis-swap (replaying the wrong buffer) produce clearly wrong values.
  //
  // Exact 0.0f is deliberately excluded from the range. The engine signals an
  // underrun (and EOF) by zero-filling the *remainder* of the current block, so
  // readBlock() below uses a zero sample as the unambiguous marker for "the
  // stream did not produce this frame" (issue #674). One of the 65536 hash
  // buckets maps to 0.0, so over the ~100k frames these tests verify it would
  // otherwise come up a couple of times per run and trim a real frame.
  float sampleAt(long n) {
    uint32_t h = static_cast<uint32_t>(n) * 2654435761u;
    double v = ((h >> 8) & 0xFFFF) / 32768.0 - 1.0;
    if (v == 0.0) v = 1.0 / 32768.0; // never emit the silence marker as audio
    return static_cast<float>(v);
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

  // What one STANDARD_BUFFERSIZE block produced.
  struct blockResult {
    UInt valid = 0; // frames of real stream audio, at the front of the block
    bool stopped = false; // the stream reached EOF and stopped during this block
  };

  // Frames of real audio in `out`: the block minus its trailing run of silence.
  //
  // A stream-buffer boundary is not block-aligned in general, so when the
  // prefetched buffer has not landed the engine plays the frames it still has
  // and zero-fills only the *rest* of the block (abstractSoundFile.cpp,
  // `calibrate`) — a partially valid block. EOF ends a block the same way. Since
  // sampleAt() never yields exactly 0.0f, a zero sample is unambiguously
  // engine-emitted silence, so the trailing zero run is exactly the part of the
  // block the stream did not fill. (issue #674)
  UInt validFrames(const std::vector<float>& out) {
    UInt n = YSE::STANDARD_BUFFERSIZE;
    while (n > 0 && out[static_cast<size_t>(n - 1)] == 0.0f)
      --n;
    return n;
  }

  // Read one STANDARD_BUFFERSIZE block, transparently retrying a *total* underrun
  // (a fully silent block while still playing) after giving the slow pool time to
  // land the refill. A partial underrun is not retried — the frames before it are
  // real audio the stream has already advanced past — so callers must advance
  // their own frame counter by `valid`, not by the block size.
  blockResult readBlock(YSE::INTERNAL::abstractSoundFile& f, Flt& pos, Bool loop,
                        SOUND_STATUS& intent, Flt& vol, std::vector<float>& out) {
    for (int retry = 0; retry < 500; ++retry) {
      std::vector<YSE::DSP::buffer> fb(1); // one mono output buffer of length STANDARD_BUFFERSIZE
      f.read(fb, pos, YSE::STANDARD_BUFFERSIZE, 1.0f, loop, intent, vol);
      const Flt* p = fb[0].getPtr();
      out.assign(p, p + YSE::STANDARD_BUFFERSIZE);
      blockResult r{validFrames(out), intent == YSE::SS_STOPPED};
      if (r.stopped || r.valid > 0) return r;
      std::this_thread::sleep_for(std::chrono::milliseconds(3)); // underrun: let the refill land
    }
    return {}; // the refill never landed
  }

  // Small periodic pause so the slow pool always fills the next buffer well before
  // the play cursor reaches it — keeps the tests free of (allowed but timing-
  // dependent) underruns so frame accounting stays exact.
  void breathe(int block) {
    if ((block & 7) == 0) std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }

  // Settle the engine's *process-global* soundFile population before a test
  // measures soundFile::liveInstances(), and leave it settled for the length of
  // that measurement.
  //
  // liveInstances() counts every abstractSoundFile in the process, and the sound
  // manager reclaims shared soundFiles asynchronously: update() hands a garbage-
  // collection pass to the slow pool once the ticks it has seen add up to a
  // second, and that pass ages every client-less file by the wall time since the
  // previous pass, erasing the ones idle for more than 30 s. Both the throttle
  // and the ageing are driven by wall time, not by the caller.
  //
  // In the monolithic test binary this is what made the #218 teardown case flake
  // (issue #673): no other suite ticks the sound manager, so the case's own first
  // Time().update() carried a multi-second delta, which armed a GC pass with an
  // equally large dt on its very first Manager().update(). That pass reclaimed a
  // file an earlier suite had left idle, so the global count dropped by one
  // *inside* the measurement window and the count-went-up assertion saw no
  // change. In the per-suite `yse_tests_sound` process the gap is short and no
  // straggler is near the threshold, which is why only the monolithic run failed.
  //
  // Feeding that whole accumulated gap to the engine *before* the baseline fixes
  // it deterministically. A single tick longer than the throttle period is
  // guaranteed to cross it whatever the manager had already accumulated, so the
  // throttle resets to zero and the pass it arms runs here instead of later.
  // Waiting for that pass to finish without ticking again (nothing new can be
  // armed while the manager is not being updated) leaves the count settled, and
  // the closing Time().update() discards the wait so the caller starts with the
  // full one-second GC-free budget — far more than the ~0.3 s of ticks the
  // measurement below spends.
  void settleSoundFilePopulation() {
    using clock = std::chrono::steady_clock;

    // Longer than the manager's one-second GC throttle, so this single tick
    // crosses it from any starting point and resets it.
    std::this_thread::sleep_for(std::chrono::milliseconds(1100));
    YSE::INTERNAL::Time().update();
    YSE::SOUND::Manager().update(); // arms the GC pass; zeroes the throttle

    // The pass runs on the slow pool. Wait for the count to stop moving, which
    // also covers any impl teardown an earlier case left in flight. Deliberately
    // no Manager().update() in this loop: a second update() would re-add the
    // same large delta and re-arm the GC behind the caller's back.
    long last = soundFile::liveInstances();
    auto stableSince = clock::now();
    const auto deadline = stableSince + std::chrono::seconds(3);
    while (clock::now() < deadline) {
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
      const long now = soundFile::liveInstances();
      if (now != last) {
        last = now;
        stableSince = clock::now();
      } else if (clock::now() - stableSince > std::chrono::milliseconds(200)) {
        break;
      }
    }

    // Absorb the wait itself, so the caller's first tick reports only its own
    // elapsed time against the manager's GC throttle.
    YSE::INTERNAL::Time().update();
  }

  const long S = static_cast<long>(YSE::STREAM_BUFFERSIZE); // 44100

  // Minimal in-memory backend for abstractSoundFile so the stale-generation
  // race of issue #283 can be reproduced deterministically. The production
  // publication protocol (abstractSoundFile::fillBackBuffer) is exercised
  // unchanged; only the blocking disk fill is replaced by an emulated file
  // handle, with a hook at the exact interleaving point: after fillBackBuffer()
  // captured the fill generation, before the fill consumes the reset flag —
  // where the audio thread's reset()/seek() races in.
  class staleGenFile : public YSE::INTERNAL::abstractSoundFile {
  public:
    staleGenFile() : abstractSoundFile("stale-gen-probe", true) {
      _length = 10 * static_cast<int>(S); // so seek() targets aren't clamped
    }
    void loadStreaming() override {}
    void loadNonStreaming() override {}

    std::function<void()> onFillStart; // runs once, at the race point
    Long handlePos = S; // emulated handle position (front buffer 0 already read)
    Long lastFillStart = -1; // where the most recent fill read from
    int seeks = 0;

    UInt fillBuffer(Flt* /*dest*/, Bool /*loop*/) override {
      if (onFillStart) {
        auto hook = std::move(onFillStart);
        onFillStart = nullptr;
        hook();
      }
      // Mirror the production backend's reset handling (lsfSoundfile.cpp).
      if (_needsReset.exchange(false, std::memory_order_relaxed)) {
        handlePos = _seekTarget.load(std::memory_order_relaxed);
        ++seeks;
      }
      lastFillStart = handlePos;
      handlePos += S;
      return YSE::STREAM_BUFFERSIZE;
    }

    // Drive one back-buffer fill the way the slow pool does: requestRefill()
    // marks the refill in flight before the job runs fillBackBuffer().
    void runFill() {
      _refillInFlight.store(true, std::memory_order_relaxed);
      fillBackBuffer();
    }

    bool needsReset() {
      return _needsReset.load(std::memory_order_relaxed);
    }
    bool backIsStale() {
      return _backGen.load(std::memory_order_relaxed) != _fillGen.load(std::memory_order_relaxed);
    }
    // What streamSwap does with a stale-generation back buffer: discard it.
    void discardStaleBack() {
      _backReady.store(false, std::memory_order_relaxed);
    }
  };

  // In-memory streaming backend that reproduces a *partial* underrun on demand
  // (issue #674). The stream-buffer boundary at frame STREAM_BUFFERSIZE is not a
  // multiple of STANDARD_BUFFERSIZE, so the block that straddles it can only be
  // half filled; withholding the back buffer at exactly that point makes the
  // engine emit that partially silent block deterministically, with no disk, no
  // slow pool and no timing involved.
  class underrunFile : public YSE::INTERNAL::abstractSoundFile {
  public:
    explicit underrunFile(long frames) : abstractSoundFile("underrun-probe", true) {
      _streaming = true;
      _endReached = false;
      _channels = 1;
      _length = static_cast<int>(frames);
      _sampleRateAdjustment = 1.f;
      _iBuffer = new Flt[static_cast<size_t>(S)];
      _iBufferBack = new Flt[static_cast<size_t>(S)];
      // requestRefill() only queues a slow-pool job when it can flip
      // _refillInFlight from false, so latching it here means read() never
      // schedules one and this fixture is the sole publisher of a back buffer.
      _refillInFlight.store(true, std::memory_order_relaxed);
      fillFrom(_iBuffer, 0); // prime the front buffer with frames [0, S)
      _frontBufferBase = 0;
      _frontValidFrames = S;
      _frontTerminal = false;
      state = YSE::INTERNAL::READY;
    }
    ~underrunFile() override {
      delete[] _iBuffer;
      delete[] _iBufferBack;
      _iBuffer = nullptr;
      _iBufferBack = nullptr;
    }
    underrunFile(const underrunFile&) = delete;
    underrunFile& operator=(const underrunFile&) = delete;

    void loadStreaming() override {}
    void loadNonStreaming() override {}

    UInt fillBuffer(Flt* dest, Bool /*loop*/) override {
      fillFrom(dest, _nextFill);
      _nextFill += S;
      return YSE::STREAM_BUFFERSIZE;
    }

    // Publish the next stream buffer exactly the way fillBackBuffer() does, so
    // the audio thread's next streamSwap() accepts it.
    void publishBack() {
      fillFrom(_iBufferBack, _nextFill);
      _nextFill += S;
      _backValidFrames.store(S, std::memory_order_relaxed);
      _backTerminal.store(false, std::memory_order_relaxed);
      _backGen.store(_fillGen.load(std::memory_order_relaxed), std::memory_order_relaxed);
      _backReady.store(true, std::memory_order_release);
    }

  private:
    static void fillFrom(Flt* dest, long start) {
      for (long i = 0; i < S; ++i)
        dest[i] = sampleAt(start + i);
    }
    long _nextFill = S; // absolute frame the next fill starts at
  };

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
      blockResult r = readBlock(f, pos, false, intent, vol, out);
      REQUIRE_FALSE(r.stopped);
      REQUIRE(r.valid > 0); // no frames at all: the refill never landed
      for (UInt j = 0; j < r.valid && frame < static_cast<long>(src.size()); ++j, ++frame) {
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
      blockResult r = readBlock(f, pos, false, intent, vol, out);
      stopped = r.stopped;
      REQUIRE((r.valid > 0 || stopped)); // no frames and not stopped: the stream is stuck
      for (UInt j = 0; j < r.valid; ++j, ++frame) {
        // Silence past true EOF is what ends the block, so any frame counted
        // here must have a source frame behind it.
        REQUIRE(frame < static_cast<long>(src.size()));
        CHECK(out[j] == doctest::Approx(src[static_cast<size_t>(frame)]).epsilon(0.0001));
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
      blockResult r = readBlock(f, pos, true, intent, vol, out);
      REQUIRE_FALSE(r.stopped); // a looping stream never stops
      REQUIRE(r.valid > 0); // no frames at all: the refill never landed
      for (UInt j = 0; j < r.valid && frame < verifyTo; ++j, ++frame) {
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
      blockResult r = readBlock(f, pos, false, intent, vol, out);
      REQUIRE_FALSE(r.stopped);
      REQUIRE(r.valid > 0);
      for (UInt j = 0; j < r.valid; ++j, ++frame) {
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
      blockResult r = readBlock(f, pos, false, intent, vol, out);
      REQUIRE_FALSE(r.stopped);
      REQUIRE(r.valid > 0);
      for (UInt j = 0; j < r.valid && frame < verifyTo; ++j, ++frame) {
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
      blockResult r = readBlock(f, pos, false, intent, vol, out);
      REQUIRE_FALSE(r.stopped);
      REQUIRE(r.valid > 0);
      for (UInt j = 0; j < r.valid; ++j, ++frame) {
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
      blockResult r = readBlock(f, pos, false, intent, vol, out);
      REQUIRE_FALSE(r.stopped);
      REQUIRE(r.valid > 0);
      for (UInt j = 0; j < r.valid; ++j, ++frame) {
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

  // 8. A streaming sound owns its per-impl soundFile directly (allocated with
  //    `new` in implementationObject::create). Before issue #218,
  //    ~implementationObject never deleted that owned file, so every streaming
  //    sound leaked its soundFile (handle + two ~172 KB stream buffers) for the
  //    process lifetime. Drive one streaming sound through the full public-API
  //    lifecycle and assert the live-soundFile count returns to its baseline
  //    once the slow-pool deleteJob has torn the impl down. Without the fix the
  //    count stays at baseline + 1 and the final CHECK fails.
  TEST_CASE("streaming: per-impl soundFile is freed on teardown (issue #218)") {
    if (!TestHelpers::engineInit()) return;

    std::vector<float> src;
    std::string path = writeWav(2 * S, src);

    // liveInstances() is process-global and the manager's idle-file GC can drop
    // it at any moment, so settle that population first — otherwise the baseline
    // is a snapshot of files an earlier suite is about to have reclaimed, and the
    // reclaim lands mid-measurement (issue #673).
    settleSoundFilePopulation();
    const long baseline = soundFile::liveInstances();

    {
      YSE::sound s;
      s.create(path.c_str(), nullptr, false, 1.0f, /*streaming*/ true);
      // create() allocates the owned streaming soundFile synchronously, so one
      // extra instance is live immediately.
      CHECK(soundFile::liveInstances() == baseline + 1);
      // Pump the manager so the impl is set up and promoted to inUse (the audio
      // thread is paused under engineInit, so the test thread drives update()).
      for (int i = 0; i < 15; ++i) {
        YSE::INTERNAL::Time().update();
        YSE::SOUND::Manager().update();
        std::this_thread::sleep_for(std::chrono::milliseconds(3));
      }
      CHECK(soundFile::liveInstances() == baseline + 1);
    } // ~sound() nulls the head; the next sync() releases the impl.

    // Pump until the slow-pool deleteJob has run ~implementationObject (which,
    // with the fix, deletes the owned streaming file). Poll with a deadline so
    // the test doesn't depend on an exact iteration count for async teardown.
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (std::chrono::steady_clock::now() < deadline && soundFile::liveInstances() != baseline) {
      YSE::INTERNAL::Time().update();
      YSE::SOUND::Manager().update();
      std::this_thread::sleep_for(std::chrono::milliseconds(3));
    }

    CHECK(soundFile::liveInstances() == baseline);
  }

  // 9. Issue #283 regression: a stop (reset) that lands after an in-flight fill
  //    captured its generation but before that fill consumed the reset flag must
  //    not be swallowed. The stale fill's output is (rightly) discarded, so the
  //    reset must be re-armed for the accepted fill — otherwise the restart
  //    resumes STREAM_BUFFERSIZE frames late. Reproduced deterministically with
  //    an in-memory backend; no engine or slow pool involved.
  TEST_CASE("streaming: reset consumed by a stale-generation fill is re-armed (issue #283)") {
    staleGenFile f;

    // F1 starts; the audio thread's stop lands at the race point.
    f.onFillStart = [&f] { f.reset(); };
    f.runFill(); // F1
    CHECK(f.seeks == 1); // F1 consumed the reset and seeked to 0...
    CHECK(f.lastFillStart == 0);
    REQUIRE(f.backIsStale()); // ...but its output is stale-tagged
    f.discardStaleBack(); // and streamSwap discards it

    // The reset must have been re-armed so the accepted fill F2 seeks again.
    CHECK(f.needsReset()); // without the fix: false (F1 swallowed the reset)
    f.runFill(); // F2 — the fill whose output is accepted
    CHECK(f.lastFillStart == 0); // without the fix: S (wrong file position)
    CHECK_FALSE(f.backIsStale());
  }

  // 10. Same interleaving with a non-zero seek target (the issue-#217 path):
  //     the re-armed reset must land the accepted fill on the requested frame,
  //     since _seekTarget is not consumed by the reset exchange.
  TEST_CASE("streaming: seek consumed by a stale-generation fill is re-armed (issue #283)") {
    staleGenFile f;

    const Long target = 4321;
    f.onFillStart = [&f, target] { f.seek(target, false); };
    f.runFill(); // F1 — consumes the seek, output stale-tagged
    CHECK(f.lastFillStart == target);
    REQUIRE(f.backIsStale());
    f.discardStaleBack();

    CHECK(f.needsReset()); // re-armed; _seekTarget still holds `target`
    f.runFill(); // F2
    CHECK(f.lastFillStart == target); // without the fix: target + S
    CHECK_FALSE(f.backIsStale());
  }

  // 11. Issue #674 regression: an underrun that lands mid-block. STREAM_BUFFERSIZE
  //     is not a multiple of STANDARD_BUFFERSIZE, so the block straddling a
  //     stream-buffer boundary carries only the frames up to that boundary and is
  //     zero-filled from there. The harness must count just those frames: the old
  //     retry heuristic only recognised a *fully* silent block, so it accepted the
  //     partial one as a whole block of audio, advanced its frame counter over
  //     samples the stream never produced, and every later comparison in the case
  //     failed. Driven by an in-memory backend, so the desync reproduces without
  //     depending on how loaded the machine is.
  TEST_CASE("streaming: a partial-block underrun does not desync frame accounting (issue #674)") {
    underrunFile f(4 * S); // long enough that EOF is never in play here

    const UInt block = YSE::STANDARD_BUFFERSIZE;
    const long straddling = S / block; // index of the block containing the boundary
    const UInt head = static_cast<UInt>(S % block); // frames of it that are real audio
    REQUIRE(head != 0); // a block-aligned boundary would make this case vacuous

    Flt pos = 0.f, vol = 1.f;
    SOUND_STATUS intent = YSE::SS_PLAYING_FULL_VOLUME;
    std::vector<float> out;

    // Everything before the boundary comes out of the primed front buffer.
    long frame = 0;
    for (long b = 0; b < straddling; ++b) {
      blockResult r = readBlock(f, pos, false, intent, vol, out);
      REQUIRE_FALSE(r.stopped);
      REQUIRE(r.valid == block);
      frame += block;
    }

    // The straddling block: `head` real frames, then silence, because the back
    // buffer has deliberately not been published yet.
    blockResult r = readBlock(f, pos, false, intent, vol, out);
    REQUIRE_FALSE(r.stopped);
    CHECK(r.valid == head); // the defect: the old harness counted a full block here
    // ...and it could not have noticed, because the block is not all zero — which
    // was the only underrun signal it looked for.
    CHECK_FALSE(std::all_of(out.begin(), out.end(), [](float v) { return v == 0.0f; }));
    for (UInt j = 0; j < r.valid; ++j, ++frame)
      CHECK(out[j] == doctest::Approx(sampleAt(frame)).epsilon(0.0001));
    for (UInt j = r.valid; j < block; ++j)
      CHECK(out[j] == 0.0f);
    CHECK(frame == S); // exactly one stream buffer consumed, not one buffer + a bit

    // Land the refill. Playback resumes at frame S — not at S + (block - head),
    // which is where a frame counter that swallowed the silence would be looking.
    f.publishBack();
    r = readBlock(f, pos, false, intent, vol, out);
    REQUIRE_FALSE(r.stopped);
    REQUIRE(r.valid == block);
    for (UInt j = 0; j < r.valid; ++j, ++frame)
      CHECK(out[j] == doctest::Approx(sampleAt(frame)).epsilon(0.0001));
  }

} // TEST_SUITE("sound")

#endif // LIBSOUNDFILE_BACKEND
