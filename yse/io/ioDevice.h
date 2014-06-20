/*
  ==============================================================================

    ioDevice.h
    Created: 12 Jun 2014 6:43:05pm
    Author:  yvan

  ==============================================================================
*/

#ifndef IODEVICE_H_INCLUDED
#define IODEVICE_H_INCLUDED

#include <string>
#include <vector>
#include <mutex>
#include "ioCallback.h"
#include "../dsp/sample.hpp"

namespace YSE {
  namespace IO {

    class ioDevice {
    public:
      ioDevice(const std::wstring & name);
      void setDescription(const std::wstring & description);

      std::wstring getName();
      std::wstring getDescription();
      std::wstring getLastError();
      bool isDefault();
      bool isRunning() { return isStarted; }

      virtual bool open () = 0;
      virtual void close() = 0;
      virtual void start(ioCallback * callback) = 0;
      virtual void stop () = 0;

    protected:
      void setDefault(bool value);      
      void setLastError(const std::wstring & message);

      ioCallback * callback;

      bool isOpen;
      bool isStarted;
      

      // Todo: adjust this to the amount of channels
      std::vector<AUDIOBUFFER> inBuffers;
      std::vector<AUDIOBUFFER> outBuffers;

    private:
      std::wstring name;
      std::wstring description;
      std::wstring lastError;

      bool defaultDevice;
    };

  }
}


#endif  // IODEVICE_H_INCLUDED
