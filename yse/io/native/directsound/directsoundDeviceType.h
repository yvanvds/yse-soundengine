/*
  ==============================================================================

    directsoundDeviceType.h
    Created: 16 Jun 2014 7:46:39pm
    Author:  yvan

  ==============================================================================
*/

#ifndef DIRECTSOUNDDEVICETYPE_H_INCLUDED
#define DIRECTSOUNDDEVICETYPE_H_INCLUDED

#include "../../ioDeviceType.h"
#include "directsoundDevice.h"
#include "../../../utils/misc.hpp"
#include <vector>

namespace YSE {
  namespace IO {

    class directSoundDeviceType : public ioDeviceType {
    public:
      directSoundDeviceType();
      virtual void scanDevices();
      virtual int getNumDevices();
      virtual ioDevice * getDevice(int num);

    private:
      std::vector< std::shared_ptr<directSoundDevice> > devices;
    };

  }
}



#endif  // DIRECTSOUNDDEVICETYPE_H_INCLUDED
