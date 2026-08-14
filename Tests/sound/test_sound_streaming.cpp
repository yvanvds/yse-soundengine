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
#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <limits>
#include <string>
#include <thread>
#include <vector>

#include "yse.hpp"
#include "internal/global.h"
#include "internal/lsfSoundfile.h"
#include "internal/threadPool.h"
#include "internal/time.h"
#include "sound/soundManager.h"
#include "dsp/buffer.hpp"
#include "support/null_device.hpp"
#include "support/timer_pacing.hpp"

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

  // Read a whole file into memory, so a WAV written by writeWav() can be handed
  // to the in-memory VFS (BufferIO) and opened through the custom-IO callbacks.
  std::vector<char> readAllBytes(const std::string& path) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f.is_open()) return {};
    const auto size = static_cast<std::streamsize>(f.tellg());
    f.seekg(0);
    std::vector<char> bytes(static_cast<size_t>(size));
    f.read(bytes.data(), size);
    return bytes;
  }

  // Wait for the slow-pool load to finish (state == READY) or run out of budget.
  // The budget is counted in ticks of the suite's pacing reference rather than
  // in wall clock, so a loaded box stretches it by exactly as much as it
  // stretches the pool (issue #753); a wedged pool still fails.
  bool waitReady(soundFile& f) {
    return TestHelpers::pacedUntil(3000, [&f] { return f.getState() == YSE::INTERNAL::READY; });
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
  //
  // Each retry waits out a window of the suite's pacing reference instead of a
  // fixed 3 ms, so the retry budget grows with the machine's load rather than
  // expiring on it (issue #753). The iteration count is kept: a retry is a read()
  // of its own, so the loop is the number of reads the refill gets, not a clock.
  blockResult readBlock(YSE::INTERNAL::abstractSoundFile& f, Flt& pos, Bool loop,
                        SOUND_STATUS& intent, Flt& vol, std::vector<float>& out) {
    for (int retry = 0; retry < 500; ++retry) {
      std::vector<YSE::DSP::buffer> fb(1); // one mono output buffer of length STANDARD_BUFFERSIZE
      f.read(fb, pos, YSE::STANDARD_BUFFERSIZE, 1.0f, loop, intent, vol);
      const Flt* p = fb[0].getPtr();
      out.assign(p, p + YSE::STANDARD_BUFFERSIZE);
      blockResult r{validFrames(out), intent == YSE::SS_STOPPED};
      if (r.stopped || r.valid > 0) return r;
      TestHelpers::paceWindow(3); // underrun: let the refill land
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
  //
  // The opening sleep stays denominated in wall clock: it is measured against
  // the manager's own wall-clock throttle, so its elapsed time is the whole
  // point of it (issue #753). The settle wait after it is not about wall time —
  // it waits on the slow pool — so its budget is counted in ticks of the
  // suite's pacing reference, which stretches with the load the pool is under.
  void settleSoundFilePopulation() {
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
    long stableSince = TestHelpers::paceTicks();
    TestHelpers::pacedUntil(3000, [&] {
      const long now = soundFile::liveInstances();
      if (now != last) {
        last = now;
        stableSince = TestHelpers::paceTicks();
        return false;
      }
      return TestHelpers::paceTicks() - stableSince > 200;
    });

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
    // marks the refill in flight and queues the job (start(), inside
    // threadPool::addJob) before a worker activate()s it — run() plus the
    // trailing isDone/inQueue bookkeeping. Arming the job is load-bearing
    // since issue #844: a claim held with nothing queued is the leaked state
    // requestRefill now self-heals, so a hook that re-enters it mid-fill
    // (reset()/seek()) would otherwise release the claim and schedule a real
    // slow-pool fill behind this deterministic choreography's back.
    void runFill() {
      _refillInFlight.store(true, std::memory_order_relaxed);
      _refillJob.start();
      _refillJob.activate();
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
      // Since issue #844 the claim alone is not enough: claimed-but-not-queued
      // is the leaked state requestRefill self-heals, so the job must read as
      // queued too — together they model a live, never-completing fill cycle,
      // which requestRefill leaves alone.
      _refillInFlight.store(true, std::memory_order_relaxed);
      _refillJob.start();
      fillFrom(_iBuffer, 0); // prime the front buffer with frames [0, S)
      _frontBufferBase = 0;
      _frontValidFrames = S;
      _frontTerminal = false;
      state = YSE::INTERNAL::READY;
    }
    ~underrunFile() override {
      // Release the never-run pseudo-queued refill job the ctor armed, or the
      // member ~threadPoolJob's join() would wait on inQueue forever. stop() +
      // activate() takes the released-without-running path (issue #285): the
      // flags are cleared, run() is not called.
      _refillJob.stop();
      _refillJob.activate();
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

  // In-memory streaming backend for the refill-scheduling race of issue #841.
  //
  // requestRefill() used to decide in two separate steps: check _backReady,
  // then claim _refillInFlight. A fill that completed *between* those steps —
  // publishing the back buffer and releasing the in-flight flag — left both
  // reads answering "schedule", so the audio thread queued a second fill that
  // overwrote the published-but-unconsumed back buffer. At the next swap
  // playback skipped exactly one STREAM_BUFFERSIZE of audio (the flake this
  // case pins down failed from frame 2*S onward with every sample offset by
  // +S). The window is two adjacent instructions wide, so this fixture
  // maximises how often a fill completion walks across it: fills are instant
  // memory stamps trailed by a random-length spin (each publication lands at a
  // random phase of the reader's loop), and the case reads tiny blocks so the
  // reader crosses the check-then-claim window as often as possible.
  class refillRaceFile : public YSE::INTERNAL::abstractSoundFile {
  public:
    refillRaceFile() : abstractSoundFile("refill-race-probe", true) {
      _streaming = true;
      _endReached = false;
      _channels = 1;
      _length = std::numeric_limits<Int>::max(); // looping forever; EOF never in play
      _sampleRateAdjustment = 1.f;
      _iBuffer = new Flt[static_cast<size_t>(S)];
      _iBufferBack = new Flt[static_cast<size_t>(S)];
      stamp(_iBuffer); // prime the front buffer with frames [0, S)
      _frontBufferBase = 0;
      _frontValidFrames = S;
      _frontTerminal = false;
      state = YSE::INTERNAL::READY;
    }
    ~refillRaceFile() override {
      // A refill may still be queued or running on the slow pool (cfr.
      // ~soundFile); let it finish before freeing the buffers it writes.
      _refillJob.join();
      delete[] _iBuffer;
      delete[] _iBufferBack;
      _iBuffer = nullptr;
      _iBufferBack = nullptr;
    }
    refillRaceFile(const refillRaceFile&) = delete;
    refillRaceFile& operator=(const refillRaceFile&) = delete;

    void loadStreaming() override {}
    void loadNonStreaming() override {}

    // Fills that began while a published back buffer was still unconsumed.
    // Such a fill is exactly the #841 defect: it overwrites that buffer and
    // playback skips one stream buffer of audio. Must stay 0.
    std::atomic<long> overwrites{0};
    // Total completed fills — the number of chances the race had.
    std::atomic<long> fills{0};

    // The stream's position-coded sample for absolute frame n. 1-based so 0
    // stays the engine's underrun/silence marker (see validFrames above).
    static Flt marker(long long n) {
      return static_cast<Flt>(1 + (n % 1000000));
    }

    UInt fillBuffer(Flt* dest, Bool /*loop*/) override {
      if (_backReady.load(std::memory_order_acquire))
        overwrites.fetch_add(1, std::memory_order_relaxed);
      // Mirror the production backend's reset handling (lsfSoundfile.cpp);
      // nothing arms it in this case, but consuming it keeps the protocol real.
      if (_needsReset.exchange(false, std::memory_order_relaxed))
        _nextFrame = _seekTarget.load(std::memory_order_relaxed);
      stamp(dest);
      // Random-length spin so the publication that follows this return (in
      // fillBackBuffer) lands at a uniformly random phase of the reader loop.
      volatile uint32_t sink = 0;
      for (uint32_t i = nextRand() & 0x3FF; i > 0; --i)
        sink += i;
      fills.fetch_add(1, std::memory_order_relaxed);
      return YSE::STREAM_BUFFERSIZE;
    }

  private:
    void stamp(Flt* dest) {
      for (long i = 0; i < S; ++i)
        dest[i] = marker(_nextFrame + i);
      _nextFrame += S;
    }
    uint32_t nextRand() { // xorshift32; only ever called from the fill thread
      _rng ^= _rng << 13;
      _rng ^= _rng >> 17;
      _rng ^= _rng << 5;
      return _rng;
    }
    long long _nextFrame = 0; // absolute frame the next stamp starts at
    uint32_t _rng = 0x9E3779B9u;
  };

  // In-memory streaming backend for the refill-job lifecycle races of issue
  // #844. Unlike refillRaceFile it carries no marker stream: its fills are
  // instant no-ops, and it instead exposes the refill protocol's internals so
  // the test driver (playing the audio thread's role, cfr. the suite header)
  // can chase the slow-pool worker across the window between fillBackBuffer()
  // releasing _refillInFlight and activate() finishing the job's isDone /
  // inQueue bookkeeping.
  class rearmProbeFile : public YSE::INTERNAL::abstractSoundFile {
  public:
    rearmProbeFile() : abstractSoundFile("rearm-probe", true) {
      _streaming = true;
      _endReached = false;
      _channels = 1;
      _length = std::numeric_limits<Int>::max(); // looping forever; EOF never in play
      _sampleRateAdjustment = 1.f;
      _iBuffer = new Flt[static_cast<size_t>(S)];
      _iBufferBack = new Flt[static_cast<size_t>(S)];
      _frontBufferBase = 0;
      _frontValidFrames = S;
      _frontTerminal = false;
      state = YSE::INTERNAL::READY;
    }
    ~rearmProbeFile() override {
      // A refill may still be queued or running on the slow pool (cfr.
      // ~soundFile); let it finish before freeing the buffers it writes.
      _refillJob.join();
      delete[] _iBuffer;
      delete[] _iBufferBack;
      _iBuffer = nullptr;
      _iBufferBack = nullptr;
    }
    rearmProbeFile(const rearmProbeFile&) = delete;
    rearmProbeFile& operator=(const rearmProbeFile&) = delete;

    void loadStreaming() override {}
    void loadNonStreaming() override {}

    // Total completed fills — the driver's progress oracle.
    std::atomic<long> fills{0};

    UInt fillBuffer(Flt* dest, Bool /*loop*/) override {
      (void)dest;
      // Mirror the production backend's reset handling (lsfSoundfile.cpp);
      // nothing arms it in this case, but consuming it keeps the protocol real.
      if (_needsReset.exchange(false, std::memory_order_relaxed))
        (void)_seekTarget.load(std::memory_order_relaxed);
      fills.fetch_add(1, std::memory_order_relaxed);
      return YSE::STREAM_BUFFERSIZE;
    }

    // --- audio-side driver hooks (the test thread plays the audio thread) ---
    // = read()'s per-block requestRefill call.
    void pump() {
      requestRefill(true);
    }
    bool backReady() const {
      return _backReady.load(std::memory_order_acquire);
    }
    // = streamSwap()'s consumption of the published back buffer.
    void consumeBack() {
      _backReady.store(false, std::memory_order_relaxed);
    }
    bool refillClaimed() const {
      return _refillInFlight.load(std::memory_order_acquire);
    }
    bool refillQueued() {
      return _refillJob.isQueued();
    }
    // Recreate exactly the state a dropped or drained enqueue leaves behind
    // (threadPool::addJob on an inactive pool / background-ring overflow, or
    // shutdown()'s ring drain): the claim set, nothing queued, nothing running.
    void leakClaim() {
      _refillInFlight.store(true, std::memory_order_release);
    }
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
        TestHelpers::paceWindow(3);
      }
      CHECK(soundFile::liveInstances() == baseline + 1);
    } // ~sound() nulls the head; the next sync() releases the impl.

    // Pump until the slow-pool deleteJob has run ~implementationObject (which,
    // with the fix, deletes the owned streaming file). Poll on a budget so the
    // test doesn't depend on an exact iteration count for async teardown — a
    // budget in pacing-reference ticks, so a loaded box gets proportionally
    // longer to run that job (issue #753).
    TestHelpers::pacedPump(
        3000, [&] { return soundFile::liveInstances() == baseline; },
        [] {
          YSE::INTERNAL::Time().update();
          YSE::SOUND::Manager().update();
        },
        3);

    CHECK(soundFile::liveInstances() == baseline);
  }

  // 8b. Issue #819 regression: a streaming sound must not be published before
  //     its loader has described the source.
  //
  //     `create(..., streaming = true)` only *schedules* the open on the slow
  //     pool; soundFile::loadStreaming() is what assigns the channel count and
  //     the frame length, publishing both with the release store to `state`.
  //     Two readers used to jump the gun: create() itself, right after
  //     addSlowJob (the data race TSan reports), and setup(), which had a
  //     "streaming sounds do not have to wait until loaded" branch that read
  //     channels()/length() whatever the state was. When setup() won, the sound
  //     was published with filebuffer.resize(0) — no output buffers at all, the
  //     #657 failure mode — and length 0, permanently: readyCheck() promotes it
  //     to OBJECT_READY and setup() never runs again.
  //
  //     Make that ordering instead of waiting for it. The background pool has
  //     exactly one worker, so parking it in a blocker job pins the order of
  //     everything queued behind it: the setup job is armed (by an update()
  //     with a non-empty toLoad) *before* the streaming sound's load job is
  //     queued, while the streaming impl is already OBJECT_CREATED and so
  //     claimable by that setup pass. On the unfixed code the sound comes out
  //     ready with length 0; with the fix setup() falls through until the file
  //     reaches READY and the sound reports the real source length.
  TEST_CASE("streaming: setup that beats the loader must not publish a zero-length sound (#819)") {
    if (!TestHelpers::engineInit()) return;

    struct Blocker : YSE::INTERNAL::threadPoolJob {
      std::atomic<bool> running{false};
      std::atomic<bool> release{false};
      void run() override {
        running.store(true, std::memory_order_release);
        while (!release.load(std::memory_order_acquire))
          std::this_thread::yield();
      }
    };

    const long frames = 2 * S + 1234; // a length no other case in this TU writes
    std::vector<float> src;
    const std::string path = writeWav(frames, src);

    // Drain whatever an earlier case left in flight, so the setup job is not
    // already queued (update() skips arming it then) and the worker is idle.
    for (int i = 0; i < 10; ++i) {
      YSE::INTERNAL::Time().update();
      YSE::SOUND::Manager().update();
      TestHelpers::paceWindow(3);
    }

    Blocker blocker;
    YSE::INTERNAL::Global().addSlowJob(&blocker);
    TestHelpers::pacedUntil(5000, [&] { return blocker.running.load(std::memory_order_acquire); });
    REQUIRE(blocker.running.load(std::memory_order_acquire));

    // A plain (non-streaming) sound purely to put something in `toLoad`, so the
    // update() below arms the manager's setup job behind the blocker.
    YSE::sound plain;
    plain.create(path.c_str(), nullptr, false, 1.0f, /*streaming*/ false);
    YSE::INTERNAL::Time().update();
    YSE::SOUND::Manager().update(); // queues the setup job: [blocker, ..., setup]

    // Now the streaming sound. Its load job lands *after* the setup job in the
    // one-worker queue, but SOUND::Manager::setup() has already flagged the impl
    // OBJECT_CREATED, so the setup pass will claim it while the file is still
    // FILESTATE::LOADING — the exact interleaving of issue #819.
    YSE::sound stream;
    stream.create(path.c_str(), nullptr, false, 1.0f, /*streaming*/ true);

    blocker.release.store(true, std::memory_order_release);

    TestHelpers::pacedPump(
        5000, [&] { return stream.isReady(); },
        [] {
          YSE::INTERNAL::Time().update();
          YSE::SOUND::Manager().update();
        },
        3);

    CHECK(stream.isReady());
    CHECK(stream.length() == static_cast<UInt>(frames));
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

  // 12. Issue #823 regression: a streaming source opened through a custom file
  //     reader (YSE::IO(), here driven by the in-memory BufferIO VFS) must load
  //     like any other stream.
  //
  //     soundFile::loadStreaming() had an empty custom-IO branch, so the load job
  //     returned with the file still at FILESTATE::LOADING — no handle, no channel
  //     count, no length, and no INVALID verdict either. Nothing ever moved it on,
  //     because run() is what does the loading. Until #819 that was masked at the
  //     next layer up (setup() published streaming sounds whatever the file state
  //     was, giving a silent zero-length sound); with streaming now gated on READY
  //     the file simply never becomes playable.
  //
  //     The custom reader supplies open/read/seek/tell/length, which is exactly
  //     what sndfile's virtual IO needs, so the stream is opened through it and the
  //     reader handle is held until ~soundFile. Assert the description the loader
  //     publishes *and* that the slow pool can keep reading through the callbacks
  //     across a stream-buffer boundary — a handle closed too early (the ownership
  //     half of the fix) still reaches READY but dies on the first refill.
  TEST_CASE("streaming: a custom-IO source loads and streams (issue #823)") {
    if (!TestHelpers::engineInit()) return;

    std::vector<float> src;
    const std::string path = writeWav(S + 20000, src); // spans one buffer boundary
    std::vector<char> bytes = readAllBytes(path);
    REQUIRE_FALSE(bytes.empty());

    YSE::BufferIO io;
    io.SetActive(true);
    REQUIRE(io.AddBuffer("stream-vfs", bytes.data(), static_cast<int>(bytes.size())));

    { // the file must be gone before the VFS it reads from
      // Bound to a std::string on purpose: a bare literal would pick the
      // inherited abstractSoundFile(bool) overload (const char* -> bool is a
      // standard conversion, -> std::string a user-defined one).
      const std::string id = "stream-vfs";
      soundFile f(id);
      f.create(true);
      REQUIRE(waitReady(f)); // without the fix: stuck at LOADING until the budget runs out
      CHECK(f.channels() == 1);
      CHECK(f.length() == static_cast<UInt>(src.size()));

      Flt pos = 0.f, vol = 1.f;
      SOUND_STATUS intent = YSE::SS_PLAYING_FULL_VOLUME;
      std::vector<float> out;
      long frame = 0;
      const long verifyTo = S + 4000; // past the boundary, still well before EOF
      for (int b = 0; frame < verifyTo; ++b) {
        blockResult r = readBlock(f, pos, false, intent, vol, out);
        REQUIRE_FALSE(r.stopped);
        REQUIRE(r.valid > 0); // no frames at all: the refill never landed
        for (UInt j = 0; j < r.valid && frame < verifyTo; ++j, ++frame) {
          CHECK(out[j] == doctest::Approx(src[static_cast<size_t>(frame)]).epsilon(0.0001));
        }
        breathe(b);
      }
    }

    CHECK(io.RemoveBufferByName("stream-vfs"));
    io.SetActive(false);
  }

  // 13. The other half of issue #823's contract: a name the custom reader does
  //     not know must fail cleanly rather than hang at LOADING. INVALID is the
  //     verdict an unreadable disk file already gets, and the one setup() knows
  //     how to drop.
  TEST_CASE("streaming: an unknown custom-IO source ends INVALID, not LOADING (issue #823)") {
    if (!TestHelpers::engineInit()) return;

    YSE::BufferIO io;
    io.SetActive(true);

    {
      const std::string id = "no-such-buffer"; // see the overload note above
      soundFile f(id);
      f.create(true);
      CHECK(TestHelpers::pacedUntil(3000, [&f] { return f.getState() == YSE::INTERNAL::INVALID; }));
    }

    io.SetActive(false);
  }

  // 14. Issue #841 regression: a refill that has landed must never be
  //     overwritten before the audio thread consumes it.
  //
  //     The reader below plays the audio thread's role in a tight loop of tiny
  //     blocks while the real slow pool lands refills whose completion is
  //     phase-randomised (see refillRaceFile). On the unfixed code the TOCTOU
  //     between requestRefill's _backReady check and its _refillInFlight claim
  //     lets a completion slip between the two, scheduling a spurious second
  //     fill: `overwrites` counts those directly, and the marker stream shows
  //     the user-visible symptom — playback jumping forward by exactly one
  //     stream buffer. Fixed, both counters stay at zero however often the
  //     window is crossed.
  TEST_CASE("streaming: a landed refill is never overwritten before it is consumed (#841)") {
    if (!TestHelpers::engineInit()) return;

    refillRaceFile f;

    Flt pos = 0.f, vol = 1.f;
    SOUND_STATUS intent = YSE::SS_PLAYING_FULL_VOLUME;
    std::vector<YSE::DSP::buffer> fb(1);
    const UInt blockLen = 16;

    long long frame = 0; // absolute stream frame the next real sample must match
    long discontinuities = 0;

    // Enough consumed frames for a few thousand fill completions, each a fresh
    // chance for the race. Bounded by reads, not wall clock, so a loaded or
    // sanitized run does the same amount of work.
    const long targetFills = 2000;
    const long reads = targetFills * (static_cast<long>(S) / blockLen + 1);
    for (long b = 0; b < reads; ++b) {
      f.read(fb, pos, blockLen, 1.0f, true, intent, vol);
      REQUIRE(intent == YSE::SS_PLAYING_FULL_VOLUME); // full fills: never terminal
      const Flt* out = fb[0].getPtr();
      for (UInt j = 0; j < blockLen; ++j) {
        if (out[j] == 0.0f) continue; // underrun frame: the stream did not advance
        if (out[j] != refillRaceFile::marker(frame)) {
          ++discontinuities;
          // Resync to the frame the stream actually produced, so one skip is
          // counted once instead of once per remaining sample (the original
          // flake failed 16345 assertions off a single skip).
          const long long v = static_cast<long long>(out[j]);
          frame += ((v - 1) - (frame % 1000000) + 1000000) % 1000000;
        }
        ++frame;
      }
    }

    INFO("fills completed: " << f.fills.load() << ", overwrites: " << f.overwrites.load()
                             << ", discontinuities: " << discontinuities);
    CHECK(f.overwrites.load() == 0);
    CHECK(discontinuities == 0);
  }

  // 15. Issue #844 regression: re-arming the refill job while its previous
  //     run's pool bookkeeping is still in flight must never lose the refill.
  //
  //     threadPoolJob::activate() stores `isDone = true; inQueue = false;`
  //     only after run() has returned, but the refill protocol releases its
  //     _refillInFlight claim *inside* run() (fillBackBuffer's last store).
  //     Unfixed, the audio thread could win the claim in that window and
  //     re-arm the job; the worker's trailing stores then clobbered the fresh
  //     `inQueue = true` (and its stale `isDone = true` could survive start()),
  //     so the re-queued job was popped as already-done and skipped — leaving
  //     _refillInFlight true forever, every later requestRefill losing the
  //     CAS, and the stream permanently silent. requestRefill now refuses to
  //     arm while _refillJob.isQueued() still reads true (the worker's
  //     genuinely last store, issue #239) and retries on the next call.
  //
  //     The driver hammers exactly that edge: it consumes each landed fill
  //     and instantly re-pumps in a tight spin, so every fill completion is
  //     chased across the [claim-release .. inQueue=false] window, while
  //     oversubscribing spinner threads make it likely the slow-pool worker
  //     is preempted inside the window at least once. The window is a handful
  //     of instructions wide, so the clobber is a rare event even here — but
  //     one occurrence is permanent, which is what the oracle checks: the
  //     stream must keep producing fills for the whole (wall-clock-bounded)
  //     run.
  TEST_CASE("streaming: refill re-armed during its pool bookkeeping is never lost (#844)") {
    if (!TestHelpers::engineInit()) return;

    rearmProbeFile f;

    // Oversubscribe the machine so the slow-pool worker gets preempted at
    // random points — including between fillBackBuffer's claim release and
    // activate()'s trailing bookkeeping.
    std::atomic<bool> stop{false};
    unsigned hc = std::thread::hardware_concurrency();
    if (hc == 0) hc = 4;
    std::vector<std::thread> spinners;
    spinners.reserve(hc);
    for (unsigned i = 0; i < hc; ++i)
      spinners.emplace_back([&stop] {
        volatile uint64_t sink = 0;
        while (!stop.load(std::memory_order_relaxed))
          ++sink;
      });

    using clock = std::chrono::steady_clock;
    const auto deadline = clock::now() + std::chrono::seconds(5);
    long cycles = 0;
    bool stalled = false;

    while (!stalled && clock::now() < deadline) {
      // Arm (or retry) until this cycle's fill lands. A lost re-arm leaves
      // _refillInFlight stuck true with nothing queued, so no fill can ever
      // land again: that permanent stall is what this wait detects. The
      // budget is generous — under oversubscription plus TSan a healthy fill
      // round-trip is slow, and the budget is only ever exhausted on failure.
      const auto cycleDeadline = clock::now() + std::chrono::seconds(30);
      while (!f.backReady()) {
        f.pump();
        if (clock::now() >= cycleDeadline) {
          stalled = true;
          break;
        }
      }
      if (stalled) break;
      // Consume the landed buffer and immediately chase the worker's epilogue
      // with the next arm, exactly as streamSwap -> requestRefill does.
      f.consumeBack();
      f.pump();
      ++cycles;
    }

    stop.store(true, std::memory_order_relaxed);
    for (auto& t : spinners)
      t.join();

    INFO("cycles: " << cycles << ", fills: " << f.fills.load() << ", claimed: " << f.refillClaimed()
                    << ", queued: " << f.refillQueued());
    CHECK_FALSE(stalled); // a lost re-arm is permanent: no fill would ever land again
    CHECK(cycles > 0);
  }

  // 16. Issue #844 regression (flag-leak half): a refill claim left set by a
  //     dropped enqueue must self-heal instead of silencing the stream.
  //
  //     threadPool::addJob returns without queueing when the pool is inactive
  //     and drops the job on background-ring overflow, and shutdown()'s drain
  //     releases queued jobs without running them. All three leave
  //     _refillInFlight == true with nothing queued or running. Unfixed, every
  //     later requestRefill lost its CAS against that orphaned claim, no
  //     refill was ever scheduled again, and the stream was permanently
  //     silent. requestRefill now recognises the orphaned state — claimed but
  //     not queued is impossible for a live cycle, whose inQueue stays true
  //     until after the claim is released — and releases it before arming.
  TEST_CASE("streaming: a refill claim leaked by a dropped enqueue self-heals (#844)") {
    if (!TestHelpers::engineInit()) return;

    rearmProbeFile f;

    // Recreate the dropped-enqueue state directly: claim set, nothing queued.
    f.leakClaim();
    REQUIRE(f.refillClaimed());
    REQUIRE_FALSE(f.refillQueued());

    // A single read()'s worth of scheduling must recover: release the orphaned
    // claim, win it back, and land a fill. Unfixed, pump() can never win the
    // CAS, so this times out with zero fills.
    f.pump();
    const bool landed = TestHelpers::pacedUntil(3000, [&f] { return f.backReady(); });

    INFO("fills completed: " << f.fills.load() << ", claimed: " << f.refillClaimed());
    CHECK(landed);
    CHECK(f.fills.load() == 1);
  }

} // TEST_SUITE("sound")

#endif // LIBSOUNDFILE_BACKEND
