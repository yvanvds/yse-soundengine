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

      virtual void openDevice(const YSE::deviceSetup&) {};
      virtual void addCallback() {};

      /* Service a device rebuild requested from a backend error thread (e.g.
         Oboe's onErrorAfterClose flags a disconnect). Called once per
         control-thread tick from system::update() so the actual reopen runs on
         the main thread rather than the backend's error thread. No-op for
         backends without deferred reconnect. (issue #200) */
      virtual void serviceReconnect() {};

      bool doOnCallback(int numSamples);

      /* Render one STANDARD_BUFFERSIZE-sample block through the channel
         tree (master->dsp() + master->buffersToParent()).  Extracted from
         the audio backends' callbacks so the same path can be driven from
         a benchmark via renderOffline().  Caller must have run
         doOnCallback() first.
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
      std::vector<device> devices;
      std::string defaultTypeName;
      std::string defaultDeviceName;

      CHANNEL::implementationObject* master;
      int currentInputChannels, currentOutputChannels;
    };

  } // namespace DEVICE

} // namespace YSE

#endif // ABSTRACTDEVICEMANAGER_H_INCLUDED
