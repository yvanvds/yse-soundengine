#pragma once

#if YSE_ANDROID

#include <atomic>

#include "../classes.hpp"
#include "../headers/types.hpp"
#include "deviceManager.h"

#include "oboeImplementation.h"

namespace YSE {

  namespace DEVICE {

    class managerObject : public deviceManager {
    public:
      managerObject();
      ~managerObject();

      virtual bool init(bool openDevice = true);
      virtual void close();
      virtual float cpuLoad() {
        return implementation.cpuLoad();
      }

      virtual void pause();
      virtual void resume();
      virtual unsigned int GetCallbacksSinceLastUpdate();

      virtual void updateDeviceList();
      virtual void openDevice(const YSE::deviceSetup& object);
      virtual void addCallback();
      virtual void serviceReconnect();

      // Live device-state getters (see deviceManager.h for contract).
      virtual double getActiveSampleRate() const;
      virtual int getActiveBufferSize() const;
      virtual int getActiveOutputLatency() const;

    private:
      OboeImplementation implementation;
      // Written on the main thread (init/pause/resume/close), read cross-thread
      // by the getActive* getters. Atomic to make those reads race-free (#200).
      std::atomic<bool> initDone{false};
      std::atomic<bool> open{false};
    };

    managerObject& Manager();

  } // namespace DEVICE

} // namespace YSE

#endif
