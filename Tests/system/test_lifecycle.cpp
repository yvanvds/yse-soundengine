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
// SCOPE: this verifies the *assert* is gone — the documented init/close/init
// repro no longer aborts and the persistent objects re-validate. It does NOT
// assert a fully functional re-initialized engine: global::close() shuts the
// slow/fast thread pools down for good (global::init() has no restart path), so
// a re-init'd engine cannot yet load sounds or run jobs. That deeper gap is
// tracked in #140 and is out of scope for #132.
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
#include "yse.hpp"
#include "channel/channelInterface.hpp"
#include "reverb/reverbInterface.hpp"
#include "reverb/reverbManager.h"

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

} // TEST_SUITE("lifecycle")
