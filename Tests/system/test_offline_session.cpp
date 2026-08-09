// An offline session must stay offline (issue #719).
//
// WHAT THIS SUITE NEEDS AND WHY IT HAS ITS OWN PROCESS
//
// `devicelayer` (Tests/system/test_device_layer.cpp) needs a process where
// PortAudio has never been initialised. This suite needs the exact opposite,
// and for the same underlying reason: Pa_Initialize runs once per process, on
// the first init() that opens a device, and the only call that undoes it —
// managerObject::terminate() — is private and runs from the singleton's
// destructor at process exit. So "PortAudio is up" is a property of the
// process, and the bug under test only exists in a process that has it.
//
// That is not an exotic configuration. It is *every* host application that
// calls System().init() and later close()s, and every test binary that does the
// same. In such a process an initOffline() session used to find a real default
// output device on the other side of resume():
//
//   System().resume()  ->  DEVICE::Manager().resume()  ->  addCallback()
//                      ->  Pa_GetDefaultOutputDevice()  ->  Pa_OpenStream
//                      ->  Pa_StartStream
//
// and the autoReconnect watchdog in System().update() got there on its own,
// since an offline session never delivers a callback and therefore reads as
// permanently stalled. The harm is not the surprise device but the threading:
// renderOffline() is documented single-threaded ("caller must not have opened a
// device, e.g. by using YSE::system::initOffline()", deviceManager.h), and a
// PortAudio callback thread drives the same manager update() functions the
// offline caller drives from its own thread — the race Tests/support/
// null_device.hpp opens by explaining.
//
// HOW THE PRECONDITION IS ESTABLISHED
//
// bringPortAudioUp() below runs one ordinary device session — init(), pause(),
// close() — before anything else in the suite. After it, PortAudio is up, the
// device list is populated (close() does not clear it), and no stream and no
// session are open. That is the state a second, offline session inherits.
//
// It needs real audio hardware, so on headless CI (and any host with no default
// output device) every case skips with a message. That is honest rather than
// silent: the cases below cannot be made to measure anything without a device
// to wrongly open, and there is nothing to substitute for it — the whole defect
// is "a device that is reachable gets opened".
//
// The suite drives System::close() and System::init(), so it is isolated like
// every other lifecycle suite (Tests/CMakeLists.txt). It must in particular not
// share a process with `devicelayer`, whose contract case asserts the opposite
// state and would fail by name.

#include <doctest/doctest.h>

#include <chrono>
#include <thread>

#include "yse.hpp"
#include "device/deviceInterface.hpp"
#include "device/deviceSetup.hpp"

namespace {

  // One device session, brought up and taken down again, purely to leave
  // PortAudio initialised and the device list populated for the offline
  // sessions below. Latched: it must run exactly once, and the answer cannot
  // change afterwards — nothing un-initialises PortAudio.
  //
  // pause() before close() is deliberate: init() opens a stream, and the cases
  // below drive the engine from the test thread. Nothing here should leave a
  // callback thread behind.
  bool bringPortAudioUp() {
    static const bool result = []() {
      YSE::System().close(); // normalize regardless of starting state
      if (!YSE::System().init()) return false;
      YSE::System().pause();
      // A device the engine can actually open is what the offline sessions
      // below must NOT get. Both halves matter: an enumerated list proves
      // Pa_Initialize ran, a non-empty default name proves
      // Pa_GetDefaultOutputDevice() resolves to something.
      const bool reachable =
          YSE::System().getNumDevices() > 0 && !YSE::System().getDefaultDevice().empty();
      YSE::System().close();
      return reachable;
    }();
    return result;
  }

  bool deviceReachable() {
    if (bringPortAudioUp()) return true;
    MESSAGE("skipped: no default audio output device in this process, so there is no device for "
            "an offline session to wrongly open. The defect this suite covers only exists where "
            "one is reachable (issue #719).");
    return false;
  }

  // The first enumerated device that can play, preferring the platform default.
  // Returned by value: the cases hold it across a close()/initOffline() cycle,
  // and deviceSetup stores a pointer to whatever it is handed.
  YSE::device playableDevice() {
    const std::string& preferred = YSE::System().getDefaultDevice();
    YSE::device fallback;
    for (unsigned int i = 0; i < YSE::System().getNumDevices(); ++i) {
      const YSE::device& d = YSE::System().getDevice(i);
      if (d.getNumOutputChannelNames() == 0) continue;
      if (d.getName() == preferred) return d;
      if (fallback.getNumOutputChannelNames() == 0) fallback = d;
    }
    return fallback;
  }

} // namespace

