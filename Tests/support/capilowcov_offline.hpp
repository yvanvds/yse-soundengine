// Shared offline-engine bootstrap for the `capilowcov` suite (issue #568).
//
// The suite spans several translation units (dsp / music / midi / patcher /
// system) that all run in one dedicated ctest process. Several of them need a
// live engine, and the established init recipe — close() to normalize, then
// init_offline() — must run exactly once for the whole process: a second
// close/init pair from another TU would tear the first one's state down
// underneath the cases still using it.
//
// An `inline` function has one instance across the program, so its
// function-local statics are shared by every TU that includes this header.
// That gives the whole suite a single idempotent entry point without a .cpp.
//
// "Once per process" is a latch on the *normalize*, not on the result (issue
// #715). doctest orders test cases by file, not by suite, so in a process that
// runs more than this suite the five translation units above are not
// contiguous: every suite whose files sort between them — `midisynth`,
// `playersynth`, the synth lifecycle suites, `buscapi` — runs in the gaps, and
// each of those drives yse_system_close(). A one-shot latch could not notice,
// so the tail of this suite ran against a torn-down engine and faulted. Asking
// the engine whether a session is up costs one call and makes the suite
// order-independent.
//
// initOffline() needs no audio hardware, so everything here runs on headless
// CI. Nothing in this header calls yse_system_close(); the teardown half of
// the surface lives in its own suite / process (see Tests/CMakeLists.txt).

#pragma once

#include <chrono>
#include <thread>

#include "yse_c/yse_common.h"
#include "yse_c/yse_system.h"

namespace capilowcov {

  // Bring the engine up offline for this suite. Returns false when the engine
  // is unavailable, in which case the caller should skip (doctest counts a case
  // that returns early as a pass).
  inline bool ensureOffline() {
    YseSystem* sys = yse_system_get();
    // getSampleRate() reports the session rate only while the session lock is
    // held — set at the end of initShared(), released by close() — so a
    // positive value is exactly "a session is up" through the C ABI alone.
    if (yse_system_get_sample_rate(sys) > 0.0) return true;

    static bool normalized = false;
    if (!normalized) {
      // Whatever the process arrived in (another suite's device-backed
      // session), drop it: this suite wants an offline one.
      yse_system_close(sys);
      normalized = true;
    }
    return yse_system_init_offline(sys) == YSE_OK;
  }

  // Offline analogue of the channel suite's drainChannels(): update() flags the
  // control-plane work the audio callback body runs, render_offline() runs
  // blocks, and the short sleep lets the slow pool execute queued setup() jobs
  // so freshly created channels / sounds reach OBJECT_READY.
  inline void pump(int iterations = 20) {
    YseSystem* sys = yse_system_get();
    for (int i = 0; i < iterations; ++i) {
      yse_system_update(sys);
      yse_system_render_offline(sys, 2);
      std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
  }

} // namespace capilowcov
