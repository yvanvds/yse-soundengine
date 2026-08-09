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
// That isolation is a *requirement*, not a convenience, and it is the one
// property here that no test can establish for itself (issue #717). The
// no-device branches below exist only while PortAudio has never been
// initialised, which is a property of the process: Pa_Initialize runs once, on
// the first init() that opens a device, and the only call that undoes it —
// managerObject::terminate() — is private and runs from the singleton's
// destructor at process exit. ensureOffline() can take the *stream* away, which
// is all initOffline() promises; it cannot take PortAudio away. So this suite
// and any suite that opens a device are mutually exclusive by construction, in
// both directions — see the mirror note in Tests/system/test_system_active_state.cpp.
// noDeviceReachable() below states that contract, one case asserts it, and the
// cases that depend on it skip with a message instead of failing on the symptom.
//
// DSP lifetime note: as in test_device.cpp, DSP source objects handed to
// sound::create() must outlive the test binary, so they are file-scope statics.

#include <doctest/doctest.h>

#include <chrono>
#include <cmath>
#include <cstring>
#include <new>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "yse.hpp"

#include "channel/channelImplementation.h"
#include "channel/channelInterface.hpp"
// channelImplementation.h instantiates lfQueue<CHANNEL::messageObject>, which
// needs the message type complete by the end of the TU.
#include "channel/channelMessage.h"
#include "channel/channelManager.h"
#include "device/deviceInterface.hpp"
#include "device/deviceSetup.hpp"
#include "headers/constants.hpp"
#include "log.hpp"
#include "sound/soundInterface.hpp"

// DEVICE::Manager() returns a managerObject&, so every use of it below — even
// the backend-agnostic getMaster() inherited from deviceManager — needs the
// concrete manager complete. That class is declared per backend, so mirror
// YseEngine/internalHeaders.h and include whichever one this build selects
// rather than relying on the desktop include chain to drag it in (issue #622).
// PORTAUDIO_BACKEND is set for this TU by Tests/CMakeLists.txt on desktop;
// YSE_ANDROID comes from headers/defines.hpp via yse.hpp above.
#ifdef PORTAUDIO_BACKEND
#include "device/portaudioDeviceManager.h"
#endif

