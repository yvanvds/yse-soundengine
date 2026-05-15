#pragma once
// Engine initialization for unit tests that exercise the channel, reverb,
// or sound subsystems.
//
// `engineInit()` initialises the engine and *then immediately pauses* the
// PortAudio output stream. This is NOT a workaround for the audio-callback
// race documented in KNOWN_ISSUES.md — that race was fixed at the engine
// level in:
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

// Like engineInit() but resumes the audio stream afterwards. Use only in
// integration tests that exercise the live audio callback path, and pump
// the engine via `YSE::System().update()` + `System().sleep()` rather than
// `Manager().update()` to avoid double-driving the manager update from two
// threads. The local-static gate keeps resume() idempotent across multiple
// test cases.
inline bool engineInitWithAudio() {
    if (!engineInit()) return false;
    static bool audioResumed = false;
    if (!audioResumed) {
        YSE::System().resume();
        audioResumed = true;
    }
    return true;
}

} // namespace TestHelpers
