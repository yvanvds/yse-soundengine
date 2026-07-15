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
      ~managerObject();

      virtual Bool init(bool openDevice = true);
      virtual void close();
      virtual Flt cpuLoad();

      virtual void pause();
      virtual void resume();
      virtual unsigned int GetCallbacksSinceLastUpdate();

      virtual void updateDeviceList();
      virtual void openDevice(const YSE::deviceSetup& object);
      virtual void addCallback();

      // Live device-state getters (see deviceManager.h for contract).
      virtual double getActiveSampleRate() const;
      virtual int getActiveBufferSize() const;
      virtual int getActiveOutputLatency() const;

      static int paCallback(const void* input, void* output, unsigned long numSamples,
                            const PaStreamCallbackTimeInfo* timeInfo,
                            PaStreamCallbackFlags statusFlags, void* userData);

    private:
      void terminate();

      // Fold one callback's wall-clock elapsed time into cpuLoadEma.
      // Called by paCallback on every exit path. Single producer (the
      // PortAudio callback thread), so relaxed atomics are sufficient.
      void updateCpuLoadEma(std::chrono::steady_clock::time_point cbStart,
                            unsigned long numSamples);

      void audioDeviceError(PaError err);
      PaStream* stream;
      PaError err;
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
