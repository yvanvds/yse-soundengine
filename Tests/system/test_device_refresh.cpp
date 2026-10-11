// Enumerating audio devices without opening a stream (issue #1026).
//
// Before System().refreshDevices() existed, the device list was filled only by
// init(), as a side effect of opening the platform default device, and
// initOffline() filled nothing. A host that stores "output device X @ host Y"
// could not resolve it without first opening the default device, which may be
// the wrong one, at the wrong rate, or held by another application. And an
// initOffline() session could only openDevice() in a process where an earlier
// init() had started PortAudio (issue #972).
//
// ISOLATION
//
// The first case needs a process where PortAudio has never been initialised,
// because "refreshDevices() starts the backend" is only observable from that
// state: it asserts the cold state up front and fails by name if some other
// suite shares the process. Cases are ordered by line within the file, so the
// cold case runs first. The suite also drives init()/close() cycles, so it
// runs in its own process (yse_tests_devicerefresh, Tests/CMakeLists.txt).
//
// The expected list comes from PortAudio itself (Pa_GetDeviceCount /
// Pa_GetDeviceInfo), not from the engine: the engine's own list is what is
// under test.
//
// HARDWARE
//
// The cold enumeration case and the C API case run on any host. On one with no
// audio backend, refreshDevices() reports false and an empty list, which is
// asserted. The open-a-device and running-stream cases need a device that
// opens, and skip with a message where none does (headless CI, a device held
// exclusively).

#include <doctest/doctest.h>

#include <string>

#include "yse.hpp"
#include "device/deviceInterface.hpp"
#include "device/deviceSetup.hpp"
#include "yse_c/yse_device.h"
#include "yse_c/yse_system.h"

#ifdef PORTAUDIO_BACKEND
#include "portaudio.h"
#endif

namespace {

#ifdef PORTAUDIO_BACKEND
  // Poll update() until the device delivers audio. False if it never does
  // within ~2 s: a stream that does not start is not what these cases measure.
  bool waitForAudio() {
    for (int i = 0; i < 200; ++i) {
      YSE::System().update();
      if (YSE::System().missedCallbacks() == 0) return true;
      YSE::System().sleep(10);
    }
    return false;
  }

  // Index of the first output-capable device, preferring the platform default.
  // -1 when the list has none.
  int playableIndex() {
    const std::string& preferred = YSE::System().getDefaultDevice();
    int fallback = -1;
    for (unsigned int i = 0; i < YSE::System().getNumDevices(); ++i) {
      const YSE::device& d = YSE::System().getDevice(i);
      if (d.getNumOutputChannelNames() == 0 || d.getNumAvailableSampleRates() == 0) continue;
      if (d.getName() == preferred) return (int)i;
      if (fallback < 0) fallback = (int)i;
    }
    return fallback;
  }

  // The engine's list must be exactly what PortAudio enumerates now.
  void checkListMatchesBackend() {
    const int count = Pa_GetDeviceCount();
    REQUIRE(count >= 0);
    REQUIRE(YSE::System().getNumDevices() == (unsigned int)count);
    for (int i = 0; i < count; ++i) {
      const PaDeviceInfo* info = Pa_GetDeviceInfo(i);
      REQUIRE(info != nullptr);
      INFO("device " << i);
      CHECK(YSE::System().getDevice((unsigned int)i).getName() == info->name);
      CHECK(YSE::System().getDevice((unsigned int)i).getID() == i);
    }
    const PaDeviceInfo* def = Pa_GetDeviceInfo(Pa_GetDefaultOutputDevice());
    CHECK(YSE::System().getDefaultDevice() == (def != nullptr ? std::string(def->name) : ""));
  }
#endif

} // namespace

