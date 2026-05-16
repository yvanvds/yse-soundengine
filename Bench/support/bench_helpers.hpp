#pragma once
// Shared engine-initialisation harness for benchmarks that exercise
// engine-level state (System, sound, channel, reverb, patcher).
//
// Mirrors Tests/support/null_device.hpp: init the engine, then pause the
// audio stream so the benchmark thread is the sole driver of
// `YSE::System().update()`. This keeps the measurement deterministic — no
// audio thread racing the bench loop — and lets the binary run on CI
// machines without a real audio device.
//
// The engine state is process-scoped (static singletons), so call
// `engineInit()` at the top of every fixture / benchmark that needs the
// engine alive.  It is idempotent — repeat calls are no-ops.  Let
// process exit handle teardown; explicit `System().close()` mid-process
// would re-trigger the slow-pool / mutex shutdown sequence on every
// benchmark, which would dominate the timing of cheap operations.

#include "yse.hpp"
#include "channel/channelInterface.hpp"

namespace BenchHelpers {

inline bool engineInitialized() {
    return YSE::ChannelMaster().isValid();
}

inline bool engineInit() {
    if (engineInitialized()) return true;
    if (!YSE::System().init()) return false;
    YSE::System().pause();
    return true;
}

// Initialise without opening an audio device — for benchmarks that drive
// the engine via DEVICE::Manager().renderOffline(blocks). Skips the
// PortAudio addCallback() entirely, so no stream is opened and no real
// audio thread runs. Pa_Initialize() still runs (it probes backends at
// the OS layer and prints noise to stderr on Linux without a default
// card, but does not block). After this call the engine is in the same
// state as engineInit() would leave it — channels created, managers
// active — except no audio callback is firing.
inline bool engineInitOffline() {
    if (engineInitialized()) return true;
    return YSE::System().initOffline();
}

} // namespace BenchHelpers
