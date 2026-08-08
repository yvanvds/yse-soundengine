// Integration tests for the YSE device layer, MIDI enumeration, and the
// end-to-end audio path.
//
// Coverage:
//   - Audio device enumeration (name, default device)
//   - MIDI device enumeration — Windows only; hardware not required to be present
//   - Engine lifecycle: init, repeated updates, cpuLoad
//   - Audio callback fires within a bounded timeout
//   - Sound loading and playback on a real output device
//   - End-to-end signal probe: DSP source signal reaches an attached effect processor
//
// These tests require a real audio output device and are DISABLED in CTest by
// default (LABELS integration, DISABLED TRUE).  Run explicitly with:
//
//   ctest -L integration
//   yse_tests --test-suite=integration
//
// DSP lifetime note: sound implementations are process-scoped static singletons
// that outlive individual test cases.  DSP source / effect objects passed to
// sound::create() or sound::setDSP() MUST therefore outlive the test binary.
// g_source and g_probe are file-scope statics for exactly this reason.
// Calling System().close() mid-run violates the invariant documented in
// null_device.hpp and is not tested here; shutdown is exercised by process exit.

#include <doctest/doctest.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <new>
#include <string>
#include <vector>
#include "yse.hpp"
#include "support/null_device.hpp"
#include "headers/defines.hpp"
// The mixer layout a refused open must not touch (issue #665) is read through
// CHANNEL::Manager(); channelImplementation.h / channelMessage.h come first for
// the same reason as in Tests/system/test_device_layer.cpp (the manager
// instantiates lfQueue<CHANNEL::messageObject>).
#include "channel/channelImplementation.h"
#include "channel/channelMessage.h"
#include "channel/channelManager.h"

#ifndef YSE_TEST_FIXTURES_DIR
#define YSE_TEST_FIXTURES_DIR "../../Tests/support/fixtures"
#endif
static const char* const WAV_FIXTURE = YSE_TEST_FIXTURES_DIR "/test_mono_44100.wav";

namespace {

  // DSP source that continuously outputs a constant non-zero signal.
  // Declared at file scope so it outlives all test cases and any sound
  // implementation that holds a pointer to it.
  struct ConstantSource : YSE::DSP::dspSourceObject {
    void process(YSE::SOUND_STATUS& intent) override {
      float* p = samples[0].getPtr();
      UInt len = samples[0].getLength();
      for (UInt i = 0; i < len; i++)
        p[i] = 0.5f;
      intent = YSE::SS_PLAYING;
    }
    void frequency(float) override {}
  };

  // Multi-sine DSP source — 11 sine generators feeding a low-pass, modelled
  // on AudioTest's shepard tone (YseEngine/internal/AudioTest.cpp) and on
  // Demo07_DspSource. This is the canonical "several sines + IIR feedback"
  // shape that triggered the original #53 / #82 reports; using it in the
  // recovery test keeps the cpuLoad measurement honest against exactly the
  // graph topology the bug appeared with. File-scope for the same lifetime
  // reason as ConstantSource.
  struct MultiSineSource : YSE::DSP::dspSourceObject {
    static constexpr int kNumGens = 11;
    YSE::DSP::sine generators[kNumGens];
    YSE::DSP::lowPass lp;
    YSE::DSP::buffer out;
    float freq[kNumGens];

    MultiSineSource() {
      // Spread sines across the audible band — keeps every generator
      // contributing real signal rather than collapsing into beating.
      for (int i = 0; i < kNumGens; ++i) {
        freq[i] = 110.f + (float)i * 87.f;
      }
      lp.setFrequency(1500.f);
    }

    void process(YSE::SOUND_STATUS& intent) override {
      out = 0.0f;
      for (int i = 0; i < kNumGens; ++i)
        out += generators[i](freq[i]);
      out *= (1.f / (float)kNumGens);
      YSE::DSP::buffer& result = lp(out);
      for (UInt i = 0; i < samples.size(); ++i)
        samples[i] = result;
      if (intent == YSE::SS_WANTSTOSTOP) intent = YSE::SS_STOPPED;
    }
    void frequency(float) override {}
  };

