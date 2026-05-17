#pragma once

#if YSE_ANDROID

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
      virtual float cpuLoad() { return 0.f; } // not implemented for android

			virtual void pause();
			virtual void resume();
			virtual unsigned int GetCallbacksSinceLastUpdate();

      virtual void updateDeviceList();
      virtual void openDevice(const YSE::deviceSetup & object);
      virtual void addCallback();

      // Live device-state getters (see deviceManager.h for contract).
      virtual double getActiveSampleRate()    const;
      virtual int    getActiveBufferSize()    const;
      virtual int    getActiveOutputLatency() const;

    private:

      OboeImplementation implementation;
      bool initDone, open;
    };

    managerObject & Manager();

  }

}


#endif
