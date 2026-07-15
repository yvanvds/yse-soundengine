// Regression test for issue #132: a second engine lifecycle in the same
// process must not trip assert(pimpl == nullptr) on re-init.
//
// System::init() re-creates two sets of *persistent* interface objects every
// cycle, each guarded by assert(pimpl == nullptr):
//   - the reverb manager's global / calculated reverbs (reverb::create), and
//   - the channel manager's master + named channels (channel::create /
//     createGlobal).
// Before the fix, close() tore down the thread pools and the bus but never
// cleared those handles, so the second init() re-entered create() with a stale
// pimpl and aborted (release builds would double-register / leak instead).
// close() now calls REVERB::Manager().destroy() and CHANNEL::Manager().destroy(),
// which drop the handles so the next init() re-creates them cleanly.
//
// SCOPE: the first case here verifies the *assert* is gone — the documented
// init/close/init repro no longer aborts and the persistent objects re-validate.
// The functional gap it used to defer (a re-init'd engine could not load sounds
// or run jobs because global::close() shut the thread pools down for good) is
// closed by #140 and covered by the second case below, which drives a real
// sound to OBJECT_READY after an init/close/init cycle.
//
// ISOLATION: this lives in its own "lifecycle" TEST_SUITE, run as a dedicated
// ctest process and excluded from the combined `yse_unit_tests` run. Calling
// System::close() permanently stops the global thread pools, so a lifecycle
// test sharing a process with the other suites would break every later test
// that relies on a live engine (the harness keeps one engine up for the whole
// process — see Tests/support/null_device.hpp). A separate process sidesteps
// that entirely.
//
// initOffline() needs no audio hardware, so this runs in CI. If it ever returns
// false (no offline device on some host), the case bails out and doctest counts
// it as a pass.

#include <doctest/doctest.h>
#include <chrono>
#include <thread>
#include "yse.hpp"
#include "channel/channelInterface.hpp"
#include "reverb/reverbInterface.hpp"
#include "reverb/reverbManager.h"
#include "sound/soundInterface.hpp"
#include "sound/soundManager.h"
#include "internal/time.h"

// Absolute path to the WAV fixture injected by CMake; falls back to a relative
// path that works when the test binary runs from build-X/bin/ (mirrors the
// definition in Tests/sound/test_sound_state.cpp).
#ifndef YSE_TEST_FIXTURES_DIR
#define YSE_TEST_FIXTURES_DIR "../../Tests/support/fixtures"
#endif
static const char* const WAV_FIXTURE = YSE_TEST_FIXTURES_DIR "/test_mono_44100.wav";

