#pragma once
// Minimal engine initialization for unit tests that exercise the channel, reverb,
// or sound subsystems.  Calls System().init() but skips the PortAudio stream open
// if no output device is available (Pa_GetDefaultOutputDevice() == paNoDevice),
// so tests run safely in CI environments without audio hardware.
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

// Initialise the full engine state (channels, reverb, thread pools).
// If no audio output device is present, the audio stream is skipped but
// the channel and reverb managers are still fully initialised.
// Returns true if channels are ready for use, false if engine init failed.
inline bool engineInit() {
    if (engineInitialized()) return true;
    return YSE::System().init();
}

} // namespace TestHelpers
