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
#include "../headers/types.hpp"
#include "../classes.hpp"

namespace YSE {
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

      void setChannel(channelImplementation * ptr); // pointer to main channel

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

      channelImplementation * mainChannel;
      AudioDeviceManager audioDeviceManager;

    };
  }
}



#endif  // DEVICEMANAGER_H_INCLUDED
