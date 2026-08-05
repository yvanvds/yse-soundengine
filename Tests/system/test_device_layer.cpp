// Device / audio-IO layer tests (issue #418, part of the SonarCloud
// quality-gate epic #420): YseEngine/device/deviceInterface.cpp,
// YseEngine/device/deviceSetup.cpp, the headless-reachable half of
// YseEngine/device/portaudioDeviceManager.cpp, and YseEngine/internal/AudioTest.cpp.
//
// WHAT CI CAN AND CANNOT REACH
//
// .github/workflows/build.yml documents at length why a real audio stream
// cannot be opened on a GitHub Actions runner (no snd-aloop/snd-dummy in
// linux-modules-extra, a PulseAudio null sink stays invisible to PortAudio's
// ALSA backend, jackd2's dummy backend aborts on mlockall without CAP_IPC_LOCK).
// That constraint is about *opening a stream*. It is not about the code that
// runs before, around, and instead of one, which is what this file covers:
//
//   * deviceInterface.cpp / deviceSetup.cpp are pure descriptor plumbing with
//     no PortAudio call in them at all — fully testable with no hardware.
//   * portaudioDeviceManager.cpp's error and enumeration paths are reachable
//     precisely *because* the runner is headless: with PortAudio never
//     initialised (the offline engine skips Pa_Initialize on purpose, see
//     managerObject::init), Pa_GetDeviceCount returns paNotInitialized and
//     Pa_GetDefaultOutputDevice returns paNoDevice. Those are the branches the
//     tests below drive, deterministically, on a developer desktop *and* on CI.
//   * paCallback's mix-copy body needs no device either: it is a static
//     function taking the output buffer as an argument, so the test hands it
//     plain memory and drives it synchronously. Only Pa_OpenStream /
//     Pa_StartStream are genuinely out of reach, and everything gated behind
//     them (`open` / `started` true, the negotiated-latency cache, the ASIO
//     latency query) stays uncovered by design rather than by omission.
//
// No `sonar.coverage.exclusions` entry is added: nothing here turned out to be
// device-bound enough to need one.
//
// ISOLATION
//
// The suite runs in its own ctest process (Tests/CMakeLists.txt) for the same
// family of reasons as buscapi / channelcapi / sendstress: it drives
// System::initOffline() (process-global engine state), it mutates the
// process-global DEVICE::Manager() singleton (updateDeviceList clears the
// device list, close() resets the live-state atomics), and it calls paCallback
// on the test thread — which runs the manager update() functions that are
// single-threaded by contract (see Tests/support/null_device.hpp).
//
// DSP lifetime note: as in test_device.cpp, DSP source objects handed to
// sound::create() must outlive the test binary, so they are file-scope statics.

#include <doctest/doctest.h>

#include <chrono>
#include <cmath>
#include <string>
#include <thread>
#include <vector>

#include "yse.hpp"

#include "channel/channelImplementation.h"
#include "channel/channelInterface.hpp"
// channelImplementation.h instantiates lfQueue<CHANNEL::messageObject>, which
// needs the message type complete by the end of the TU.
#include "channel/channelMessage.h"
#include "device/deviceInterface.hpp"
#include "device/deviceSetup.hpp"
#include "headers/constants.hpp"
#include "internal/AudioTest.h"
#include "sound/soundInterface.hpp"

#ifdef PORTAUDIO_BACKEND
#include "device/portaudioDeviceManager.h"
#endif

namespace {

  // Init the offline engine once for the whole devicelayer process. Mirrors the
  // sendstress / capisurface suites' ensureOffline(); no audio hardware needed,
  // and initOffline() deliberately skips Pa_Initialize.
  bool ensureOffline() {
    static bool done = false;
    static bool ok = false;
    if (!done) {
      YSE::System().close(); // normalize regardless of starting state
      ok = YSE::System().initOffline();
      done = true;
    }
    return ok;
  }

