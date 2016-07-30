/*
  ==============================================================================

    deviceManager.h
    Created: 27 Jan 2014 8:04:27pm
    Author:  yvan

  ==============================================================================
*/

#if JUCE_BACKEND

#ifndef DEVICEMANAGER_H_INCLUDED
#define DEVICEMANAGER_H_INCLUDED

#include "JuceHeader.h"
#include "../classes.hpp"
#include "../headers/types.hpp"
#include "abstractDeviceManager.h"


namespace YSE {
   
    
  namespace DEVICE {

    class managerObject : public abstractDeviceManager, AudioIODeviceCallback {
    public:
      managerObject();
      virtual ~managerObject();

      // implementation of abstractDeviceManager
      virtual Bool init ();
      virtual void close();
      virtual Flt  cpuLoad(); 

      // implementation of AudioIODeviceCallback
      virtual void audioDeviceIOCallback(const float ** inputChannelData, int numInputChannels, float ** outputChannelData, int numOutputChannels, int numSamples);
      virtual void audioDeviceAboutToStart(AudioIODevice * device);
      virtual void audioDeviceStopped();
      virtual void audioDeviceError(const juce::String & errorMessage);

      void updateDeviceList();
      virtual void openDevice(const YSE::DEVICE::setupObject & object);
      virtual void addCallback();
      
    private:
      void openDevice();
      juce::String _lastError;
      Bool initialized;
      Bool open;
      Bool started;
      UInt bufferPos;

      AudioDeviceManager audioDeviceManager;
      AudioDeviceManager::AudioDeviceSetup deviceSetup;

      // for windows COM library
      bool coInitialized;
    };

    managerObject & Manager();
  }
}



#endif  // DEVICEMANAGER_H_INCLUDED

#endif // JUCE_BACKEND