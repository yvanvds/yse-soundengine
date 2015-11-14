/*
  ==============================================================================

    deviceManager.h
    Created: 27 Jan 2014 8:04:27pm
    Author:  yvan

  ==============================================================================
*/

#ifndef DEVICEMANAGER_H_INCLUDED
#define DEVICEMANAGER_H_INCLUDED

#include "JuceHeader.h"
#include "../classes.hpp"
#include "../headers/types.hpp"


namespace YSE {
  namespace CHANNEL {
      class implementationObject;
  }  
    
  namespace DEVICE {


    class managerObject : AudioIODeviceCallback {
    public:
      managerObject();

      Bool init();
      void close();

      Flt cpuLoad();

      // callbacks
      virtual void audioDeviceIOCallback(const float ** inputChannelData, int numInputChannels, float ** outputChannelData, int numOutputChannels, int numSamples);
      virtual void audioDeviceAboutToStart(AudioIODevice * device);
      virtual void audioDeviceStopped();
      virtual void audioDeviceError(const juce::String & errorMessage);

      void setMaster(CHANNEL::implementationObject * ptr); // pointer to main channel
      CHANNEL::implementationObject & getMaster();

      void updateDeviceList();
      const std::vector<interfaceObject> & getDeviceList();
      void openDevice(const YSE::DEVICE::setupObject & object);
      
      const std::string & getDefaultTypeName();
      const std::string & getDefaultDeviceName();

    private:
      void openDevice();
      juce::String _lastError;
      Bool initialized;
      Bool open;
      Bool started;
      UInt bufferPos;

      CHANNEL::implementationObject * master;
      AudioDeviceManager audioDeviceManager;
      AudioDeviceManager::AudioDeviceSetup deviceSetup;
      int currentInputChannels, currentOutputChannels;
      
      std::vector<interfaceObject> devices;

      //defaults
      std::string defaultTypeName;
      std::string defaultDeviceName;
    };

    managerObject & Manager();
  }
}



#endif  // DEVICEMANAGER_H_INCLUDED
