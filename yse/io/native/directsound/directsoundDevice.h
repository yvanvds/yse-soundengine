/*
  ==============================================================================

    directsoundDevice.h
    Created: 16 Jun 2014 8:40:47pm
    Author:  yvan

  ==============================================================================
*/

#ifndef DIRECTSOUNDDEVICE_H_INCLUDED
#define DIRECTSOUNDDEVICE_H_INCLUDED

#include <string>
#include <dsound.h>
#include <cmath>
#include "../../ioDevice.h"
#include "../../../internal/thread.h"
#include "../../../utils/misc.hpp"

namespace YSE {
  namespace IO {

    class directSoundDevice : public ioDevice, public INTERNAL::thread {
    public:
      directSoundDevice(const std::wstring & name, LPGUID id);
     ~directSoundDevice();
     
      virtual bool open ();
      virtual void close();
      virtual void start(ioCallback * callback);
      virtual void stop ();
      virtual void run  ();

    private:
      GUID id;
      LPDIRECTSOUND8 deviceObject;
      LPDIRECTSOUNDBUFFER outputBuffer;
      bool firstPartFilled, secondPartFilled, thirdPartFilled;
      std::mutex audiolock;

      static inline int convertValues(const float l, const float r) {
        return (static_cast<int>(32767 * r) << 16) | (0xffff & static_cast<int>(32767 * l));
      }
    };

  }
}



#endif  // DIRECTSOUNDDEVICE_H_INCLUDED
