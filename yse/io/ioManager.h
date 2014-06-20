/*
  ==============================================================================

    ioManager.h
    Created: 12 Jun 2014 6:29:57pm
    Author:  yvan

  ==============================================================================
*/

#ifndef IOMANAGER_H_INCLUDED
#define IOMANAGER_H_INCLUDED

#include "ioCallback.h"
#include "ioDeviceType.h"
#include "..\headers\types.hpp"
#include "..\headers\enums.hpp"
#include "..\channel\channelImplementation.h"
#include <memory>

namespace YSE {
  namespace IO {

   class ioManager : public ioCallback {
    public:
      ioManager();
     ~ioManager();

      Bool init();
      Bool isRunning ();
      Bool isReadyToStart();

      void startDevice  ();
      void stopDevice   ();
      void restartDevice();

      // handle device types
      Bool hasDeviceType(DEVICETYPE type);
      void setDeviceType(DEVICETYPE type);

      // handle devices
      std::vector<std::wstring> getDeviceList();
      void setActiveDevice(int num = -1); // -1 opens the default device
      void setDefaultDeviceAsActive();

      // master channel
      void setMaster(CHANNEL::implementationObject * ptr); // pointer to main channel
      CHANNEL::implementationObject & getMaster();

      // callbacks
      virtual void onCallback(const std::vector<AUDIOBUFFER> & inputChannels, std::vector<AUDIOBUFFER> & outputChannels) override;
      virtual void onStart() override;
      virtual void onStop() override;
      virtual void onError(const std::wstring & errorMessage) override;

   private:
     bool initDone;
     
     UInt bufferPos;
     
     std::shared_ptr<ioDeviceType> currentDeviceType;
     ioDevice * currentDevice;
     CHANNEL::implementationObject * master;
    };

   ioManager & Manager();
  }
}



#endif  // IOMANAGER_H_INCLUDED