#if YSE_ANDROID
#include "device/androidDeviceManager.h"
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
  // Every scalar field is set explicitly, so these cases assert on values they
  // wrote rather than on the constructor's defaults — those are the #565/#569
  // contract, covered by the placement-new case below and by the capisurface
  // suite through the C ABI.
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
    // The hand-rolled virtual destructor this used to carry is no longer
    // needed: DSP::dspSourceObject declares one itself as of #573.
    ~SteadySource() override = default;

    void process(YSE::SOUND_STATUS& intent) override {
      for (UInt c = 0; c < samples.size(); ++c) {
        float* p = samples[c].getPtr();
        const UInt len = samples[c].getLength();
        for (UInt i = 0; i < len; ++i)
          p[i] = 0.5f;
      }
      // Honour a stop request, the same way AudioTest.cpp's shepard does. This
      // used to force SS_PLAYING unconditionally, which made the `s.stop()` in
      // the paCallback case a no-op: the source kept mixing 0.5 into the master
      // for the whole process and masked the tone case at the end of the file
      // (issue #570).
      if (intent == YSE::SS_WANTSTOSTOP)
        intent = YSE::SS_STOPPED;
      else
        intent = YSE::SS_PLAYING;
    }
    void frequency(float) override {}
  };

  SteadySource g_steady;

  // Collects everything the engine logs while it is installed. The refusal
  // paths under test are only observable as a log line plus an absence of
  // state change, so the line is part of the contract, not decoration — a
  // silently ignored openDevice() call is what made issue #661's null
  // dereference look like "nothing happened" to begin with.
  class CapturingLog : public YSE::logHandler {
  public:
    void AddMessage(const std::string& message) override {
      messages.push_back(message);
    }
    bool contains(const std::string& fragment) const {
      for (const std::string& m : messages)
        if (m.find(fragment) != std::string::npos) return true;
      return false;
    }
    std::vector<std::string> messages;
  };

  // Installs a sink (and a level that lets warnings through) for one case, and
  // puts both back even if an assertion unwinds. Same shape as the logsafety
  // suite's ScopedSink; safe here because the devicelayer suite owns its
  // process (see the ISOLATION note at the top of this file).
  class ScopedSink {
  public:
    explicit ScopedSink(YSE::logHandler* handler) : previousLevel(YSE::Log().getLevel()) {
      YSE::Log().setLevel(YSE::EL_WARNING);
      YSE::Log().setHandler(handler);
    }
    ~ScopedSink() {
      YSE::Log().setHandler(nullptr);
      YSE::Log().setLevel(previousLevel);
    }
    ScopedSink(const ScopedSink&) = delete;
    ScopedSink& operator=(const ScopedSink&) = delete;

  private:
    YSE::ERROR_LEVEL previousLevel;
  };

  // The suite's *process-level* precondition, asked rather than assumed
  // (issue #717).
  //
  // Several cases below drive branches that only exist while the engine can
  // reach no audio device at all: enumeration failing closed, openDevice()
  // refusing ahead of Pa_GetDeviceInfo, resume() finding no default output.
  // In a process of its own — which is how ctest runs this suite — that state
  // is free, because the offline engine skips Pa_Initialize and
  // Pa_GetDeviceCount() then answers paNotInitialized.
  //
  // It is not free in a shared process, and no test can make it so. PortAudio
  // is initialised once per process by the first init() that opens a device,
  // and the only call that undoes it — managerObject::terminate() — is private
  // and runs from the singleton's destructor, i.e. at process exit. So
  // ensureOffline()'s close() + initOffline() hands back an engine with no
  // *stream*, which is all initOffline() promises, over a PortAudio that is
  // still up: the device list enumerates real hardware and resume() opens a
  // real stream. The precondition is a property of the process, not of the
  // engine session, and Tests/CMakeLists.txt's isolated yse_tests_devicelayer
  // entry is what establishes it.
  //
  // Latched: one enumeration for the suite, and the answer cannot swing back —
  // nothing un-initialises PortAudio once some suite has.
  bool noDeviceReachable() {
    static const bool result = []() {
      YSE::DEVICE::Manager().updateDeviceList();
      return YSE::DEVICE::Manager().getDeviceList().empty();
    }();
    return result;
  }

#ifdef PORTAUDIO_BACKEND
  // Guard for those cases: report "not measurable here" instead of failing on
  // the symptom. The contract case below is what keeps this from quietly
  // emptying the suite — it fails wherever the guard closes.
  //
  // Gated on the backend macro for the same reason as clampToUnit() below: its
  // only callers live in this file's PORTAUDIO_BACKEND region, so the
  // Android/Oboe build would otherwise carry an unused function (issue #631).
  // The contract case itself is not gated — it needs no backend.
  bool ownProcess() {
    if (noDeviceReachable()) return true;
    MESSAGE("skipped: an audio device is reachable in this process, so the no-device branch "
            "under test cannot be driven. devicelayer and any suite that opens a device are "
            "mutually exclusive — run this one through the yse_tests_devicelayer ctest entry "
            "(issue #717).");
    return false;
  }
#endif

#ifdef PORTAUDIO_BACKEND
  // Mirrors the clamp paCallback applies on the way out. Its only caller is the
  // mix-copy case below, which lives in this file's PORTAUDIO_BACKEND region —
  // paCallback is declared in portaudioDeviceManager.h and has no Oboe
  // counterpart. Gated on the same macro so the Android/Oboe build, where that
  // region compiles out, does not carry an unused function (issue #631).
  float clampToUnit(float v) {
    return v < -1.f ? -1.f : (v > 1.f ? 1.f : v);
  }
