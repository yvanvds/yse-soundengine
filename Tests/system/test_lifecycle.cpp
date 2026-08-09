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
#include <memory>
#include <thread>
#include <vector>
#include "yse.hpp"
#include "channel/channelInterface.hpp"
#include "internal/namedBus.h"
#include "reverb/reverbInterface.hpp"
#include "reverb/reverbManager.h"
#include "sound/soundInterface.hpp"
#include "sound/soundManager.h"
#include "internal/AudioTest.h"
#include "internal/time.h"
#include "internal/underWaterEffect.h"
#include "dsp/ADSRenvelope.hpp"
#include "headers/constants.hpp"
#include "yse_c/yse_common.h"
#include "yse_c/yse_system.h"

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

  // Requested-rate lifecycle (issue #646), doubling as the #637 session-
  // contract scenario: the sample rate is an application setting, fixed per
  // session — changing it means close() + init(). Offline sessions have no
  // device to negotiate with, so the requested rate is authoritative and the
  // assertions below are deterministic on headless CI.
  //
  // The derived-state observable: a 0.1 s ADSR attack ramp generated *inside*
  // a session bakes SAMPLERATE into its breakpoint positions, so the number of
  // 128-sample blocks needed to exhaust it is ceil(0.1 * rate / 128) — 38 at
  // 48000, 35 at 44100 (same arithmetic as Tests/dsp/test_envelope.cpp).
  static int adsrAttackBlocks() {
    YSE::DSP::ADSRenvelope adsr;
    adsr.addPoint({0.0f, 0.0f, 1.0f});
    adsr.addPoint({0.1f, 1.0f, 1.0f});
    adsr.generate();
    int blocks = 1;
    adsr(YSE::DSP::ADSRenvelope::ATTACK);
    while (!adsr.isAtEnd() && blocks < 1000) {
      adsr(YSE::DSP::ADSRenvelope::RESUME);
      ++blocks;
    }
    return blocks;
  }

  TEST_CASE("lifecycle: requested sample rate applies per session across init/close (issue #646)") {
    YSE::System().close(); // normalize to a closed engine
    const UInt startRate = YSE::SAMPLERATE;
    const unsigned int startRequest = YSE::System().requestSampleRate();

    // Session 1: request 48000 before init.
    YSE::System().requestSampleRate(48000);
    CHECK(YSE::System().requestSampleRate() == 48000u);
    if (!YSE::System().initOffline()) { // no offline device on this host
      YSE::System().requestSampleRate(startRequest);
      return;
    }
    CHECK(YSE::SAMPLERATE == 48000u);
    CHECK(YSE::System().getSampleRate() == doctest::Approx(48000.0));
    CHECK(adsrAttackBlocks() == 38);

    // The rate is a per-session setting: close() releases the lock so the
    // next init() can renegotiate.
    YSE::System().close();
    CHECK(YSE::System().getSampleRate() == 0.0);

    // Session 2: reopen at a different requested rate.
    YSE::System().requestSampleRate(44100);
    REQUIRE(YSE::System().initOffline());
    CHECK(YSE::SAMPLERATE == 44100u);
    CHECK(YSE::System().getSampleRate() == doctest::Approx(44100.0));
    CHECK(adsrAttackBlocks() == 35);
    YSE::System().close();

    // The same parameter through the C ABI (issue #646 "the C API exposes the
    // same").
    YseSystem* sys = yse_system_get();
    yse_system_request_sample_rate(sys, 48000);
    CHECK(yse_system_get_requested_sample_rate(sys) == 48000u);
    REQUIRE(yse_system_init_offline(sys) == YSE_OK);
    CHECK(yse_system_get_sample_rate(sys) == doctest::Approx(48000.0));
    yse_system_close(sys);

    // NULL handles follow the header's null-safe no-op convention.
    yse_system_request_sample_rate(nullptr, 48000);
    CHECK(yse_system_get_requested_sample_rate(nullptr) == 0u);

    // Leave the process as we found it for later cases in this isolated
    // binary: clear the request (it deliberately survives close()) and put
    // the pre-test rate back (permitted — the session lock is released).
    YSE::System().requestSampleRate(startRequest);
    YSE::SAMPLERATE = startRate;
  }

  // The #637 acceptance scenario proper: host-owned DSP state created in one
  // session must re-derive its SAMPLERATE-dependent internals after a real
  // close() -> init() cycle at a different rate. The observable is the same
  // rendered-envelope arithmetic as adsrAttackBlocks() above, but with ONE
  // envelope kept alive across the sessions — before the fix its table kept
  // the first session's sample counts forever.
  TEST_CASE("lifecycle: host-owned envelope re-derives after a rate change (issue #637)") {
    YSE::System().close(); // normalize to a closed engine
    const UInt startRate = YSE::SAMPLERATE;
    const unsigned int startRequest = YSE::System().requestSampleRate();

    YSE::System().requestSampleRate(48000);
    if (!YSE::System().initOffline()) { // no offline device on this host
      YSE::System().requestSampleRate(startRequest);
      return;
    }

    // Session 1 (48 kHz): generate and play out a 0.1 s attack ramp.
    YSE::DSP::ADSRenvelope adsr;
    adsr.addPoint({0.0f, 0.0f, 1.0f});
    adsr.addPoint({0.1f, 1.0f, 1.0f});
    adsr.generate();

    auto attackBlocks = [&adsr]() {
      int blocks = 1;
      adsr(YSE::DSP::ADSRenvelope::ATTACK);
      while (!adsr.isAtEnd() && blocks < 1000) {
        adsr(YSE::DSP::ADSRenvelope::RESUME);
        ++blocks;
      }
      return blocks;
    };
    CHECK(attackBlocks() == 38); // ceil(0.1 * 48000 / 128)

    // Reopen at 44.1 kHz. The envelope object survives the cycle; its next
    // note must last 0.1 s of the NEW session's clock.
    YSE::System().close();
    YSE::System().requestSampleRate(44100);
    REQUIRE(YSE::System().initOffline());
    CHECK(attackBlocks() == 35); // ceil(0.1 * 44100 / 128), not the stale 38
    YSE::System().close();

    YSE::System().requestSampleRate(startRequest);
    YSE::SAMPLERATE = startRate;
  }

  // Regression test for issue #715: the stock underwater effect must survive a
  // close() -> init() cycle.
  //
  // INTERNAL::UnderWaterEffect() is a process-global driver that owns a
  // persistent YSE::reverb for the REVERB_UNDERWATER zone. That interface is
  // long-lived, but its *implementation* is session state:
  // REVERB::Manager().destroy() clears every implementation at close(), and
  // each implementation's destructor nulls its interface's pimpl. The manager
  // re-creates its own two persistent reverbs (globalReverb, calculatedValues)
  // in create(); nothing re-created this third one, because it was only ever
  // built in the driver's constructor — which runs once per process.
  //
  // So every session after the first messaged a null implementation the moment
  // a host touched the effect: System().setUnderWaterDepth() ->
  // reverb::setActive() -> REVERB::implementationObject::sendMessage(this=0).
  // That is the access violation the unfiltered yse_tests run died on (the
  // fault landed in Tests/system/test_c_api_lowcov.cpp's
  // yse_system_set_underwater_depth call, reached with the engine closed).
  //
  // Pre-fix this case faults on the marked line. Post-fix the driver rebuilds
  // the zone for the new session, and no-ops while no session is up.
  TEST_CASE("lifecycle: underwater FX is rebuilt across a close/init cycle (issue #715)") {
    YSE::System().close(); // normalize to a closed engine

    if (!YSE::System().initOffline()) return; // no offline device on this host

    // Session 1: first touch constructs the driver and its zone.
    YSE::System().setUnderWaterDepth(0.5f);
    REQUIRE(YSE::INTERNAL::UnderWaterEffect().zone() != nullptr);
    CHECK(YSE::INTERNAL::UnderWaterEffect().zone()->isValid());
    CHECK(YSE::INTERNAL::UnderWaterEffect().module().depth() == doctest::Approx(0.5f));
    YSE::System().setUnderWaterDepth(0.0f);

    YSE::System().close();
    // close() freed the zone's implementation and nulled the handle.
    CHECK_FALSE(YSE::INTERNAL::UnderWaterEffect().zone()->isValid());

    // Engine down: the driver must no-op on the zone rather than message a
    // freed implementation. The module parameter still takes the value — it is
    // a plain atomic and carries no session state.
    YSE::System().setUnderWaterDepth(0.25f);
    CHECK(YSE::INTERNAL::UnderWaterEffect().module().depth() == doctest::Approx(0.25f));
    CHECK_FALSE(YSE::INTERNAL::UnderWaterEffect().zone()->isValid());

    // Session 2: the zone has to come back, exactly like the reverb manager's
    // own persistent pair.
    REQUIRE(YSE::System().initOffline());
    YSE::System().setUnderWaterDepth(0.5f); // <- faulted here before the fix
    CHECK(YSE::INTERNAL::UnderWaterEffect().zone()->isValid());
    CHECK(YSE::INTERNAL::UnderWaterEffect().zone()->getActive());
    // The rebuilt zone carries the underwater preset, not a fresh
    // implementation's defaults — the reason it is rebuilt rather than
    // re-created behind the old interface.
    CHECK(YSE::INTERNAL::UnderWaterEffect().zone()->getSize() == doctest::Approx(10.0f));

    YSE::System().setUnderWaterDepth(0.0f);
    CHECK_FALSE(YSE::INTERNAL::UnderWaterEffect().zone()->getActive());
    // The attach path is callable in the new session too. Detach again so the
    // process-global module does not go into close() still occupying the
    // master's insert slot.
    YSE::System().underWaterFX(YSE::ChannelMaster());
    CHECK(YSE::ChannelMaster().getDSP() == &YSE::INTERNAL::UnderWaterEffect().module());
    YSE::ChannelMaster().setDSP(nullptr);

    YSE::System().close();
  }

  // Regression test for issue #717: the built-in diagnostic tone must survive a
  // close() -> init() cycle. Same defect family as the underwater zone above,
  // in the sibling process-global singleton.
  //
  // INTERNAL::Test() is a function-local static owning a YSE::sound built once
  // per process, in its constructor. SOUND::Manager().destroy() clears every
  // sound implementation at System::close() and each implementation's
  // destructor nulls its interface's pimpl, so from the second session on the
  // driver was messaging a dead interface. Unlike the reverb case this does not
  // fault — every sound method is documented to no-op while isValid() is false
  // — so the symptom is silence: System().AudioTest(true), and the C API's
  // yse_system_audio_test() with it, did nothing at all. For the engine's
  // built-in *output diagnostic* that is the worst possible failure mode, and
  // it is what the "audio test" case in the devicelayer suite was failing on in
  // a shared process: capilowcov drives yse_system_audio_test() from
  // Tests/system/test_c_api_lowcov.cpp, which sorts before
  // test_device_layer.cpp, so the singleton was built in that earlier session
  // and the close() in between emptied it.
  //
  // Reproduced with two *offline* suites and no PortAudio anywhere:
  //   yse_tests --test-suite=capilowcov,capilowcovlife,devicelayer
  //
  // Pre-fix the second session's CHECK(isPlaying()) fails. Post-fix the driver
  // re-creates the sound for the new session, and no-ops while none is up.
  TEST_CASE("lifecycle: the built-in audio test tone is rebuilt across a close/init cycle "
            "(issue #717)") {
    YSE::System().close(); // normalize to a closed engine

    if (!YSE::System().initOffline()) return; // no offline device on this host

    // isPlaying() reads the implementation's head status, which the audio tick
    // writes — so the SI_PLAY message has to be delivered before it is read.
    // update() flags the control-plane work, renderOffline() runs the audio
    // callback body, and the sleep lets the single-threaded slow pool execute
    // the queued setup() job create() posted.
    auto pump = []() {
      for (int i = 0; i < 20; ++i) {
        YSE::System().update();
        YSE::System().renderOffline(2);
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
      }
    };

    // Session 1: first touch constructs the driver and its sound.
    // isValid() is the handle, isReady() is the implementation having finished
    // the setup job create() posts to the slow pool — together they are "there
    // is a live diagnostic sound in this session". Deliberately not isPlaying():
    // that reads the DSP-side status the shepard source leaves alone, and what
    // this case owns is the *rebuild*. That the tone actually reaches the master
    // mix is asserted at the render level by the devicelayer suite's "audio
    // test" case, in the process that can measure it.
    YSE::System().AudioTest(true);
    pump();
    CHECK(YSE::INTERNAL::Test().source().isValid());
    CHECK(YSE::INTERNAL::Test().source().isReady());
    YSE::System().AudioTest(false);
    pump();

    YSE::System().close();
    // close() freed the sound's implementation and nulled the handle.
    CHECK_FALSE(YSE::INTERNAL::Test().source().isValid());

    // Engine down: the driver must no-op rather than message a freed
    // implementation, and must not leave a half-built sound behind.
    YSE::System().AudioTest(true);
    CHECK_FALSE(YSE::INTERNAL::Test().source().isValid());

    // Session 2: the sound has to come back, or the diagnostic is silent for
    // the rest of the process.
    REQUIRE(YSE::System().initOffline());
    YSE::System().AudioTest(true); // <- silently did nothing before the fix
    pump();
    CHECK(YSE::INTERNAL::Test().source().isValid());
    CHECK(YSE::INTERNAL::Test().source().isReady());

    YSE::System().AudioTest(false);
    pump();
    YSE::System().close();
  }

  // Regression test for issue #716: NamedBus subscription handles were numbered
  // per bus instance, restarting at 1 every session, while the subscribers that
  // hold them are not destroyed with the bus.
  //
  // global::close() drops the NamedBus, but a named channel / sound / synth, or
  // a patcher .receive, is host- or patcher-owned and survives. Each of those
  // unsubscribes behind a bare Global().isActive() guard — true again in the
  // next session — so a handle minted by the dead bus was handed to the live
  // one, which had reissued the same low numbers. The unsubscribe then dropped
  // an unrelated, live subscription: the victim silently stopped receiving,
  // with nothing logged.
  //
  // Handles are now drawn from a process-global counter, the way tap handles
  // already were (issue #389), so a stale handle is permanently unknown to any
  // later bus and unsubscribe() on it is a guaranteed no-op.
  //
  // The victim below is a real call site, not a hand-rolled subscription:
  // YSE::channel::name() registers "channel.<name>.volume" on the bus and
  // ~channel() is the guarded teardown. Pre-fix the final CHECK fails (one
  // publish reaches one subscriber fewer than were registered) and so does the
  // handle-ordering CHECK; post-fix both hold.
  TEST_CASE(
      "lifecycle: a bus handle from a closed session cannot unsubscribe a live one (issue #716)") {
    using YSE::INTERNAL::BusValue;
    using YSE::INTERNAL::SubHandle;

    YSE::System().close(); // normalize to a closed engine

    if (!YSE::System().initOffline()) return; // no offline device on this host

    // Session 1. The channel's handle is private, so bracket the registration
    // with two probes: the handle it took is the one issued between them, and
    // the REQUIRE pins the "name() takes exactly one subscription" assumption
    // the arithmetic rests on. No create() — naming is independent of the
    // implementation, and leaving pimpl null keeps the cross-session destructor
    // about the bus and nothing else.
    auto stale = std::make_unique<YSE::channel>();
    const SubHandle before = YSE::INTERNAL::Bus().subscribe("bus.probe", [](const BusValue&) {});
    stale->name("staleSessionChannel");
    const SubHandle after = YSE::INTERNAL::Bus().subscribe("bus.probe", [](const BusValue&) {});
    REQUIRE(after == before + 2);
    const SubHandle staleHandle = before + 1;
    YSE::INTERNAL::Bus().unsubscribe(before);
    YSE::INTERNAL::Bus().unsubscribe(after);

    // The channel survives this boundary still holding staleHandle and still
    // flagged as a bus owner: nothing in close() reaches a host-owned object.
    YSE::System().close();
    REQUIRE(YSE::System().initOffline());

    // Session 2 runs on a brand-new NamedBus. Walk its handle counter up to the
    // stale value, keeping every subscription issued on the way: with per-bus
    // numbering the counter restarts at 1, so one of these *is* numbered
    // staleHandle. With process-global numbering the session's first handle
    // already exceeds it and the loop subscribes exactly once.
    const std::string victimName = "bus.session2.victim";
    int hits = 0;
    std::vector<SubHandle> live;
    while (live.size() < 1024) {
      const SubHandle h =
          YSE::INTERNAL::Bus().subscribe(victimName, [&hits](const BusValue&) { ++hits; });
      live.push_back(h);
      if (h >= staleHandle) break;
    }
    REQUIRE(live.back() >= staleHandle);
    // The guarantee, stated directly: no handle of this session can collide
    // with one the previous session issued.
    CHECK(live.front() > staleHandle);

    YSE::INTERNAL::Bus().publish(victimName, BusValue{1}, YSE::T_GUI);
    REQUIRE(hits == static_cast<int>(live.size()));

    // Now drive the boundary: the session-1 channel is destroyed *during*
    // session 2, so its destructor sees an active engine and unsubscribes the
    // stale handle on this bus.
    stale.reset();

    hits = 0;
    YSE::INTERNAL::Bus().publish(victimName, BusValue{2}, YSE::T_GUI);
    CHECK(hits == static_cast<int>(live.size()));

    for (const SubHandle h : live)
      YSE::INTERNAL::Bus().unsubscribe(h);
    YSE::System().close();
  }

} // TEST_SUITE("lifecycle")
