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
    
  namespace INTERNAL {


    class deviceManager : AudioIODeviceCallback {
    public:
      deviceManager();
      ~deviceManager();

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
      //std::vector<audioDevice> deviceList;
      //Bool updateDevices();
      //Bool openDevice(UInt ID, Int outChannels);
      //Int activeDevice;

      juce_DeclareSingleton(deviceManager, true)
    private:
      juce::String _lastError;
      Bool initialized;
      Bool open;
      Bool started;
      UInt bufferPos;

      CHANNEL::implementationObject * master;
      AudioDeviceManager audioDeviceManager;

    };
  }
}



#endif  // DEVICEMANAGER_H_INCLUDED
