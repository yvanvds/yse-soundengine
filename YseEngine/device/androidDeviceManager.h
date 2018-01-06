#pragma once

#if YSE_ANDROID

#include "../classes.hpp"
#include "../headers/types.hpp"
#include "deviceManager.h"

#include <SLES/OpenSLES.h>
#include "OpenSLImplementation.h"

namespace YSE {

  namespace DEVICE {

    class managerObject : public deviceManager {
    public:
      managerObject();
      ~managerObject();

      virtual bool init();
      virtual void close();
      virtual float cpuLoad() { return 0.f; } // not implemented for android

      virtual void updateDeviceList();
      virtual void openDevice(const YSE::deviceSetup & object);
      virtual void addCallback();

    private:

      OpenSLImplementation implementation;
      bool initDone, open;
    };

    managerObject & Manager();

  }

}


#endif