TEST_SUITE("offlinesession") {

  // The direct repro. Pre-fix, resume() opens a real stream here and
  // getActiveSampleRate() reports the rate of a device the host explicitly
  // asked not to have; post-fix it is a no-op with a debug log line.
  TEST_CASE("offlinesession: resume() does not open a device on an offline session (issue #719)") {
    if (!deviceReachable()) return;

    YSE::System().close();
    REQUIRE(YSE::System().initOffline());
    // The starting point: a session with no stream, in a process where one is
    // one Pa_OpenStream away.
    REQUIRE(YSE::System().getActiveSampleRate() == 0.0);

    YSE::System().pause();
    YSE::System().resume();

    const double rate = YSE::System().getActiveSampleRate();
    const int latency = YSE::System().getActiveOutputLatency();
    // Take the stream away before asserting. On the unfixed engine a live
    // PortAudio callback thread is running on an offline session right now, and
    // it must not survive a failing CHECK into the next case — that thread
    // driving the manager update()s is the harm being tested for.
    if (rate != 0.0) YSE::System().closeCurrentDevice();

    CHECK(rate == 0.0);
    CHECK(latency == 0);

    YSE::System().close();
  }

  // The same thing reached without the host asking at all: with autoReconnect
  // on, an offline session's permanent zero-callback state reads as a stalled
  // device, and the watchdog's remedy is pause() + resume(). A delay of 0 is
  // what autoReconnect ships with, so this is the shape a host gets by simply
  // enabling reconnection.
  TEST_CASE("offlinesession: the autoReconnect watchdog does not open a device on an offline "
            "session (issue #719)") {
    if (!deviceReachable()) return;

    YSE::System().close();
    REQUIRE(YSE::System().initOffline());
    REQUIRE(YSE::System().getActiveSampleRate() == 0.0);

    YSE::System().autoReconnect(true, 0);
    double rate = 0.0;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(300);
    while (std::chrono::steady_clock::now() < deadline) {
      YSE::System().update();
      rate = YSE::System().getActiveSampleRate();
      if (rate != 0.0) break;
      std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    // Disarm before asserting, so a failing CHECK cannot leave the watchdog
    // re-opening the device for the rest of the process.
    YSE::System().autoReconnect(false, 0);
    if (rate != 0.0) YSE::System().closeCurrentDevice();

    CHECK(rate == 0.0);
    // And the session still reads as having no device at all.
    CHECK(YSE::System().getActiveBufferSize() == 0);
    CHECK(YSE::System().getActiveOutputLatency() == 0);

    YSE::System().close();
  }

  // The other half of the decision (issue #719, design point 3): openDevice()
  // is *allowed* on an offline session and promotes it, because unlike resume()
  // it names a device and asks for it. The observable of the promotion is that
  // pause()/resume() work afterwards — before this change a promoted session
  // would have been resumable only by accident, and after it, only because the
  // promotion is recorded.
  TEST_CASE("offlinesession: openDevice() promotes an offline session (issue #719)") {
    if (!deviceReachable()) return;

    YSE::System().close();

    const YSE::device out = playableDevice();
    if (out.getNumOutputChannelNames() == 0 || out.getNumAvailableSampleRates() == 0) {
      MESSAGE("skipped: no enumerated device with output channels and an advertised rate.");
      return;
    }

    // Bring the session up at the device's own rate. SAMPLERATE is locked for
    // the session at the end of initShared(), and openDevice() asserts a locked
    // rate matches the device it opens — an offline session that defaulted to a
    // different rate would trip that assert rather than test this.
    const double deviceRate = out.getAvailableSampleRate(0);
    const unsigned int previousRequest = YSE::System().requestSampleRate();
    YSE::System().requestSampleRate(static_cast<unsigned int>(deviceRate));
    REQUIRE(YSE::System().initOffline());
    REQUIRE(YSE::System().getActiveSampleRate() == 0.0);

    YSE::deviceSetup setup;
    setup.setOutput(out).setSampleRate(deviceRate).setBufferSize(0);
    YSE::System().openDevice(setup, YSE::CT_AUTO);

    if (YSE::System().getActiveSampleRate() == 0.0) {
      MESSAGE("skipped: the enumerated device did not open in this process (exclusive-mode host, "
              "device in use), so there is no promotion to observe.");
      YSE::System().close();
      YSE::System().requestSampleRate(previousRequest);
      return;
    }

    // Promoted. pause() closes the stream the session now owns...
    YSE::System().pause();
    CHECK(YSE::System().getActiveSampleRate() == 0.0);
    // ...and resume() is entitled to bring it back, which is precisely what it
    // refuses to do for a session that never had one.
    YSE::System().resume();
    CHECK(YSE::System().getActiveSampleRate() > 0.0);

    YSE::System().closeCurrentDevice();
    YSE::System().close();
    YSE::System().requestSampleRate(previousRequest);

    // close() drops the promotion with the session it belonged to: the next
    // offline session starts offline again.
    REQUIRE(YSE::System().initOffline());
    YSE::System().resume();
    const double rate = YSE::System().getActiveSampleRate();
    if (rate != 0.0) YSE::System().closeCurrentDevice();
    CHECK(rate == 0.0);
    YSE::System().close();
  }

} // TEST_SUITE("offlinesession")