#endif

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

  // The scalar half of the same descriptor, on the public C++ surface a linked
  // application calls directly (issue #569). The capisurface suite asserts the
  // same contract through the C ABI; this case owns it at the layer the fix
  // lives in, so removing the default member initialisers from
  // deviceInterface.hpp fails the suite that covers that file.
  //
  // A plain stack-local device would be an unreliable regression test — the
  // slot is usually already zero. Placement-new over pre-dirtied storage makes
  // an uninitialised read deterministic, the same trick the capisurface suite
  // uses for #565 and test_reverb_dsp.cpp for #263. The fill is 0xAA rather
  // than the 0xFF those use, because 0xFF reads back as -1 for an int and -1 is
  // now the ID's *expected* value (issue #666) — a byte pattern that is neither
  // 0 nor -1 keeps the case able to fail for either field.
  TEST_CASE("device: a default-constructed descriptor's scalars are defined (issues #569, #666)") {
    alignas(YSE::device) unsigned char storage[sizeof(YSE::device)];
    std::memset(storage, 0xAA, sizeof(storage));
    YSE::device* d = new (storage) YSE::device();

    // 0 means "the host advertised nothing", and openDevice() reads it as
    // paFramesPerBufferUnspecified — see the getter's doc comment.
    CHECK(d->getDefaultBufferSize() == 0);
    CHECK(d->getInputLatency() == 0);
    CHECK(d->getOutputLatency() == 0);
    // paNoDevice (-1), not 0: 0 is a valid PortAudio device index, so it could
    // not distinguish "no device chosen" from "device 0" (issue #666). Safe
    // because openDevice() refuses an index no host API resolves instead of
    // dereferencing it (issue #661) — see the member-declaration note in
    // deviceInterface.hpp.
    CHECK(d->getID() == -1);

    d->~device();
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

  // ─── the suite's own contract (issue #717) ──────────────────────────────────

  // The precondition every no-device case below inherits, stated as a case of
  // its own so that a run which does not satisfy it says so once, by name,
  // instead of producing a spray of unexplained value mismatches.
  //
  // It is deliberately a failure and not a skip. The guards below are the only
  // thing standing between this suite and silently measuring nothing, so if the
  // isolation in Tests/CMakeLists.txt is ever loosened — devicelayer folded
  // back into yse_unit_tests, say — this case has to be what fails, rather than
  // five cases quietly passing without asserting anything.
  //
  // Placed ahead of the PORTAUDIO_BACKEND region because it needs no backend:
  // on Android the base updateDeviceList() enumerates nothing, so the contract
  // holds there by construction.
  TEST_CASE("devicelayer: the suite has a process with no reachable audio device (issue #717)") {
    if (!ensureOffline()) return;
    INFO("devicelayer drives the engine's no-device branches, so it needs a process in which "
         "nothing has initialised PortAudio. Nothing can re-establish that from inside the "
         "process, so it comes from the yse_tests_devicelayer ctest entry running the suite "
         "alone. This failing means some other suite shares the process; the cases that "
         "depend on it are skipped with a message.");
    CHECK(noDeviceReachable());
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
    if (!ownProcess()) return;

    YSE::DEVICE::Manager().updateDeviceList();

    CHECK(YSE::System().getNumDevices() == 0);
    CHECK(YSE::DEVICE::Manager().getDeviceList().empty());
    // The error path must not have invented a default either.
    CHECK(YSE::System().getDefaultDevice().empty());
  }

  // getDevice() indexed the device vector with operator[], which never throws:
  // an out-of-range index read past the end of the list, and the try/catch in
  // yse_system_get_device() that looks like it handles the case was dead code.
  // The empty list above is the common shape of the bug — every index, index 0
  // included, is out of range — so a binding that asks for the first device
  // before checking the count got undefined behaviour rather than an error.
  TEST_CASE("device manager: getDevice() bound-checks its index (issue #581)") {
    if (!ensureOffline()) return;

    YSE::DEVICE::Manager().updateDeviceList();

    const unsigned int n = YSE::System().getNumDevices();
    CHECK_THROWS_AS((void)YSE::System().getDevice(n), std::out_of_range);
    CHECK_THROWS_AS((void)YSE::System().getDevice(n + 9999), std::out_of_range);

    // With no device enumerated at all, index 0 is itself out of range. On a
    // desktop with hardware the offline engine still enumerates nothing (it
    // skips Pa_Initialize), so this holds on CI and on a developer machine.
    if (n == 0) CHECK_THROWS_AS((void)YSE::System().getDevice(0), std::out_of_range);

    // Anything inside the range keeps resolving to a real descriptor.
    for (unsigned int i = 0; i < n; ++i)
      CHECK_NOTHROW((void)YSE::System().getDevice(i));
  }

  // openDevice() is gated on initDone: with PortAudio uninitialised it must
  // refuse before touching Pa_GetDeviceInfo, which would otherwise be called
  // with a device index no host API can resolve.
  TEST_CASE("device manager: opening a device before PortAudio init is refused") {
    if (!ensureOffline()) return;

    YSE::device out = makeDevice();
    YSE::deviceSetup setup;
    setup.setOutput(out).setSampleRate(44100.0).setBufferSize(256);

    // And it says so: the backend reports whether a stream is running
    // afterwards, which is what system::openDevice() gates the mixer layout on
    // (issue #665).
    CHECK(YSE::DEVICE::Manager().openDevice(setup) == false);

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
    // Without the guard this case does not merely fail: with PortAudio up,
    // resume() opens a real stream on an offline engine, and the live callback
    // thread then races the cases below that drive paCallback by hand. That the
    // engine lets it is its own defect, filed as #719.
    if (!ownProcess()) return;

    YSE::System().pause();
    YSE::System().resume();

    CHECK(YSE::System().getActiveSampleRate() == 0.0);
    CHECK(YSE::System().getActiveBufferSize() == 0);
    CHECK(YSE::System().getActiveOutputLatency() == 0);
  }

#ifdef PORTAUDIO_BACKEND
  // autoReconnect's `delay` is milliseconds, as its documentation and the C
  // API's `delay_ms` parameter have always said; it used to be compared against
  // a count of update() calls, which made the actual wait a property of the
  // host's polling rate rather than of the value passed (issue #681).
  //
  // Measurable headless, and deterministically so: the offline engine never
  // opens a stream, so every tick is a zero-callback tick and every watchdog
  // fire is a resume() → addCallback() → Pa_GetDefaultOutputDevice() ==
  // paNoDevice → one warning (the case above owns that path). Counting those
  // lines counts reconnection attempts.
  //
  // A 300 ms interval over a ~750 ms window is 2 attempts. The old tick
  // comparison gives a number that has nothing to do with the interval: too few
  // when the loop turns fewer than 300 times, then one attempt per tick — some
  // 75 of them — as soon as it turns more.
  TEST_CASE("system: autoReconnect retries on a millisecond interval [issue #681]") {
    if (!ensureOffline()) return;
    // The attempt counter here is the count of "no default output device"
    // refusals, so a process where a default output device exists measures
    // nothing at all.
    if (!ownProcess()) return;

    CapturingLog captured;
    ScopedSink sink(&captured);

    YSE::System().autoReconnect(true, 300);
    const auto start = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() - start < std::chrono::milliseconds(750)) {
      YSE::System().update();
      std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    YSE::System().autoReconnect(false, 0);

    int attempts = 0;
    for (const std::string& m : captured.messages) {
      if (m.find("No default audio output device") != std::string::npos) attempts++;
    }
    INFO("reconnection attempts in 750 ms at a 300 ms interval: " << attempts);
    CHECK(attempts >= 1);
    CHECK(attempts <= 4);
  }
#endif

  // A setup that never got a device. deviceSetup's constructor leaves `out`
  // null and nothing forces setOutput(), so this is exactly the shape a host
  // builds with yse_device_setup_create() + set_sample_rate() +
  // set_buffer_size(); openDevice() then read `object.out->getID()` — one line
  // before the getOutputChannels() call that *does* guard the same pointer
  // (issue #661).
  //
  // Reachable headless, unlike the second dereference the same issue covers:
  // the guard sits ahead of the initDone gate, because a setup with no device
  // in it is a malformed request whatever state the backend is in. The
  // real-device half lives in the integration suite (Tests/integration/
  // test_device.cpp), which is where PortAudio is actually initialised.
  TEST_CASE("device manager: a setup with no output device is refused (issue #661)") {
    if (!ensureOffline()) return;
    // The refusal itself holds anywhere; the "no stream opened" half of the
    // contract can only be read on a manager that has no stream to begin with.
    if (!ownProcess()) return;

    CapturingLog captured;
    ScopedSink sink(&captured);

    YSE::deviceSetup setup;
    setup.setSampleRate(44100.0).setBufferSize(256);
    REQUIRE(setup.getOutputChannels() == 0);

    // Driven through the public entry point a host calls, not the backend
    // method: the mixer half of the contract below lives in system::openDevice.
    YSE::System().openDevice(setup, YSE::CT_STEREO);

    // Refused with a diagnostic rather than in silence.
    CHECK(captured.contains("no output device"));

    // No stream opened: the live getters keep reporting the documented zeros.
    CHECK(YSE::System().getActiveSampleRate() == 0.0);
    CHECK(YSE::System().getActiveBufferSize() == 0);
    CHECK(YSE::System().getActiveOutputLatency() == 0);

    // And the mixer layout is untouched. getOutputChannels() is 0 for this
    // setup, so applying it would have configured the engine for zero output
    // channels — deviceManager::doOnCallback() resizes the master to
    // getNumberOfOutputs() on the next callback, and everything rendered after
    // that goes nowhere.
    CHECK(YSE::CHANNEL::Manager().getNumberOfOutputs() == 2u);
  }

  // The general case of the same contract (issue #665): the mixer layout must
  // follow the device that is *actually* open, not the one that was asked for.
  // #661 only covered the setup with nothing in it, because getOutputChannels()
  // is 0 there and a zero-output layout was the visible symptom; a setup with a
  // perfectly well-formed device in it that simply does not open — every
  // PortAudio error path, and the offline engine driven here — still applied
  // its channel count to CHANNEL::Manager().
  //
  // Reachable headless through the initDone gate: with PortAudio never
  // initialised there is no stream to switch to, so this is a failed open by
  // any definition. The real-device half (a device ID no host API resolves,
  // with a stream running) lives in the integration suite.
  TEST_CASE("device manager: a failed open leaves the mixer layout alone (issue #665)") {
    if (!ensureOffline()) return;

    const UInt outputsBefore = YSE::CHANNEL::Manager().getNumberOfOutputs();
    REQUIRE(outputsBefore == 2u);

    // Three output channel names, so the requested layout differs from the
    // running one and applying it is unmistakable.
    YSE::device out = makeDevice();
    REQUIRE(out.getNumOutputChannelNames() == 3u);
    YSE::deviceSetup setup;
    setup.setOutput(out).setSampleRate(44100.0).setBufferSize(256);
    REQUIRE(setup.getOutputChannels() == 3);

    // Through the public entry point: the backend refusing is only half of it,
    // system::openDevice() has to act on the refusal.
    YSE::System().openDevice(setup, YSE::CT_AUTO);

    // Nothing opened, so nothing about the audio path changed — including the
    // channel count deviceManager::doOnCallback() resizes the master to.
    CHECK(YSE::System().getActiveSampleRate() == 0.0);
    CHECK(YSE::CHANNEL::Manager().getNumberOfOutputs() == outputsBefore);
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

  // The other half of that stitching contract: closing the device has to drop
  // the partial block rather than carry it into the next stream (issue #717).
  //
  // paCallback consumes each rendered STANDARD_BUFFERSIZE block in slices and
  // remembers where it stopped in managerObject::bufferPos. A stream is stopped
  // between callbacks, not on a block boundary, so at close() that position is
  // almost always part-way through a block — and it is plain manager state that
  // outlives the stream. close() resets the live-state atomics right next to it
  // (active buffer size, output latency, CPU load) but left this one alone, so
  // the first callback of the *next* stream resumed from it and handed the
  // device the tail of a block rendered before the close: up to
  // STANDARD_BUFFERSIZE-1 samples of pre-close audio after every
  // pause()/resume() and every device switch. The Oboe backend already resets
  // it on each open (oboeImplementation.cpp), so this was the desktop path
  // diverging from the Android one rather than a deliberate design.
  //
  // Found through the unfiltered run: it is what made the stitching case above
  // read a freshly rendered block where it had written its own pattern, because
  // real callbacks from an earlier suite had left bufferPos mid-block.
  //
  // Needs no device, like the case above: paCallback is a static function
  // taking the output buffer as an argument, so the position can be left
  // mid-block by hand and the close driven underneath it.
  TEST_CASE("device manager: closing the device drops the partial rendered block (issue #717)") {
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
    void* const output = static_cast<void*>(planes.data());

    YSE::sound s;
    s.create(g_steady);
    s.relative(true);
    s.play();
    pump();

    // One partial callback: renders a block and consumes the first kFirst
    // samples of it, leaving kRest of that block unconsumed.
    REQUIRE(YSE::DEVICE::managerObject::paCallback(nullptr, output, kFirst, nullptr, 0, &mgr) == 0);

    // What a freshly rendered block of this source looks like, read from the
    // block that was just rendered — the value the callback after the close
    // must produce.
    const float rendered = master.GetBuffers()[0].getPtr()[0];

    // Mark the unconsumed tail with a value the source never produces, so the
    // next callback's output says unambiguously which block it came from.
    const float staleMarker = 0.75f;
    REQUIRE(rendered != doctest::Approx(staleMarker));
    for (size_t c = 0; c < channels; ++c) {
      float* block = master.GetBuffers()[c].getPtr();
      for (unsigned long i = 0; i < kRest; ++i)
        block[kFirst + i] = staleMarker;
    }

    // The device goes away here, mid-block.
    YSE::System().closeCurrentDevice();

    // The next stream's first callback has to start a new block. Before the fix
    // every sample below is staleMarker — the tail of the pre-close block.
    for (auto& plane : storage)
      std::fill(plane.begin(), plane.end(), 0.f);
    REQUIRE(YSE::DEVICE::managerObject::paCallback(nullptr, output, kRest, nullptr, 0, &mgr) == 0);
    for (size_t c = 0; c < channels; ++c) {
      for (unsigned long i = 0; i < kRest; ++i) {
        INFO("channel " << c << " sample " << i);
        CHECK(storage[c][i] == doctest::Approx(rendered));
      }
    }

    s.stop();
    pump();

    // Leave the singleton as the other cases expect to find it.
    YSE::System().closeCurrentDevice();
  }

#endif // PORTAUDIO_BACKEND

  // ─── speaker layout without a device (issue #668) ───────────────────────────
  //
  // The offline engine used to pick its layout up as a side effect of
  // openDevice(): the backend opened nothing, but the setChannelConf() on the
  // next line ran regardless. #665 made the layout follow the device that
  // actually opened — right for a device session, and it left an offline one
  // (this suite, renderOffline() benchmarks, headless CI) pinned to the
  // CT_STEREO that initShared() installs, with no public way out.
  // system::setChannelConfiguration() is that way out, and this is the
  // user-visible claim it has to make good on: an engine brought up with
  // initOffline(), no device and no deviceSetup anywhere in sight, renders in
  // 5.1.
  //
  // Asserted on what the engine actually mixes into, not just on what the
  // manager was told: getNumberOfOutputs() alone would pass on a manager that
  // stored the request and never applied it. The master resize happens in
  // deviceManager::doOnCallback(), which returns early while no sound is
  // alive — hence the live source below.
  //
  // Placed after the PORTAUDIO_BACKEND region because it needs no backend at
  // all, and before the AudioTest case, which has to stay last (see its note).
  TEST_CASE("system: an offline engine renders in a non-stereo layout (issue #668)") {
    if (!ensureOffline()) return;

    auto& master = YSE::DEVICE::Manager().getMaster();
    REQUIRE(YSE::CHANNEL::Manager().getNumberOfOutputs() == 2u);

    YSE::sound s;
    s.create(g_steady);
    s.relative(true);
    s.play();
    pump();
    REQUIRE(master.GetBuffers().size() == 2u);

    YSE::System().setChannelConfiguration(YSE::CT_51, 6);
    pump();

    CHECK(YSE::CHANNEL::Manager().getNumberOfOutputs() == 6u);
    CHECK(master.GetBuffers().size() == 6u);
    // The layout, not merely the channel count: 5.1 puts the LFE at index 3,
    // and it is the one output excluded from azimuth panning (issue #203).
    CHECK(YSE::CHANNEL::Manager().getOutputIsLFE(3));
    CHECK_FALSE(YSE::CHANNEL::Manager().getOutputIsLFE(0));

    // A zero-output layout is refused with a diagnostic rather than accepted:
    // doOnCallback() would resize the master to nothing on the next block and
    // everything rendered after that would go nowhere — the same failure mode
    // the openDevice() guards in #661 / #665 exist to prevent.
    {
      CapturingLog captured;
      ScopedSink sink(&captured);
      YSE::System().setChannelConfiguration(YSE::CT_STEREO, 0);
      CHECK(captured.contains("no output channels"));
    }
    CHECK(YSE::CHANNEL::Manager().getNumberOfOutputs() == 6u);

    // Chaining, like the other system setters.
    CHECK(&YSE::System().setChannelConfiguration(YSE::CT_STEREO, 2) == &YSE::System());
    pump();
    CHECK(YSE::CHANNEL::Manager().getNumberOfOutputs() == 2u);
    CHECK(master.GetBuffers().size() == 2u);

    s.stop();
    pump();
  }

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
  // Driven through the *public* entry point, YSE::System().AudioTest(bool),
  // not INTERNAL::Test().On() (issue #570). That distinction is the whole test:
  // AudioTest() used to wrap its one call in `#ifdef __WINDOWS__`, so the tone
  // rendered fine when poked internally while the documented API — and the C
  // API forwarding to it — did nothing at all off Windows. Asserting at the
  // internal level would keep passing through exactly that bug, so this case
  // must stay on the public call for every platform the suite builds on
  // (desktop and Android alike; nothing below needs a backend).
  //
  // Kept last in the file on purpose. YSE::INTERNAL::Test() constructs a
  // function-local static whose destructor deletes the DSP source that its own
  // sound member still refers to, so the tone is stopped and drained here
  // rather than left running into static teardown.
  TEST_CASE("audio test: the built-in diagnostic tone renders through the offline engine") {
    if (!ensureOffline()) return;

    auto& master = YSE::DEVICE::Manager().getMaster();
    REQUIRE(master.GetBuffers().size() > 0);

    // Zero the master mix before every probed block. Two facts make this
    // mandatory rather than tidy: the paCallback case above writes a hand-made
    // non-zero pattern straight into these buffers, and with no sound alive
    // doOnCallback() gates the render off entirely — so nothing ever overwrites
    // that pattern. Probing the buffer as-found therefore reports "signal"
    // whatever AudioTest() did, which is precisely how the #570 no-op stayed
    // invisible: the case passed in a full-suite run while failing when run
    // alone. Clearing first makes every non-zero sample below attributable to
    // the block that was just rendered.
    auto silenceMaster = [&master]() {
      for (size_t c = 0; c < master.GetBuffers().size(); ++c) {
        float* p = master.GetBuffers()[c].getPtr();
        const UInt len = master.GetBuffers()[c].getLength();
        for (UInt i = 0; i < len; ++i)
          p[i] = 0.f;
      }
    };
    auto renderedSignal = [&master]() {
      bool sawSignal = false;
      const float* p = master.GetBuffers()[0].getPtr();
      const UInt len = master.GetBuffers()[0].getLength();
      for (UInt i = 0; i < len; ++i) {
        CHECK(std::isfinite(p[i]));
        if (std::fabs(p[i]) > 1e-6f) sawSignal = true;
      }
      return sawSignal;
    };

    // Baseline: with the tone off, a cleared master stays silent across a
    // render. This is what gives the positive assertion below its meaning — if
    // some earlier case left a source running, this fails and says so instead
    // of quietly standing in for the tone.
    silenceMaster();
    YSE::System().renderOffline(1);
    REQUIRE_FALSE(renderedSignal());

    YSE::System().AudioTest(true);
    pump();

    // The shepard tone is 11 parallel sine octaves through a low-pass; a few
    // rendered blocks in, the master mix must carry signal. Anything finite and
    // non-silent proves the source ran — asserting a level would be asserting
    // the mixer's gain staging, which belongs to the channel suite.
    bool sawSignal = false;
    for (int block = 0; block < 32 && !sawSignal; ++block) {
      silenceMaster();
      YSE::System().renderOffline(1);
      sawSignal = renderedSignal();
    }
    CHECK(sawSignal);

    YSE::System().AudioTest(false);
    pump();
  }

} // TEST_SUITE("devicelayer")
