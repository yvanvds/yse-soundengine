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
#include "ioDevice.h"

namespace YSE {
  namespace IO {
    class ioDeviceType {
    public:
      virtual ~ioDeviceType();

      const std::wstring & getTypeName() const { return typeName; }
      virtual void scan() = 0;
      virtual const std::vector<std::wstring> & getDeviceNames(bool returnInputNames = false) const = 0;
      virtual int getDefaultDeviceIndex(bool forIntput) const = 0;
      virtual int getIndex(ioDevice * device, bool asInput) const = 0;
      virtual bool hasSeparateInputsAndOutputs() const = 0;
      virtual ioDevice * createDevice(const std::wstring & outputName, const std::wstring & inputName) = 0;

      static ioDeviceType * createCoreAudio();
      static ioDeviceType * createIosAudio();
      static ioDeviceType * createWASAPI();
      static ioDeviceType * createDirectSound();
      static ioDeviceType * createASIO();
      static ioDeviceType * createALSA();
      static ioDeviceType * createJACK();
      static ioDeviceType * createAndroid();
      static ioDeviceType * createOpenSLES();

    protected:
      explicit ioDeviceType(const std::wstring& typeName);

    private:
      std::wstring typeName;
    };
  }
}



#endif  // IODEVICETYPE_H_INCLUDED