  // Drive the engine to quiescence: update() flags the control-plane work,
  // renderOffline() runs the audio callback body, and the short sleep lets the
  // single-threaded slow pool execute the queued setup() jobs. Offline analogue
  // of the channel suite's drainChannels(); copied from the sendstress suite.
  void pump(int iterations = 20) {
    for (int i = 0; i < iterations; ++i) {
      YSE::System().update();
      YSE::System().renderOffline(2);
      std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
  }

  // A fully populated descriptor. The engine's PortAudio enumerator is the only
  // other producer and it needs real hardware, so the tests build one by hand.
  // Every scalar field is set explicitly — the default constructor leaves the
  // int members indeterminate (filed separately, see the PR for #418).
  YSE::device makeDevice() {
    YSE::device d;
    d.setName("Test Output Device")
        .setTypeName("TestHost")
        .addOutputChannelName("out 1")
        .addOutputChannelName("out 2")
        .addOutputChannelName("out 3")
        .addInputChannelName("in 1")
        .addAvailableSampleRate(44100.0)
        .addAvailableSampleRate(48000.0)
        .addAvailableBufferSize(256)
        .addAvailableBufferSize(512)
        .setDefaultBufferSize(512)
        .setOutputLatency(128)
        .setInputLatency(64)
        .setID(7);
    return d;
  }

  // Constant, deliberately out-of-range source. Its only job is to keep
  // SOUND::Manager() non-empty so paCallback's doOnCallback() gate opens and
  // the mix-copy body below actually runs. File scope: the create() contract
  // requires the source to outlive every sound built from it.
  struct SteadySource : YSE::DSP::dspSourceObject {
    // DSP::dspSourceObject declares no destructor of its own; give this one a
    // virtual destructor so the class is not deleted through a non-virtual base.
    virtual ~SteadySource() = default;

    void process(YSE::SOUND_STATUS& intent) override {
      for (UInt c = 0; c < samples.size(); ++c) {
        float* p = samples[c].getPtr();
        const UInt len = samples[c].getLength();
        for (UInt i = 0; i < len; ++i)
          p[i] = 0.5f;
      }
      intent = YSE::SS_PLAYING;
    }
    void frequency(float) override {}
  };

  SteadySource g_steady;

  float clampToUnit(float v) {
    return v < -1.f ? -1.f : (v > 1.f ? 1.f : v);
  }

} // namespace

TEST_SUITE("devicelayer") {

  // ─── device descriptor (deviceInterface.cpp) ────────────────────────────────
  //
  // Pure data plumbing: no PortAudio symbol is reachable from this translation
  // unit, so these need neither hardware nor an initialised engine.

  TEST_CASE("device: a default-constructed descriptor is empty") {
    YSE::device d;
    CHECK(d.getName().empty());
    CHECK(d.getTypeName().empty());
    CHECK(d.getNumOutputChannelNames() == 0);
    CHECK(d.getNumInputChannelNames() == 0);
    CHECK(d.getNumAvailableSampleRates() == 0);
    CHECK(d.getNumAvailableBufferSizes() == 0);
    CHECK(d.getOutputChannelNames().empty());
    CHECK(d.getInputChannelNames().empty());
    CHECK(d.getAvailableSampleRates().empty());
    CHECK(d.getAvailableBufferSizes().empty());
  }

  TEST_CASE("device: every setter round-trips through its getter") {
    YSE::device d = makeDevice();
    CHECK(d.getName() == "Test Output Device");
    CHECK(d.getTypeName() == "TestHost");
    CHECK(d.getDefaultBufferSize() == 512);
    CHECK(d.getOutputLatency() == 128);
    CHECK(d.getInputLatency() == 64);
    CHECK(d.getID() == 7);
  }

  // The vector accessors are the DLL-unsafe half of the descriptor API (see the
  // doc comments in deviceInterface.hpp); the indexed pair is what the C API
  // and any dynamically linked host uses. They must agree, or a host reading
  // one and a test asserting on the other would diverge silently.
  TEST_CASE("device: the vector accessors agree with the indexed accessors") {
    YSE::device d = makeDevice();

    const std::vector<std::string>& outs = d.getOutputChannelNames();
    REQUIRE(outs.size() == d.getNumOutputChannelNames());
    REQUIRE(outs.size() == 3);
    for (unsigned int i = 0; i < d.getNumOutputChannelNames(); ++i)
      CHECK(outs[i] == d.getOutputChannelName(i));

    const std::vector<std::string>& ins = d.getInputChannelNames();
    REQUIRE(ins.size() == d.getNumInputChannelNames());
    REQUIRE(ins.size() == 1);
    for (unsigned int i = 0; i < d.getNumInputChannelNames(); ++i)
      CHECK(ins[i] == d.getInputChannelName(i));

    const std::vector<double>& rates = d.getAvailableSampleRates();
    REQUIRE(rates.size() == d.getNumAvailableSampleRates());
    REQUIRE(rates.size() == 2);
    for (unsigned int i = 0; i < d.getNumAvailableSampleRates(); ++i)
      CHECK(rates[i] == doctest::Approx(d.getAvailableSampleRate(i)));

    const std::vector<int>& sizes = d.getAvailableBufferSizes();
    REQUIRE(sizes.size() == d.getNumAvailableBufferSizes());
    REQUIRE(sizes.size() == 2);
    for (unsigned int i = 0; i < d.getNumAvailableBufferSizes(); ++i)
      CHECK(sizes[i] == d.getAvailableBufferSize(i));
  }

  // ─── device setup (deviceSetup.cpp) ─────────────────────────────────────────

  TEST_CASE("device setup: a fresh setup reports no output channels") {
    YSE::deviceSetup setup;
    // The out == nullptr guard. A host that builds a setup and queries it
    // before choosing a device must get 0, not a null dereference — and
    // system::openDevice() feeds this straight into
    // CHANNEL::Manager().setChannelConf().
    CHECK(setup.getOutputChannels() == 0);
  }

  TEST_CASE("device setup: the output channel count follows the attached device") {
    YSE::device out = makeDevice();
    YSE::deviceSetup setup;
    setup.setOutput(out);
    CHECK(setup.getOutputChannels() == 3);

    // Re-pointing at a different device re-reads the count rather than caching it.
    YSE::device mono;
    mono.addOutputChannelName("out 1");
    setup.setOutput(mono);
    CHECK(setup.getOutputChannels() == 1);
  }

  TEST_CASE("device setup: every setter returns the same setup for chaining") {
    YSE::device in = makeDevice();
    YSE::device out = makeDevice();
    YSE::deviceSetup setup;

    // The fluent chain is the documented way to build a setup; each link must
    // hand back the same object or later links would configure a temporary.
    YSE::deviceSetup& chained =
        setup.setInput(in).setOutput(out).setSampleRate(48000.0).setBufferSize(256);
    CHECK(&chained == &setup);
    CHECK(setup.getOutputChannels() == 3);
  }

#ifdef PORTAUDIO_BACKEND

  // ─── PortAudio manager: headless-reachable paths ────────────────────────────

  TEST_CASE("device manager: the backend singleton is stable") {
    CHECK(&YSE::DEVICE::Manager() == &YSE::DEVICE::Manager());
  }

  // close() on a manager that never opened a stream must be a clean no-op: both
  // the `started` and the `open` guards stay false, and the live-state atomics
  // are reset. Every getter then reports the documented "no device" zero.
  TEST_CASE("device manager: closing an unopened device leaves the live getters at zero") {
    if (!ensureOffline()) return;

    YSE::System().closeCurrentDevice();
    YSE::System().closeCurrentDevice(); // idempotent

    CHECK(YSE::System().getActiveSampleRate() == 0.0);
    CHECK(YSE::System().getActiveBufferSize() == 0);
    CHECK(YSE::System().getActiveOutputLatency() == 0);
    CHECK(YSE::System().cpuLoad() == 0.f);
  }

  // Pa_GetDeviceCount() returns paNotInitialized (a negative PaError) while
  // PortAudio has not been initialised — which is exactly the offline engine's
  // state, and also a bare CI runner's. That drives updateDeviceList()'s
  // count < 0 error branch: the list must come back empty rather than being
  // walked with a negative bound.
  TEST_CASE("device manager: enumeration fails closed when PortAudio is not initialised") {
    if (!ensureOffline()) return;

    YSE::DEVICE::Manager().updateDeviceList();

    CHECK(YSE::System().getNumDevices() == 0);
    CHECK(YSE::DEVICE::Manager().getDeviceList().empty());
    // The error path must not have invented a default either.
    CHECK(YSE::System().getDefaultDevice().empty());
  }

  // openDevice() is gated on initDone: with PortAudio uninitialised it must
  // refuse before touching Pa_GetDeviceInfo, which would otherwise be called
  // with a device index no host API can resolve.
  TEST_CASE("device manager: opening a device before PortAudio init is refused") {
    if (!ensureOffline()) return;

    YSE::device out = makeDevice();
    YSE::deviceSetup setup;
    setup.setOutput(out).setSampleRate(44100.0).setBufferSize(256);

    YSE::DEVICE::Manager().openDevice(setup);

    CHECK(YSE::System().getActiveSampleRate() == 0.0);
    CHECK(YSE::System().getActiveBufferSize() == 0);
    CHECK(YSE::System().getActiveOutputLatency() == 0);
  }

  // resume() → addCallback() → Pa_GetDefaultOutputDevice(), which returns
  // paNoDevice on a host with no default output *and* whenever PortAudio was
  // never initialised. The engine must log and return rather than dereference
  // the null PaDeviceInfo that Pa_GetDeviceInfo(paNoDevice) hands back.
  TEST_CASE("device manager: resume without a default output device does not open a stream") {
    if (!ensureOffline()) return;

    YSE::System().pause();
    YSE::System().resume();

    CHECK(YSE::System().getActiveSampleRate() == 0.0);
    CHECK(YSE::System().getActiveBufferSize() == 0);
    CHECK(YSE::System().getActiveOutputLatency() == 0);
  }

  // GetCallbacksSinceLastUpdate() is a read-and-reset exchange (issue #198), so
  // a second read with no callback in between must be 0. system::update()
  // depends on that to decide whether the device stalled.
  TEST_CASE("device manager: the callback counter reads and resets") {
    if (!ensureOffline()) return;

    (void)YSE::DEVICE::Manager().GetCallbacksSinceLastUpdate();
    CHECK(YSE::DEVICE::Manager().GetCallbacksSinceLastUpdate() == 0);
  }

  // The audio callback body, driven synchronously with plain memory as the
  // "device" buffer. Two things are asserted, and neither needs a stream:
  //
  //   1. The early-out. With no sound alive, doOnCallback() returns false and
  //      paCallback must return 0 having touched nothing but its own counters —
  //      the negotiated framesPerBuffer is still captured (the stream is opened
  //      with paFramesPerBufferUnspecified, so the first callback is the only
  //      place that value can come from).
  //   2. The mix copy. PortAudio is opened non-interleaved, and the copy
  //      clamps to [-1, 1] through an 8-way unrolled loop plus a scalar
  //      remainder, striping a 128-sample engine block across callbacks of an
  //      unrelated size. A 100-frame callback leaves 28 samples of the rendered
  //      block unconsumed; the next 28-frame callback must copy exactly those,
  //      clamped, *without* re-rendering. 28 = 3×8 + 4 exercises both loops.
  TEST_CASE("device manager: the audio callback clamps the mix and stitches partial blocks") {
    if (!ensureOffline()) return;

    auto& mgr = YSE::DEVICE::Manager();
    auto& master = mgr.getMaster();
    const size_t channels = master.GetBuffers().size();
    REQUIRE(channels > 0);

    const unsigned long kFirst = 100;
    const unsigned long kRest = YSE::STANDARD_BUFFERSIZE - kFirst;

    std::vector<std::vector<float>> storage(channels, std::vector<float>(YSE::STANDARD_BUFFERSIZE));
    std::vector<float*> planes;
    planes.reserve(channels);
    for (size_t c = 0; c < channels; ++c)
      planes.push_back(storage[c].data());

    // PortAudio hands the callback its non-interleaved output as a void* that
    // the callback casts back to Flt**; mirror that explicitly here.
    void* const output = static_cast<void*>(planes.data());

    // (1) No sound alive yet — doOnCallback() gates the render off.
    for (auto& plane : storage)
      std::fill(plane.begin(), plane.end(), 12345.f);
    CHECK(YSE::DEVICE::managerObject::paCallback(nullptr, output, kFirst, nullptr, 0, &mgr) == 0);
    CHECK(mgr.getActiveBufferSize() == (int)kFirst);
    // Nothing was written: the guard returned before the copy loop.
    CHECK(storage[0][0] == 12345.f);

    // (2) Attach a live source so the render/copy path opens.
    YSE::sound s;
    s.create(g_steady);
    s.relative(true);
    s.play();
    pump();

    CHECK(YSE::DEVICE::managerObject::paCallback(nullptr, output, kFirst, nullptr, 0, &mgr) == 0);

    // The tail of the block just rendered is still unconsumed. Overwrite it
    // with a known pattern that straddles the clamp limits in both directions;
    // the next callback must hand those exact samples through, clamped, with no
    // second render in between.
    std::vector<float> pattern(kRest);
    for (unsigned long i = 0; i < kRest; ++i)
      pattern[i] = ((i % 2) == 0 ? 1.f : -1.f) * 0.25f * (float)i;
    for (size_t c = 0; c < channels; ++c) {
      float* block = master.GetBuffers()[c].getPtr();
      for (unsigned long i = 0; i < kRest; ++i)
        block[kFirst + i] = pattern[i];
    }

    for (auto& plane : storage)
      std::fill(plane.begin(), plane.end(), 0.f);
    CHECK(YSE::DEVICE::managerObject::paCallback(nullptr, output, kRest, nullptr, 0, &mgr) == 0);

    for (size_t c = 0; c < channels; ++c) {
      for (unsigned long i = 0; i < kRest; ++i) {
        INFO("channel " << c << " sample " << i);
        CHECK(storage[c][i] == doctest::Approx(clampToUnit(pattern[i])));
      }
    }

    s.stop();
    pump();

    // Leave the singleton as the other cases expect to find it.
    YSE::System().closeCurrentDevice();
    CHECK(YSE::System().getActiveBufferSize() == 0);
  }

#endif // PORTAUDIO_BACKEND

  // ─── built-in diagnostic tone (internal/AudioTest.cpp) ──────────────────────
  //
  // Decision for issue #418's "test it / gate it / remove it" question: TEST IT.
  //
  // AudioTest is not a scratch path — it is the engine's built-in output
  // diagnostic, reachable from the documented public API
  // (YSE::System().AudioTest(bool)), from the C API (yse_system_audio_test),
  // and from Demo12. Neither the shepard tone nor its low-pass touches any
  // platform or device API: it is a dspSourceObject like any other, so it
  // renders through the offline engine and needs no exclusion.
  //
  // Kept last in the file on purpose. YSE::INTERNAL::Test() constructs a
  // function-local static whose destructor deletes the DSP source that its own
  // sound member still refers to, so the tone is stopped and drained here
  // rather than left running into static teardown.
  TEST_CASE("audio test: the built-in diagnostic tone renders through the offline engine") {
    if (!ensureOffline()) return;

    auto& master = YSE::DEVICE::Manager().getMaster();
    REQUIRE(master.GetBuffers().size() > 0);

    YSE::INTERNAL::Test().On(true);
    pump();

    // The shepard tone is 11 parallel sine octaves through a low-pass; a few
    // rendered blocks in, the master mix must carry signal. Anything finite and
    // non-silent proves the source ran — asserting a level would be asserting
    // the mixer's gain staging, which belongs to the channel suite.
    bool sawSignal = false;
    for (int block = 0; block < 32 && !sawSignal; ++block) {
      YSE::System().renderOffline(1);
      const float* p = master.GetBuffers()[0].getPtr();
      const UInt len = master.GetBuffers()[0].getLength();
      for (UInt i = 0; i < len; ++i) {
        CHECK(std::isfinite(p[i]));
        if (std::fabs(p[i]) > 1e-6f) sawSignal = true;
      }
    }
    CHECK(sawSignal);

    YSE::INTERNAL::Test().On(false);
    pump();
  }

} // TEST_SUITE("devicelayer")
