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

} // namespace BenchHelpers
