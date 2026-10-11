/*
  ==============================================================================

    abstractDeviceManager.h
    Created: 27 Jul 2016 1:02:03pm
    Author:  yvan

  ==============================================================================
*/

#ifndef ABSTRACTDEVICEMANAGER_H_INCLUDED
#define ABSTRACTDEVICEMANAGER_H_INCLUDED

#include "../classes.hpp"
#include "../headers/types.hpp"
#include <vector>

namespace YSE {

  namespace CHANNEL {
    class implementationObject;
  }

  namespace DEVICE {

    class deviceManager {
    public:
      /* you should call the updateDeviceList in the constructor */
      deviceManager();
      virtual ~deviceManager();

      /* `openDevice = false` is the offline path: backends that probe
         hardware (PortAudio's Pa_Initialize, ALSA/JACK/HDA enumeration)
         must skip that work entirely.  Verified bench scenario: bare
         GHA Ubuntu runners take down the VM if Pa_Initialize runs even
         when no stream is later opened (see Bench/README.md). */
      virtual Bool init(bool openDevice = true);
      virtual void close() {};

      virtual void pause() = 0;
      virtual void resume() = 0;
      virtual unsigned int GetCallbacksSinceLastUpdate() = 0;

      /* Monotonic count of audio streams this manager has successfully started
         (issue #681). Bumped by the backends right after the stream is running
         — Pa_StartStream / Oboe requestStart returned OK — on every path that
         can start one: addCallback(), openDevice(), resume(), and the
         backend-side rebuild in serviceReconnect().

         Starting a stream is not the same as delivering audio: the start call
         returns before the device produces its first callback (measured 15-70 ms
         on Windows, load-dependent). system::update() watches this counter to
         tell "started, coming up" from "stalled", so the missed-callback
         watchdog does not tear down a device that is still starting.

         Written and read on the control thread only — never from an audio
         callback. Wraps harmlessly: callers compare for inequality, not order. */
      unsigned int getStreamStartCount() const {
        return streamStartCount;
      }

      /* If the audio backend provides a method to retrieve
         the cpu load, use it. Otherwise just return a number.
         YSE does not depend on this method, but it can be useful
         for testing.
      */
      virtual Flt cpuLoad() {
        return 0.f;
      };

      /* Live values for the currently open audio device. All three return
         0 when no device is open — host applications can use that to drive
         "device disconnected" UI states. Sample rate is the engine's
         negotiated rate (typically YSE::SAMPLERATE) cast to double for ABI
         stability across the C interface. Buffer size is the device's
         frames-per-callback (PortAudio's framesPerBuffer / Oboe's
         framesPerBurst), NOT YSE::STANDARD_BUFFERSIZE — they may differ.
         Output latency is reported in samples (frames) to match the existing
         YSE::device descriptor unit; convert to ms with
         (latency / sampleRate * 1000) on the consumer side. */
      virtual double getActiveSampleRate() const {
        return 0.0;
      }
      virtual int getActiveBufferSize() const {
        return 0;
      }
      virtual int getActiveOutputLatency() const {
        return 0;
      }

      /* this method should populate the devices vector.
       */
      virtual void updateDeviceList() {};

      /* Enumerate the devices without opening a stream (issue #1026), and
         report whether the backend could be reached to do it. Valid with no
         session, on an offline session and on a running device session. A
         backend that probes hardware starts itself here if it is not running
         yet (PortAudio's Pa_Initialize), which is also what lets openDevice()
         work on an offline session afterwards; it must leave any open stream
         alone. On false the device list is empty. Control thread only. The
         base implementation re-reads the list, which for a backend with a
         fixed device needs no start. */
      virtual Bool refreshDevices() {
        updateDeviceList();
        return true;
      }

      /* Open the stream described by the setup and report whether a stream is
         actually running afterwards. False means the request was refused or
         failed and the audio path is unchanged: a malformed setup, a device ID
         no host API resolves, a backend that was never initialised (the
         offline engine), or a backend error. system::openDevice() applies the
         requested speaker layout only on true — a mixer configured for a
         device that is not playing is silently wrong, because
         doOnCallback() resizes the master to getNumberOfOutputs() and the
         still-running stream then gets the wrong channel count (issue #665).
         The base implementation opens nothing, so it reports false. */
      virtual Bool openDevice(const YSE::deviceSetup&) {
        return false;
      };
      /* Open and start a stream on the platform default output device, and
         report whether one is running afterwards (issue #973). False means no
         default device, or the backend refused to open or start the stream;
         nothing is left open in that case. system::init() fails on false, the
         way openDevice() reports its own refusals. The base implementation
         opens nothing, so it reports false. */
      virtual Bool addCallback() {
        return false;
      };

