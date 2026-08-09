#pragma once
// Engine initialization for unit tests that exercise the channel, reverb,
// or sound subsystems.
//
// `engineInit()` initialises the engine and *then immediately pauses* the
// PortAudio output stream. This is NOT a workaround for the audio-callback
// race tracked in issue #41 — that race was fixed at the engine level in:
//
//   * Phase A — mutex on `implementations` + lock-free SPSC inbox for
//                `toLoad` between main thread and audio thread.
//   * Phase B — audio-thread-side disconnect-before-delete + per-impl
//                `connectedToParent` atomic flag in SOUND and CHANNEL.
//   * Phase C — atomic `source_dsp` with defensive release-time nullify.
//
// pause() is here for a *different* reason: unit tests drive
// `SOUND::Manager().update()` (and the equivalents for CHANNEL/REVERB)
// directly from the test thread via helpers like `drainSoundManager()` in
// test_sound_impl.cpp. Those manager update() functions are designed to be
// called from a *single* thread (the audio callback thread in production).
// If we left the audio stream open, the PortAudio thread would also call
// `Manager().update()` and the two callers would race on the
// audio-thread-owned `inUse` / `toLoad` lists — which Phase A/B/C
// deliberately left lockless because they are single-threaded by design.
//
// Integration tests that genuinely need a live audio callback call
// `engineInitWithAudio()` instead, and use `YSE::System().update()` +
// `System().sleep()` — the public API, which only flags for update and
// lets the audio thread drive the manager updates as in production.
//
// Usage:
//   TestHelpers::engineInit();          // unit tests, audio paused
//   TestHelpers::engineInitWithAudio(); // integration tests, audio live
//
// The engine state persists for the lifetime of the test process because all
// subsystem managers are process-scoped static singletons.  There is no
// matching engineClose(): let the process exit normally to trigger static
// destructors.

#include "yse.hpp"
#include "channel/channelInterface.hpp"

namespace TestHelpers {

  // Returns true if the master channel has already been initialised via System().init().
  inline bool engineInitialized() {
    return YSE::ChannelMaster().isValid();
  }

  // Initialise the full engine state and pause the audio stream so the test
  // thread is the sole driver of `Manager().update()`. Returns true on
  // success, false if init failed (typically CI without a default audio
  // device — addCallback() is a no-op there).
  inline bool engineInit() {
    if (engineInitialized()) return true;
    if (!YSE::System().init()) return false;
    YSE::System().pause();
    return true;
  }

  // True when the device is delivering audio right now. missedCallbacks()
  // counts consecutive update() ticks that saw no audio callback, so it drops
  // to 0 on the first tick after a callback lands and rises again the moment
  // one stops. Deliberately *not* one of the getters the system suite asserts
  // on (active sample rate / buffer size / output latency) — using one of those
  // as the precondition for the cases that measure them would make them
  // vacuous.
  //
  // Retried a few times before concluding the device is dead: two update()
  // ticks closer together than one callback period legitimately see no callback
  // in between, which would read as "stopped" on a perfectly healthy stream.
  inline bool audioIsFlowing(int attempts = 5, unsigned int sleepMs = 20) {
    for (int i = 0; i < attempts; ++i) {
      YSE::System().update();
      if (YSE::System().missedCallbacks() == 0) return true;
      YSE::System().sleep(sleepMs);
    }
    return false;
  }

  // Like engineInit() but resumes the audio stream afterwards. Use only in
  // integration tests that exercise the live audio callback path, and pump
  // the engine via `YSE::System().update()` + `System().sleep()` rather than
  // `Manager().update()` to avoid double-driving the manager update from two
  // threads.
  //
  // resume() only *starts* the stream: Pa_StartStream() returns before the
  // device has delivered its first callback, and until it does
  // System().missedCallbacks() is non-zero — a state indistinguishable from a
  // stalled device. So the helper used to hand back a stream that was started
  // but not yet running, and whichever case called it first paid the whole
  // start-up window inside its own assertions. That is issue #675: the first
  // caller in the integration suite is the #661 case, whose opening
  // `REQUIRE(audioStreamRunning())` samples a single 50 ms window. On an idle
  // Windows host the first callback lands inside it (60/60 runs); with the
  // machine under CPU load it was measured at 55-70 ms, so the REQUIRE tipped
  // over (43/60 runs), aborted the case, and turned the suite's 162 assertions
  // into 157 with a single failure — the exact signature reported in #675.
  //
  // Waiting here — in the setup, once per process — means every caller's
  // assertions measure the running device instead of the start-up gap, and
  // they keep their original strictness. The ceiling is ~2 s, orders of
  // magnitude beyond any real device start-up, and deliberately does not
  // report failure: a device that never starts must still trip the caller's
  // own liveness assertion rather than being swallowed here.
  //
  // The `audioResumed` gate below starts the stream once per process, and it
  // deliberately does *not* re-check whether that stream is still open. That is
  // a decision, and issue #717 is where it was made rather than assumed.
  //
  // In a shared process something does close the device underneath it:
  // `devicelayer`'s ensureOffline() runs System().close() + initOffline(), and
  // doctest orders cases by *file*, so test_device_layer.cpp lands between
  // system/test_api_doc_coverage.cpp and system/test_system_active_state.cpp —
  // inside the `system` suite's own files. The obvious repair, ask
  // audioIsFlowing() and resume() when it says no, was implemented and measured,
  // and it is worse than the problem: it puts a live PortAudio callback thread
  // back into a process where the unit suites drive `Manager().update()` from
  // the test thread, which is the exact race this header opens by explaining.
  // The unfiltered run then stops merely failing three assertions and *aborts*
  // on lfQueue.hpp's `!inSection` reentrancy assertion.
  //
  // So the stream is started once and never re-established, and the cases that
  // need a live one ask audioIsFlowing() themselves and skip when it is not —
  // the mirror of the guard `devicelayer` carries for the opposite state. The
  // two suites need opposite device state and cannot share a process; that is
  // what the isolated ctest entries are for.
  inline bool engineInitWithAudio() {
    if (!engineInit()) return false;
    static bool audioResumed = false;
    if (!audioResumed) {
      YSE::System().resume();
      audioResumed = true;
      // missedCallbacks() returns to 0 on the first update() that sees a
      // callback, which is exactly "the device is delivering audio now".
      for (int i = 0; i < 100; ++i) {
        YSE::System().sleep(20);
        YSE::System().update();
        if (YSE::System().missedCallbacks() == 0) break;
      }
    }
    return true;
  }

} // namespace TestHelpers
