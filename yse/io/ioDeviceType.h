/*
  ==============================================================================

    ioDeviceType.h
    Created: 12 Jun 2014 8:09:31pm
    Author:  yvan

  ==============================================================================
*/

#ifndef IODEVICETYPE_H_INCLUDED
#define IODEVICETYPE_H_INCLUDED

#include <string>
#include <vector>
#include <memory>
#include "ioDevice.h"
#include "../headers/defines.hpp"
#include "../headers/enums.hpp"

namespace YSE {
  namespace IO {

    

    class ioDeviceType {
    public:
      ioDeviceType(const std::wstring & name);
      const std::wstring & getName() const { return typeName; }

      virtual void scanDevices() = 0;
      virtual int getNumDevices() = 0;
      virtual ioDevice * getDevice(int num) = 0;

      static std::shared_ptr<ioDeviceType> Create(DEVICETYPE type);

    private:
      std::wstring typeName;
    };

    #if (YSE_WINDOWS && YSE_WASAPI) 
    std::shared_ptr<ioDeviceType> createWASAPI(); 
    #endif

#if (YSE_WINDOWS && YSE_DIRECTSOUND) 
    std::shared_ptr<ioDeviceType> createDirectSound();
#endif
  }
}



#endif  // IODEVICETYPE_H_INCLUDED