TEST_SUITE("lifecycle") {

  TEST_CASE("lifecycle: repeated init/close re-creates global reverb + channels (issue #132)") {
    // Normalize to a closed engine regardless of starting state (close() is a
    // no-op when the engine is inactive).
    YSE::System().close();

    // First lifecycle: brings the global reverb and master channel up.
    if (!YSE::System().initOffline()) return; // no offline device on this host
    CHECK(YSE::REVERB::Manager().getGlobalReverb().isValid());
    CHECK(YSE::ChannelMaster().isValid());

    YSE::System().close();
    // After close() the persistent handles are cleared, so a re-init can run
    // create()/createGlobal() again without tripping their asserts.
    CHECK_FALSE(YSE::REVERB::Manager().getGlobalReverb().isValid());
    CHECK_FALSE(YSE::ChannelMaster().isValid());

    // Second lifecycle in the same process — this is where init aborted before
    // the fix (reverb::create first, then channel::createGlobal once reverb was
    // patched).
    REQUIRE(YSE::System().initOffline());
    CHECK(YSE::REVERB::Manager().getGlobalReverb().isValid());
    CHECK(YSE::ChannelMaster().isValid());

    // A third cycle for good measure, then leave the engine closed.
    YSE::System().close();
    REQUIRE(YSE::System().initOffline());
    CHECK(YSE::REVERB::Manager().getGlobalReverb().isValid());
    CHECK(YSE::ChannelMaster().isValid());
    YSE::System().close();
  }

  // Regression test for issue #140: after a full init/close cycle, global::init()
  // must revive the slow/fast thread pools so a re-initialized engine is actually
  // *functional* — not just non-aborting. The clearest observable proof is a WAV
  // file reaching OBJECT_READY, which requires the slow pool's file-load worker to
  // be alive again. Before the fix, close() joined the pools for good and init()
  // had no restart path, so the second session's sound stayed at LOADING forever
  // and this CHECK failed.
  //
  // Runs in the isolated "lifecycle" process for the same reason as the case
  // above: it drives System::close(), which the shared unit-test process cannot
  // tolerate mid-run.
  TEST_CASE("lifecycle: re-init'd engine loads a sound to OBJECT_READY (issue #140)") {
    YSE::System().close(); // normalize to a closed engine

    // Burn one full lifecycle, then re-init — this is the cycle that left the
    // pools dead before the fix.
    if (!YSE::System().initOffline()) return; // no offline device on this host
    YSE::System().close();
    REQUIRE(YSE::System().initOffline());

    {
      YSE::sound s;
      s.create(WAV_FIXTURE);
      if (s.isValid()) { // skip only if the fixture is missing on this host
        // Pump the manager directly (test thread drives update; audio is
        // paused). Budget ~2 s: the 244-byte WAV loads through the single
        // revived slow-pool worker within a few update ticks.
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        while (std::chrono::steady_clock::now() < deadline) {
          YSE::INTERNAL::Time().update();
          YSE::SOUND::Manager().update();
          if (s.isReady()) break;
          std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        CHECK(s.isReady());
      }
    } // ~sound fires here, while the engine is still up

    YSE::System().close();
  }

  // Regression test for issue #298: a SOUND::implementationObject that is still
  // alive at engine teardown (never drained to OBJECT_DELETE before close())
  // must not use-after-free its parent channel impl.
  //
  // The ordering that triggers the bug: a file-backed sound is driven to
  // OBJECT_READY — at which point doThisWhenReady() has run, so the impl is
  // linked into its parent channel's `sounds` list and connectedToParent is
  // set. The sound interface is then destroyed while the engine is still up,
  // but close() is called BEFORE the manager pumps the impl through
  // OBJECT_RELEASE→OBJECT_DELETE, so it lingers in SOUND::Manager's
  // `implementations` list still pointing at its parent channel impl. Pre-fix,
  // close() freed the channel impls (CHANNEL::Manager().destroy()) while that
  // sound impl was still referencing one, leaving `parent` dangling — then
  // dereferenced either at the next init() (the audio thread reprocesses the
  // lingering impl and calls parent->disconnect on freed storage) or during
  // static destruction of the manager singletons at process exit. A flaky
  // SIGSEGV in normal builds (heap-layout dependent), a deterministic
  // heap-use-after-free under AddressSanitizer. The fix drains the sound
  // manager in close() (SOUND::Manager().destroy()) BEFORE the channels are
  // freed, plus gates the impl destructor's parent disconnect on
  // Global().isActive() as defence in depth.
  //
  // This case sets up exactly that lingering-impl-past-close state and then
  // re-inits and closes again. Post-fix nothing dangles: the drain tears the
  // impl down while its parent is still alive, so the re-init is clean and
  // there is nothing left for static teardown to touch. The CHECKs below assert
  // the cycle completes; the real regression gate is the AddressSanitizer CI
  // leg, which runs this suite (build.yml: Sanitizers/asan) and would report
  // the freed-parent read deterministically if the drain regressed.
  TEST_CASE("lifecycle: a sound impl lingering past close() is torn down cleanly (issue #298)") {
    YSE::System().close(); // normalize to a closed engine

    if (!YSE::System().initOffline()) return; // no offline device on this host

    {
      YSE::sound s;
      s.create(WAV_FIXTURE);
      if (s.isValid()) { // skip only if the fixture is missing on this host
        // Drive the sound all the way to OBJECT_READY so doThisWhenReady() has
        // run: this is what links it into the parent channel and sets
        // connectedToParent — the precondition for the dangling-parent bug.
        // Audio is paused, so the test thread pumps the manager itself.
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        while (std::chrono::steady_clock::now() < deadline) {
          YSE::INTERNAL::Time().update();
          YSE::SOUND::Manager().update();
          if (s.isReady()) break;
          std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        CHECK(s.isReady());
      }
      // ~sound fires here (engine still up), nulling the impl's head. Crucially
      // we do NOT pump SOUND::Manager().update() afterwards, so the impl never
      // reaches OBJECT_DELETE and lingers into close() below with its parent
      // link still live.
    }

    // close() must drain the still-connected sound impl (SOUND::Manager().
    // destroy()) before freeing its parent channel — the fix for #298.
    YSE::System().close();

    // A second lifecycle: with the drain in place nothing from session 1
    // survives, so re-init/close is clean. Pre-fix the lingering impl's stale
    // parent pointer was reprocessed here (or blew up at static exit).
    REQUIRE(YSE::System().initOffline());
    YSE::System().close();

    CHECK(true); // completing the cycle without a crash is the observable win
  }

} // TEST_SUITE("lifecycle")
