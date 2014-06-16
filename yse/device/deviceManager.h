/*
  ==============================================================================

    deviceManager.h
    Created: 27 Jan 2014 8:04:27pm
    Author:  yvan

  ==============================================================================
*/

#ifndef DEVICEMANAGER_H_INCLUDED
#define DEVICEMANAGER_H_INCLUDED

#include "../io/ioManager.h"
#include "../io/ioCallback.h"
#include "../classes.hpp"
#include "../headers/types.hpp"
#include <string>

namespace YSE {
  namespace CHANNEL {
      class implementationObject;
  }  
    
  namespace DEVICE {


    class managerObject : IO::ioCallback {
    public:
      managerObject();

      Bool init();
      void close();

      Flt cpuLoad();

      // callbacks
      virtual void onCallback(const float ** inputChannelData, int numInputChannels, float ** outputChannelData, int numOutputChannels, int numSamples) override;
      virtual void onStart() override;
      virtual void onStop () override;
      virtual void onError(const std::wstring & errorMessage) override;

      void setMaster(CHANNEL::implementationObject * ptr); // pointer to main channel
      CHANNEL::implementationObject & getMaster();
      //std::vector<audioDevice> deviceList;
      void updateDeviceList();
      const std::vector<interfaceObject> & getDeviceList();
      void openDevice(const YSE::DEVICE::setupObject & object);
      //Int activeDevice;

    private:
      std::wstring _lastError;
      Bool initialized;
      Bool open;
      Bool started;
      UInt bufferPos;

      CHANNEL::implementationObject * master;
      IO::ioManager audioDeviceManager;
      
      std::vector<interfaceObject> devices;
    };

    managerObject & Manager();
  }
}



#endif  // DEVICEMANAGER_H_INCLUDED
