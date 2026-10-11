/*
  ==============================================================================

    portaudioDeviceManager.h
    Created: 27 Jul 2016 5:20:20pm
    Author:  yvan

  ==============================================================================
*/

#ifdef PORTAUDIO_BACKEND

#ifndef PORTAUDIODEVICEMANAGER_H_INCLUDED
#define PORTAUDIODEVICEMANAGER_H_INCLUDED

// #pragma comment(lib, "portaudio_x86.lib")

#include "classes.hpp"
#include "headers/types.hpp"
#include "deviceManager.h"
#include "portaudio.h"
#include <chrono>

namespace YSE {

  namespace DEVICE {

    class managerObject : public deviceManager {
    public:
      managerObject();
      ~managerObject() override;

      Bool init(bool openDevice = true) override;
      void close() override;
      Flt cpuLoad() override;

      void pause() override;
      void resume() override;
      unsigned int GetCallbacksSinceLastUpdate() override;

      void updateDeviceList() override;
      Bool refreshDevices() override;
      Bool openDevice(const YSE::deviceSetup& object) override;
      Bool addCallback() override;
      reopenOutcome reopenDevice(bool fallbackToDefault) override;
      void forgetOpenedDevice() override;

      // Record the device reopenDevice() goes back to. openDevice() calls this
      // after a stream started; public so the device-layer tests can stand in
      // for a device they cannot open headless.
      void rememberOpenedDevice(const std::string& name, const std::string& host, int id,
                                int channels, int bufferSize);

      // Number of channels in the output array paCallback() is handed (issue
      // #1028). Set from the stream's channel count before every stream
      // start; public so the device-layer tests, which drive paCallback() with
      // plain memory and no stream, can state the width of that memory.
      void setStreamChannels(int channels) {
        streamChannels.store(channels, std::memory_order_release);
      }

      // Live device-state getters (see deviceManager.h for contract).
      double getActiveSampleRate() const override;
      int getActiveBufferSize() const override;
      int getActiveOutputLatency() const override;

      static int paCallback(const void* input, void* output, unsigned long numSamples,
                            const PaStreamCallbackTimeInfo* timeInfo,
                            PaStreamCallbackFlags statusFlags, void* userData);

    private:
      void terminate();

      // Open and start a stream on `deviceID` (already known to resolve) with
      // `channels` outputs. `bufferSize` 0 lets the backend choose;
      // `requestedRate` 0 means no rate request. Closes whatever was open
      // first, and leaves nothing open on failure.
      Bool openStream(int deviceID, int channels, int bufferSize, UInt requestedRate);

      // The remembered device's index in the backend's current enumeration,
      // matched by name and host, or paNoDevice.
      PaDeviceIndex findOpenedDevice() const;

      // Narrow the mixer layout to `outputs` when it is wider (issue #1028).
      // Called before a stream on a device with that many outputs starts, so
      // the mix is not rendered for channels the device does not have.
      // paCallback() clamps its copy to the stream either way.
      static void fitLayoutToOutputs(int outputs);

      // See setStreamChannels(). Written on the control thread before
      // Pa_StartStream(), read by paCallback(). Not reset by close(): no
      // callback runs without a stream, and every start sets it again.
      std::atomic<int> streamChannels{0};

      // Device of the last successful openDevice() (issue #1027). Control
      // thread only.
      bool hasOpenedDevice = false;
      std::string openedName;
      std::string openedHost;
      int openedID = paNoDevice;
      int openedChannels = 0;
      int openedBufferSize = 0;

      // Fold one callback's wall-clock elapsed time into cpuLoadEma.
      // Called by paCallback on every exit path. Single producer (the
      // PortAudio callback thread), so relaxed atomics are sufficient.
      void updateCpuLoadEma(std::chrono::steady_clock::time_point cbStart,
                            unsigned long numSamples);

      void audioDeviceError(PaError err) const;
      PaStream* stream;

      // Initialised in-class because updateDeviceList() reports a
      // Pa_GetDeviceCount() failure through audioDeviceError() before any call
      // has assigned `err` (issue #418) — on a headless host that is the first
      // thing that happens.
      PaError err = paNoError;
      UInt bufferPos;
      bool initDone, open, started;

      std::atomic<unsigned int> callbacksSinceLastUpdate;

      // Cached live device state, populated post-Pa_OpenStream and reset on
      // close(). Buffer size is captured on the first paCallback invocation
      // because we open the stream with paFramesPerBufferUnspecified.
      std::atomic<int> activeBufferSize{0};
      std::atomic<int> activeOutputLatencySamples{0};

      // EMA-smoothed callback wall-clock load (issue #82). Written by
      // paCallback at the end of each render (relaxed, single producer),
      // read by cpuLoad() on the control thread (relaxed — no
      // synchronisation needed, callers just want a recent value).
      // Reset to 0 on close() so a fresh device starts clean.
      std::atomic<float> cpuLoadEma{0.f};
    };

    managerObject& Manager();
  } // namespace DEVICE
} // namespace YSE

#endif // PORTAUDIODEVICEMANAGER_H_INCLUDED

#endif // PORTAUDIO_BACKEND
