#pragma once
// Minimal engine initialization for unit tests that exercise the channel, reverb,
// or sound subsystems.  Calls System().init() and then immediately pause()s the
// PortAudio output stream, so the audio callback thread never runs during tests.
//
// Why: the unit tests create and destroy sounds far faster than a real audio
// callback can safely drain queues.  When a default output device is present
// (developer workstations, not CI), the audio thread races with rapid
// sound-manager cleanup and the process crashes — usually at exit, sometimes
// mid-test.  pause() calls Pa_StopStream / Pa_CloseStream, so the engine runs
// purely on the user thread for the rest of the process.  On CI (paNoDevice),
// addCallback() is already a no-op, so the pause() is harmless there.
//
// Usage:
//   TestHelpers::engineInit();   // idempotent — safe to call multiple times
//
// The engine state persists for the lifetime of the test process because all
// subsystem managers are process-scoped static singletons.  There is no matching
// engineClose(): let the process exit normally to trigger static destructors.

#include "yse.hpp"
#include "channel/channelInterface.hpp"

namespace TestHelpers {

// Returns true if the master channel has already been initialised via System().init().
inline bool engineInitialized() {
    return YSE::ChannelMaster().isValid();
}

// Initialise the full engine state (channels, reverb, thread pools), then
// close the live audio stream so the audio callback cannot race with test
// teardown.  Returns true if channels are ready for use, false on init failure.
inline bool engineInit() {
    if (engineInitialized()) return true;
    if (!YSE::System().init()) return false;
    YSE::System().pause();
    return true;
}

// Like engineInit(), but (re)opens the audio stream.  Integration tests that
// genuinely need the audio callback to fire (e.g. end-to-end signal probes)
// call this.  resume() inside YSE calls addCallback() which has no
// already-open guard, so we gate it on a local static to make this helper
// safely callable from multiple test cases.
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