      /* Result of reopenDevice(). */
      enum class reopenOutcome {
        opened, // the session's own device is running again
        fellBack, // it could not be, and the default output device was opened
        failed, // nothing is running
      };

      /* Open the stream again on the device this session had (issue #1027):
         the device of the last successful openDevice(), identified by name and
         host because indices shift when the backend re-enumerates, with its
         channel count and buffer size. A session that never called openDevice()
         reopens the default output device, which is the device it had. Only
         when the session's device cannot be reopened and fallbackToDefault is
         set is the default device opened instead. Expects no stream to be open
         (pause() first). Control thread only. */
      virtual reopenOutcome reopenDevice(bool fallbackToDefault) = 0;

      /* Drop the device remembered by openDevice(), so the next session's
         reopen goes back to the default device. Called by system::close(). */
      virtual void forgetOpenedDevice() {}

      /* Name and host API of the device whose stream is currently open; empty
         when none is (issue #1027). Control thread only. */
      const std::string& getActiveDeviceName() const {
        return activeDeviceName;
      }
      const std::string& getActiveDeviceHost() const {
        return activeDeviceHost;
      }

      /* Application-requested sample rate in Hz for the next stream open
         (issue #646). 0 = no request: the backend opens at the device default.
         The request is an application setting consumed on the init path — the
         backend's negotiated rate stays authoritative (it is what SAMPLERATE
         is written with inside the session-lock window) and the request
         deliberately survives close() so a host can set it once for repeated
         init()/close() cycles. */
      void requestSampleRate(UInt rate) {
        requestedSampleRate = rate;
      }
      UInt getRequestedSampleRate() const {
        return requestedSampleRate;
      }

      /* Service a device rebuild requested from a backend error thread (e.g.
         Oboe's onErrorAfterClose flags a disconnect). Called once per
         control-thread tick from system::update() so the actual reopen runs on
         the main thread rather than the backend's error thread. No-op for
         backends without deferred reconnect. (issue #200) */
      virtual void serviceReconnect() {};

      bool doOnCallback(int numSamples);

      /* Render one STANDARD_BUFFERSIZE-sample block through the channel
         tree on the render scheduler (CHANNEL::Manager().render()).  Extracted from
         the audio backends' callbacks so the same path can be driven from
         a benchmark via renderOffline().  Caller must have run
         doOnCallback() first.  Advances the domain clocks and clip
         transports by one block before rendering it (issue #944), so beat
         time moves per block whatever the device buffer size.
      */
      void renderOneBlock();

      /* Drive the audio callback body N blocks synchronously, no real
         audio device required.  For benchmarks and tests that need to
         measure the DSP mix path.  Single-threaded — assumes no PortAudio
         callback thread is running (caller must not have opened a device,
         e.g. by using YSE::system::initOffline()).
      */
      void renderOffline(int blocks);

      void setMaster(CHANNEL::implementationObject* ptr);
      CHANNEL::implementationObject& getMaster();

      const std::vector<device>& getDeviceList();

      const std::string& getDefaultTypeName();
      const std::string& getDefaultDeviceName();

    protected:
      /* Audio thread. Advance every domain clock by numSamples, then the clip
         transports that read them. Called per rendered block from
         renderOneBlock(), or per callback from doOnCallback() when nothing
         renders (issue #944). */
      void advanceDomainClocks(int numSamples);

      /* Backends call this once per successfully started stream. See
         getStreamStartCount(). */
      void notifyStreamStarted() {
        ++streamStartCount;
      }

      std::vector<device> devices;
      std::string defaultTypeName;
      std::string defaultDeviceName;

      // See getActiveDeviceName(). Set by the backends when a stream starts,
      // cleared when it closes. Control thread only.
      std::string activeDeviceName;
      std::string activeDeviceHost;

      CHANNEL::implementationObject* master;
      int currentInputChannels, currentOutputChannels;

      // See requestSampleRate() above. Written on the control thread before
      // init(); read by the backends on their stream-open (init/negotiation)
      // path — never on the audio callback.
      UInt requestedSampleRate = 0;

      // See getStreamStartCount(). Control thread only, so a plain int is
      // enough — no audio callback ever touches it.
      unsigned int streamStartCount = 0;
    };

  } // namespace DEVICE

} // namespace YSE

#endif // ABSTRACTDEVICEMANAGER_H_INCLUDED