  // Passthrough DSP effect that sets an atomic flag when non-silent audio arrives.
  // Declared at file scope for the same lifetime reason as ConstantSource.
  struct ProbeEffect : YSE::DSP::dspObject {
    std::atomic<bool> triggered{false};

    void reset() {
      triggered.store(false, std::memory_order_relaxed);
    }
    void create() override {}

    void process(MULTICHANNELBUFFER& buf) override {
      for (auto& ch : buf) {
        const float* p = ch.getPtr();
        for (UInt i = 0; i < ch.getLength(); i++) {
          if (std::fabs(p[i]) > 1e-6f) {
            triggered.store(true, std::memory_order_relaxed);
            return;
          }
        }
      }
    }
  };

  static ConstantSource g_source;
  static MultiSineSource g_multi_sine;
  static ProbeEffect g_probe;

  // Sleep briefly, pump one update tick, then report whether the audio callback
  // fired during that interval (missedCallbacks resets to 0 when callbacks arrive).
  bool audioStreamRunning() {
    YSE::System().sleep(50);
    YSE::System().update();
    return YSE::System().missedCallbacks() == 0;
  }

} // namespace

TEST_SUITE("integration") {

  // ─── Audio device enumeration ─────────────────────────────────────────────────

  TEST_CASE("device: getNumDevices does not crash after init") {
    if (!TestHelpers::engineInit()) return;
    (void)YSE::System().getNumDevices();
    CHECK(true);
  }

  TEST_CASE("device: each enumerated device has a non-empty name") {
    if (!TestHelpers::engineInit()) return;
    unsigned int n = YSE::System().getNumDevices();
    for (unsigned int i = 0; i < n; i++)
      CHECK_FALSE(YSE::System().getDevice(i).getName().empty());
  }

  TEST_CASE("device: default device name is non-empty when devices exist") {
    if (!TestHelpers::engineInit()) return;
    if (YSE::System().getNumDevices() == 0) return;
    CHECK_FALSE(YSE::System().getDefaultDevice().empty());
  }

  // The scalar half of a real, hardware-enumerated descriptor (issue #569).
  // This is the only place the assertion means anything: the devicelayer suite
  // runs the offline engine, which skips Pa_Initialize on purpose, so its
  // device list is empty and any loop over it is vacuous. Here PortAudio is
  // initialised against the machine's actual hardware and updateDeviceList()
  // has walked every device the host reports.
  //
  // updateDeviceList() sets ID and both latencies from PaDeviceInfo but never
  // calls setDefaultBufferSize(), because PaDeviceInfo carries no equivalent
  // field. Before the descriptor's members were initialised, that field was
  // whatever happened to be in the freshly-pushed vector slot — the bug's
  // user-visible face, since System().getDevice(n).getDefaultBufferSize() is
  // what a host reads to size its own buffers, and yse_device_default_buffer_
  // size() forwards it verbatim to FFI consumers. It must now read 0 for every
  // enumerated device, meaning "unspecified — the backend picks".
  TEST_CASE("device: enumerated devices report a defined, unspecified buffer size [issue #569]") {
    if (!TestHelpers::engineInit()) return;
    const unsigned int n = YSE::System().getNumDevices();
    if (n == 0) return;

    for (unsigned int i = 0; i < n; i++) {
      const YSE::device& d = YSE::System().getDevice(i);
      CHECK(d.getDefaultBufferSize() == 0);
      // Set from the PortAudio device index, so it identifies this slot rather
      // than carrying the constructor's default through.
      CHECK(d.getID() == (int)i);
      // Latencies come from PaDeviceInfo's defaultLow*Latency, which is never
      // negative; an indeterminate read was free to be.
      CHECK(d.getInputLatency() >= 0);
      CHECK(d.getOutputLatency() >= 0);
    }
  }

  // Issue #661. openDevice() built its PaStreamParameters from
  // `object.out->getID()` and then read `Pa_GetDeviceInfo(...)->
  // defaultHighOutputLatency`, checking neither pointer. Both are null on
  // ordinary API input — `out` for a setup that never had setOutput() called,
  // the PaDeviceInfo* for any index outside [0, Pa_GetDeviceCount()).
  //
  // This has to run against a real, initialised PortAudio to mean anything.
  // The devicelayer suite covers the first guard headless, but it runs the
  // offline engine, whose `initDone` is false — so its openDevice() returns
  // before Pa_GetDeviceInfo() is ever called and the second dereference is
  // unreachable there by construction. Here initDone is true and the calls go
  // through for real.
  //
  // The assertion is that the engine is still alive and still playing
  // afterwards: on the unpatched engine this segfaults inside openDevice() and
  // the suite never reports at all.
  TEST_CASE("device: a malformed openDevice request is refused, not dereferenced [issue #661]") {
    if (!TestHelpers::engineInitWithAudio()) return;
    if (YSE::System().getNumDevices() == 0) return;
    REQUIRE(audioStreamRunning());

    const double rateBefore = YSE::System().getActiveSampleRate();
    REQUIRE(rateBefore > 0.0);

    // (1) No output device in the setup at all — sample rate and buffer size
    //     only, which is all the C API forces a caller to provide.
    {
      YSE::deviceSetup setup;
      setup.setSampleRate(44100.0).setBufferSize(256);
      YSE::System().openDevice(setup, YSE::CT_STEREO);
    }
    CHECK(audioStreamRunning());

    // (2) An output device whose ID no host API resolves — the shape a stale
    //     descriptor has when a device is unplugged between updateDeviceList()
    //     and the open. Two output channel names, so a refusal here cannot be
    //     mistaken for the zero-channel case above.
    {
      YSE::device stale;
      stale.setName("unplugged device")
          .setTypeName("TestHost")
          .addOutputChannelName("out 1")
          .addOutputChannelName("out 2")
          .setID(9999);
      YSE::deviceSetup setup;
      setup.setOutput(stale).setSampleRate(44100.0).setBufferSize(256);
      YSE::System().openDevice(setup, YSE::CT_STEREO);
    }
    CHECK(audioStreamRunning());

    // (3) paNoDevice itself, the sentinel Pa_GetDefaultOutputDevice() returns
    //     on a host with no output — and, since issue #666, the descriptor's
    //     default ID. Set explicitly here so this case keeps testing the guard
    //     rather than the default.
    {
      YSE::device none;
      none.addOutputChannelName("out 1").addOutputChannelName("out 2").setID(-1);
      YSE::deviceSetup setup;
      setup.setOutput(none).setSampleRate(44100.0).setBufferSize(256);
      YSE::System().openDevice(setup, YSE::CT_STEREO);
    }
    CHECK(audioStreamRunning());

    // The device that was already running is still the one running: a refused
    // request must not close the stream out from under the host.
    CHECK(YSE::System().getActiveSampleRate() == rateBefore);
  }

  // Issue #665, the general form of the guard above. Refusing the open is only
  // half the contract — the mixer has to keep following the device that is
  // actually playing. DEVICE::managerObject::openDevice() returned void, so
  // system::openDevice() applied the *requested* speaker layout for every
  // failure path; #661 only stopped the one setup whose channel count is 0.
  //
  // This needs a real stream to mean anything: the assertion is that a refused
  // switch to a six-channel device leaves CHANNEL::Manager() on the layout the
  // running stereo device negotiated. Otherwise the next callback has
  // deviceManager::doOnCallback() resize the master to six outputs and the
  // engine renders six channels into a two-channel stream — which is also why
  // the "still running" check below is not redundant with the layout one.
  TEST_CASE("device: a refused open does not reconfigure the mixer [issue #665]") {
    if (!TestHelpers::engineInitWithAudio()) return;
    if (YSE::System().getNumDevices() == 0) return;
    REQUIRE(audioStreamRunning());

    const UInt outputsBefore = YSE::CHANNEL::Manager().getNumberOfOutputs();
    const double rateBefore = YSE::System().getActiveSampleRate();
    REQUIRE(outputsBefore > 0);
    REQUIRE(rateBefore > 0.0);

    // A well-formed descriptor for a device that is not there: six output
    // channels, and an ID no host API resolves (the shape a stale descriptor
    // has after the device it named was unplugged). Nothing about this setup
    // is zero, so the #661 guard cannot cover it.
    YSE::device sixCh;
    sixCh.setName("unplugged 5.1 device").setTypeName("TestHost").setID(9999);
    for (int i = 0; i < 6; ++i)
      sixCh.addOutputChannelName("out " + std::to_string(i + 1));

    YSE::deviceSetup setup;
    setup.setOutput(sixCh).setSampleRate(44100.0).setBufferSize(256);
    REQUIRE(setup.getOutputChannels() == 6);

    YSE::System().openDevice(setup, YSE::CT_51);

    // The layout belongs to the device that is open, which is still the one
    // that was open before the request.
    CHECK(YSE::CHANNEL::Manager().getNumberOfOutputs() == outputsBefore);
    CHECK(YSE::System().getActiveSampleRate() == rateBefore);
    CHECK(audioStreamRunning());
  }

  // Issue #666, at the level a host actually meets it. A descriptor a host
  // builds itself — rather than taking one from System().getDevices() — now
  // starts at paNoDevice (-1) instead of 0, so an open that forgot to name a
  // device is reported instead of silently landing on whatever device the host
  // enumerated first.
  //
  // Only a real, initialised PortAudio can tell the two apart: index 0 resolves
  // there, so on the old default this request reached Pa_OpenStream() — after
  // close() had already torn down the stream that was playing. The devicelayer
  // suite cannot see any of that (its openDevice() returns at the initDone
  // gate), and the descriptor-level cases in that suite only assert the value.
  //
  // Six channels and CT_51 make the outcome unmistakable in either direction:
  // if the request were honoured against device 0 the mixer would follow it to
  // six outputs, and if it were honoured and then failed in Pa_OpenStream() the
  // stream would be gone. Refused, everything below is untouched.
  TEST_CASE(
      "device: a descriptor with no device id is refused, not read as device 0 [issue #666]") {
    if (!TestHelpers::engineInitWithAudio()) return;
    if (YSE::System().getNumDevices() == 0) return;
    REQUIRE(audioStreamRunning());

    const UInt outputsBefore = YSE::CHANNEL::Manager().getNumberOfOutputs();
    const double rateBefore = YSE::System().getActiveSampleRate();
    REQUIRE(outputsBefore > 0);
    REQUIRE(rateBefore > 0.0);

    // Everything a host would fill in except the id, which is the whole point:
    // the descriptor is well-formed enough that no other guard covers it.
    YSE::device unnamed;
    unnamed.setName("hand-built device").setTypeName("TestHost");
    for (int i = 0; i < 6; ++i)
      unnamed.addOutputChannelName("out " + std::to_string(i + 1));
    REQUIRE(unnamed.getID() == -1);

    YSE::deviceSetup setup;
    setup.setOutput(unnamed).setSampleRate(44100.0).setBufferSize(256);
    REQUIRE(setup.getOutputChannels() == 6);

    YSE::System().openDevice(setup, YSE::CT_51);

    CHECK(YSE::CHANNEL::Manager().getNumberOfOutputs() == outputsBefore);
    CHECK(YSE::System().getActiveSampleRate() == rateBefore);
    CHECK(audioStreamRunning());
  }

  // ─── MIDI device enumeration (gated on YSE_ENABLE_MIDI_DEVICE) ───────────────

#if YSE_WINDOWS && YSE_ENABLE_MIDI_DEVICE
  TEST_CASE("midi: getNumMidiInDevices does not crash") {
    if (!TestHelpers::engineInit()) return;
    (void)YSE::System().getNumMidiInDevices();
    CHECK(true);
  }

  TEST_CASE("midi: getNumMidiOutDevices does not crash") {
    if (!TestHelpers::engineInit()) return;
    (void)YSE::System().getNumMidiOutDevices();
    CHECK(true);
  }

  TEST_CASE("midi: device names are accessible without crash") {
    if (!TestHelpers::engineInit()) return;
    unsigned int nIn = YSE::System().getNumMidiInDevices();
    unsigned int nOut = YSE::System().getNumMidiOutDevices();
    for (unsigned int i = 0; i < nIn; i++)
      (void)YSE::System().getMidiInDeviceName(i);
    for (unsigned int i = 0; i < nOut; i++)
      (void)YSE::System().getMidiOutDeviceName(i);
    CHECK(true);
  }
#endif // YSE_WINDOWS && YSE_ENABLE_MIDI_DEVICE

  // ─── Engine lifecycle ─────────────────────────────────────────────────────────

  TEST_CASE("engine: init succeeds and channel master is valid") {
    bool ok = TestHelpers::engineInit();
    CHECK(ok);
    CHECK(TestHelpers::engineInitialized());
  }

  TEST_CASE("engine: repeated update calls do not crash") {
    if (!TestHelpers::engineInit()) return;
    for (int i = 0; i < 5; i++) {
      YSE::System().sleep(10);
      YSE::System().update();
    }
    CHECK(true);
  }

  TEST_CASE("engine: cpuLoad returns a non-negative value") {
    if (!TestHelpers::engineInit()) return;
    CHECK(YSE::System().cpuLoad() >= 0.0f);
  }

  // Issue #82: cpuLoad() is our own callback wall-clock / buffer-period EMA.
  // Without any user graph attached, the engine's per-callback work is tiny;
  // the smoothed reading should sit well below 1.0 (would mean callback taking
  // the entire buffer period). 0.5 is a generous ceiling that catches a
  // totally broken measurement without being flaky on slow CI machines.
  TEST_CASE("engine: cpuLoad stays bounded with no graph activity") {
    if (!TestHelpers::engineInitWithAudio()) return;
    if (YSE::System().getNumDevices() == 0) return;
    // Let callbacks fire long enough for the ~1 s EMA to settle.
    for (int i = 0; i < 30; i++) {
      YSE::System().sleep(50);
      YSE::System().update();
    }
    const float load = YSE::System().cpuLoad();
    CHECK(load >= 0.0f);
    CHECK(load < 0.5f);
  }

  // Issue #82 regression guard. Original report: cpuLoad stayed elevated for
  // seconds after sound.stop(), then was eventually root-caused to YSE's
  // default empty child channels (ambient/fx/music/gui/voice — see
  // system.cpp's initShared) being dispatched to the fast threadpool every
  // render. The audio thread's join() on those workers spin-slept in 2 ms
  // increments, adding ~10 ms of fake wall-clock per callback once the
  // source was silent (during playback the source work hid the wait).
  //
  // Fixed by skipping addFastJob for children with no work in
  // channelImplementation::dsp(); this test samples cpuLoad once per
  // second through idle → playing → stopped phases and asserts the
  // stopped reading decays back to the idle baseline. CSV trace written
  // to a deterministic temp path for offline correlation.
  //
  // The graph mirrors AudioTest (11 sines + low-pass), the same DSP
  // shape from the original repro, so the test also exercises the
  // FTZ/DAZ path on the audio thread (issue #53 / PR #70).
  //
  // Total runtime: ~13 s on a Release build. Run explicitly with:
  //
  //   python yse.py test --integration
  //   yse_tests --test-suite=integration --test-case='*cpuLoad recovers*'
  TEST_CASE("engine: cpuLoad recovers after multi-sine playback stops [issue #82]") {
    if (!TestHelpers::engineInitWithAudio()) return;
    if (YSE::System().getNumDevices() == 0) return;

    auto sample_cpu = []() { return YSE::System().cpuLoad(); };

    // Let the EMA settle (~1 s time constant) before the trace begins so
    // we have a clean idle baseline to compare the post-stop reading to.
    for (int i = 0; i < 4; i++) {
      YSE::System().sleep(500);
      YSE::System().update();
    }
    const float idle_baseline = sample_cpu();

    struct Sample {
      int t_secs;
      const char* phase;
      float cpu;
    };
    std::vector<Sample> trace;
    trace.push_back({0, "idle", idle_baseline});

    YSE::sound s;
    s.create(g_multi_sine);
    s.relative(true);
    s.play();

    // 5 seconds of active playback.
    for (int i = 1; i <= 5; i++) {
      YSE::System().sleep(1000);
      YSE::System().update();
      trace.push_back({i, "playing", sample_cpu()});
    }

    s.stop();

    // 5 seconds after stop. By the end of this window the ~1 s EMA should
    // have decayed any "playing" contribution to under 1 %, so the final
    // reading should sit very close to the idle baseline.
    for (int i = 1; i <= 5; i++) {
      YSE::System().sleep(1000);
      YSE::System().update();
      trace.push_back({5 + i, "stopped", sample_cpu()});
    }

    // Drop the trace to a deterministic temp path. Whoever runs the test
    // can diff this against an external CPU probe to confirm whether the
    // engine reading still drifts up under their setup.
    const auto log_path = std::filesystem::temp_directory_path() / "yse_cpuload_issue82.csv";
    {
      std::ofstream csv(log_path);
      csv << "t_secs,phase,cpu_load\n";
      for (const auto& smp : trace) {
        csv << smp.t_secs << "," << smp.phase << "," << smp.cpu << "\n";
      }
    }
    MESSAGE("cpuLoad trace written to " << log_path.string());

    float max_playing = 0.f;
    for (int i = 1; i <= 5; i++)
      max_playing = std::max(max_playing, trace[i].cpu);
    const float final_stopped = trace.back().cpu;

    INFO("idle_baseline = " << idle_baseline);
    INFO("max_playing   = " << max_playing);
    INFO("final_stopped = " << final_stopped);

    // Sanity bounds: nothing negative, no impossible (>1.0) values.
    for (const auto& smp : trace) {
      CHECK(smp.cpu >= 0.0f);
      CHECK(smp.cpu < 1.5f);
    }

    // The recovery assertion. 5 s after stop with a ~1 s tau, the playing
    // component has decayed by e^-5 ≈ 0.7 %, so the final reading must
    // sit very close to the idle baseline. Allow a generous 25 % of
    // max_playing as slack for slow machines and EMA noise; if the
    // engine still reports half its playing value at this point, the
    // bug from the original report is back.
    CHECK(final_stopped <= max_playing * 0.25f + idle_baseline + 0.01f);
  }

  // ─── Audio callback ───────────────────────────────────────────────────────────

  TEST_CASE("engine: audio callback fires within 100ms on a real output device") {
    if (!TestHelpers::engineInitWithAudio()) return;
    if (YSE::System().getNumDevices() == 0) return;
    CHECK(audioStreamRunning());
  }

  // The watchdog's side of that same start-up window (issue #681). A stream
  // that has just been started delivers nothing for the first tens of
  // milliseconds, which is indistinguishable from a disconnected device by
  // callback count alone. autoReconnect's remedy is pause() + resume(), and the
  // reopened stream is right back inside that window — so a host polling
  // update() faster than its device starts used to tear down a perfectly
  // healthy device on every tick and never hear a sample.
  //
  // Driven exactly as such a host would: reconnection enabled with the delay
  // that ships by default (0), a genuinely fresh start-up window, and update()
  // pumped as fast as the loop turns. The device has to reach "delivering
  // callbacks" regardless. Before the fix this loop never got there — every
  // tick closed the stream that was about to deliver.
  TEST_CASE("engine: autoReconnect leaves a starting device alone [issue #681]") {
    if (!TestHelpers::engineInitWithAudio()) return;
    if (YSE::System().getNumDevices() == 0) return;

    YSE::System().pause();
    // Drain the callbacks the running device delivered between the previous
    // case's last update() and the pause above; without this the first update()
    // in the loop would report a live device on their evidence rather than on
    // the restarted stream's. Done with the watchdog still off, so draining
    // cannot itself trigger a reconnect.
    YSE::System().update();

    YSE::System().autoReconnect(true, 0);
    YSE::System().resume();

    bool delivering = false;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (std::chrono::steady_clock::now() < deadline) {
      YSE::System().update();
      if (YSE::System().missedCallbacks() == 0) {
        delivering = true;
        break;
      }
      YSE::System().sleep(1);
    }
    // Restore the default before asserting: a CHECK that fails must not leave
    // the watchdog armed for the rest of the suite.
    YSE::System().autoReconnect(false, 0);
    CHECK(delivering);
  }

  // ─── Sound loading and playback ───────────────────────────────────────────────

  TEST_CASE("sound: WAV fixture creates a valid sound on a real device") {
    if (!TestHelpers::engineInit()) return;
    YSE::sound s;
    s.create(WAV_FIXTURE);
    CHECK(s.isValid());
  }

  TEST_CASE("sound: playing WAV fixture does not crash during update loop") {
    if (!TestHelpers::engineInit()) return;
    YSE::sound s;
    s.create(WAV_FIXTURE);
    if (!s.isValid()) return;
    s.relative(true);
    s.play();
    for (int i = 0; i < 10; i++) {
      YSE::System().sleep(20);
      YSE::System().update();
    }
    CHECK(true);
  }

  TEST_CASE("sound: DSP source sound plays on a real device without crash") {
    if (!TestHelpers::engineInitWithAudio()) return;
    if (YSE::System().getNumDevices() == 0) return;
    YSE::sound s;
    s.create(g_source);
    s.relative(true);
    s.play();
    for (int i = 0; i < 5; i++) {
      YSE::System().sleep(20);
      YSE::System().update();
    }
    s.stop();
    CHECK(true);
  }

  // ─── End-to-end signal probe ──────────────────────────────────────────────────

  TEST_CASE("engine: DSP source signal reaches an attached effect processor") {
    if (!TestHelpers::engineInitWithAudio()) return;
    if (YSE::System().getNumDevices() == 0) return;
    if (!audioStreamRunning()) return;

    g_probe.reset();
    YSE::sound s;
    s.create(g_source);
    s.setDSP(&g_probe);
    s.relative(true);
    s.play();

    for (int i = 0; i < 10 && !g_probe.triggered.load(std::memory_order_relaxed); i++) {
      YSE::System().sleep(50);
      YSE::System().update();
    }
    s.stop();

    CHECK(g_probe.triggered.load(std::memory_order_relaxed));
  }

  // ─── moveTo() reparenting, end to end ────────────────────────────────────────

  // Regression guard for #656. YSE::sound::_parent — the interface-side cache
  // moveTo() compares against to skip redundant MOVE messages — had no
  // initialiser, so on a freshly constructed sound it held whatever bytes
  // happened to be at that address. When those bytes matched the move target,
  // moveTo() short-circuited: no MOVE message, the sound stayed on the channel
  // create() gave it, and the caller got silence on the channel it asked for.
  //
  // The repro makes that collision deterministic instead of waiting for luck:
  // a first sound is built in a fixed block of storage and moved to `target`,
  // which writes &target into the block's _parent slot; the sound under test is
  // then placement-constructed over the *same* bytes. Before the fix its
  // _parent read &target straight back. This is exercised end to end — real
  // device, real audio callback — because "did the move actually happen?" is
  // only answerable from the target channel's own output meter.
  TEST_CASE("sound: first moveTo() reparents a freshly created sound [issue #656]") {
    if (!TestHelpers::engineInitWithAudio()) return;
    if (YSE::System().getNumDevices() == 0) return;
    if (!audioStreamRunning()) return;

    YSE::channel target;
    target.create("move_target_656", YSE::ChannelMaster());
    for (int i = 0; i < 10; i++) {
      YSE::System().sleep(10);
      YSE::System().update();
    }
    REQUIRE(target.isValid());
    REQUIRE(target.getPeakLinearPost() == doctest::Approx(0.f));

    // Sound-sized, sound-aligned block reused across both constructions. Static
    // so the storage outlives the case even if an assertion unwinds early.
    alignas(YSE::sound) static unsigned char storage[sizeof(YSE::sound)];

    // Pass 1 — dirty the block: this sound's moveTo() stores &target in the
    // _parent slot, and ~sound() leaves those bytes untouched.
    {
      YSE::sound* dirty = new (static_cast<void*>(storage)) YSE::sound();
      dirty->create(g_source);
      REQUIRE(dirty->isValid());
      dirty->moveTo(target);
      dirty->~sound();
    }

    // Pass 2 — the sound under test, built over the poisoned bytes.
    YSE::sound* s = new (static_cast<void*>(storage)) YSE::sound();
    s->create(g_source);
    REQUIRE(s->isValid());
    s->relative(true);
    s->moveTo(target);
    s->play();

    float peak = 0.f;
    for (int i = 0; i < 20 && peak <= 0.f; i++) {
      YSE::System().sleep(50);
      YSE::System().update();
      peak = target.getPeakLinearPost();
    }
    s->stop();
    YSE::System().sleep(50);
    YSE::System().update();
    s->~sound();

    // Non-zero only if the MOVE message actually reached the implementation:
    // without it the sound is still rendering into MainMix and `target` — which
    // create() never attached anything to — meters pure silence.
    CHECK(peak > 0.f);
  }

} // TEST_SUITE("integration")