TEST_SUITE("devicerefresh") {

#ifdef PORTAUDIO_BACKEND
  // The headline request: list the devices before anything is opened, with no
  // session at all. Must stay the first case in the file (see ISOLATION).
  TEST_CASE("devicerefresh: refreshDevices() enumerates with no session and opens nothing "
            "(issue #1026)") {
    INFO("devicerefresh needs a process in which nothing has initialised PortAudio yet, which "
         "only the yse_tests_devicerefresh ctest entry guarantees.");
    // Cold: PortAudio was never started, so nothing is listed.
    REQUIRE(Pa_GetDeviceCount() < 0);
    REQUIRE(YSE::System().getNumDevices() == 0);
    REQUIRE(YSE::System().getSampleRate() == 0.0);

    const bool ok = YSE::System().refreshDevices();

    // No stream and no session came out of it either way.
    CHECK(YSE::System().getActiveSampleRate() == 0.0);
    CHECK(YSE::System().getSampleRate() == 0.0);

    if (!ok) {
      MESSAGE("no audio backend on this host: refreshDevices() reported false.");
      CHECK(YSE::System().getNumDevices() == 0);
      CHECK(YSE::System().getDefaultDevice().empty());
      return;
    }
    checkListMatchesBackend();
  }

  // The use case from the issue: an initOffline() session resolves a device
  // and opens exactly it, in a process that never called init() (#972's gap).
  TEST_CASE("devicerefresh: an offline session opens a device listed by refreshDevices() "
            "(issues #1026, #972)") {
    YSE::System().close();
    if (!YSE::System().refreshDevices() || playableIndex() < 0) {
      MESSAGE("skipped: no enumerated output device on this host.");
      return;
    }
    // What a host stores: name and host, not the index.
    const YSE::device& stored = YSE::System().getDevice((unsigned int)playableIndex());
    const std::string name = stored.getName();
    const std::string host = stored.getTypeName();
    const unsigned int deviceRate = (unsigned int)stored.getAvailableSampleRate(0);

    // Run the session at the device's rate: a refused session rate is #972's
    // own case, not this one.
    const unsigned int previousRequest = YSE::System().requestSampleRate();
    YSE::System().requestSampleRate(deviceRate);
    REQUIRE(YSE::System().initOffline());

    // Refresh again on the offline session: the backend restarts (no stream is
    // open) and the list is rebuilt, so resolve the stored device by name.
    REQUIRE(YSE::System().refreshDevices());
    REQUIRE(YSE::System().getActiveSampleRate() == 0.0);
    int found = -1;
    for (unsigned int i = 0; i < YSE::System().getNumDevices(); ++i) {
      const YSE::device& d = YSE::System().getDevice(i);
      if (d.getName() == name && d.getTypeName() == host) {
        found = (int)i;
        break;
      }
    }
    REQUIRE(found >= 0);

    YSE::deviceSetup setup;
    setup.setOutput(YSE::System().getDevice((unsigned int)found)).setBufferSize(0);
    const bool opened = YSE::System().openDevice(setup, YSE::CT_AUTO);

    const double active = YSE::System().getActiveSampleRate();
    const bool flowing = opened && waitForAudio();
    if (opened) YSE::System().closeCurrentDevice();
    YSE::System().close();
    YSE::System().requestSampleRate(previousRequest);

    if (!opened) {
      MESSAGE("skipped: the device did not open in this process (exclusive-mode host, device in "
              "use).");
      return;
    }
    CHECK(active == (double)deviceRate);
    CHECK(flowing);
  }

  // On a running device session the stream is left alone; once it is closed,
  // the refresh restarts the backend, and the next init() still works.
  TEST_CASE("devicerefresh: refreshDevices() leaves a running stream alone (issue #1026)") {
    YSE::System().close();
    const unsigned int previousRequest = YSE::System().requestSampleRate();
    YSE::System().requestSampleRate(0);
    if (!YSE::System().init()) {
      MESSAGE("skipped: no default output device opens on this host.");
      YSE::System().requestSampleRate(previousRequest);
      return;
    }
    if (!waitForAudio()) {
      MESSAGE("skipped: the default device opened but delivered no audio.");
      YSE::System().close();
      YSE::System().requestSampleRate(previousRequest);
      return;
    }
    const double rate = YSE::System().getActiveSampleRate();
    const unsigned int listed = YSE::System().getNumDevices();
    REQUIRE(rate > 0.0);

    const bool ok = YSE::System().refreshDevices();

    // Same stream: still open at the same rate and still delivering. A backend
    // restart would have closed it.
    const double after = YSE::System().getActiveSampleRate();
    YSE::System().sleep(50);
    YSE::System().update();
    const int missed = YSE::System().missedCallbacks();
    const unsigned int listedAfter = YSE::System().getNumDevices();
    YSE::System().pause();
    YSE::System().close();

    CHECK(ok);
    CHECK(after == rate);
    CHECK(missed == 0);
    // Re-read from the running backend's enumeration, which has not changed.
    CHECK(listedAfter == listed);

    // No stream now: the refresh restarts the backend, and what it lists is
    // PortAudio's fresh enumeration.
    REQUIRE(YSE::System().refreshDevices());
    checkListMatchesBackend();

    // The restarted backend is usable: a device session comes up on it.
    const bool again = YSE::System().init();
    if (again) YSE::System().pause();
    YSE::System().close();
    YSE::System().requestSampleRate(previousRequest);
    CHECK(again);
  }
#endif

  // The C entry point. Holds on any host: either the list is current, or the
  // call reports YSE_ERR_AUDIO_DEVICE with a reason and an empty list.
  TEST_CASE("devicerefresh: yse_system_refresh_devices() (issue #1026)") {
    CHECK(yse_system_refresh_devices(nullptr) == YSE_ERR_INVALID_HANDLE);

    YseSystem* sys = yse_system_get();
    yse_system_close(sys);
    const YseStatus status = yse_system_refresh_devices(sys);
    CHECK(yse_system_get_active_sample_rate(sys) == 0.0);
    if (status != YSE_OK) {
      CHECK(status == YSE_ERR_AUDIO_DEVICE);
      CHECK_FALSE(std::string(yse_last_error()).empty());
      CHECK(yse_system_num_devices(sys) == 0);
      return;
    }
#ifdef PORTAUDIO_BACKEND
    const int count = Pa_GetDeviceCount();
    REQUIRE(count >= 0);
    CHECK(yse_system_num_devices(sys) == (unsigned int)count);
    if (count > 0) {
      char name[256];
      yse_device_get_name(yse_system_get_device(sys, 0), name, sizeof name);
      CHECK(std::string(name) == std::string(Pa_GetDeviceInfo(0)->name).substr(0, 255));
    }
#endif
  }

} // TEST_SUITE("devicerefresh")
