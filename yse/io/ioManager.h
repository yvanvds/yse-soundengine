/*
  ==============================================================================

    ioManager.h
    Created: 12 Jun 2014 6:29:57pm
    Author:  yvan

  ==============================================================================
*/

#ifndef IOMANAGER_H_INCLUDED
#define IOMANAGER_H_INCLUDED

#include <string>
#include "ioDevice.h"
#include "ioCallback.h"
#include "ioDeviceType.h"
#include <memory>
#include <mutex>
#include "..\headers\types.hpp"
#include "..\dsp\sample.hpp"

namespace YSE {
  namespace IO {

    struct ioSetup {
      ioSetup();

      bool operator==(const ioSetup& other) const;

      std::string outputName;
      std::string inputName ;

      Dbl  sampleRate      ;
      Int  bufferSize      ;
      U64  inputChannels   ;
      Bool useDefaultInput ;
      U64  outputChannels  ;
      Bool useDefaultOutput;
    };


    class ioManager {
    public:
      ioManager();
      ~ioManager();

      std::string initialise(int inputChannels, int outputChannels, const std::string & devicename = "", const ioSetup * preferredSetup = nullptr);     
      std::string setDeviceSetup(const ioSetup& newSetup);
      void getDeviceSetup(ioSetup & setup);
      
      ioDevice * getCurrentDevice() const { return currentDevice.get(); }
      std::string getCurrentDeviceType() const { return activeDeviceType; }
      ioDeviceType * getCurrentDeviceTypeObject() const;

      void closeDevice();
      void restartDevice();

      //TODO: one one callback is supported, so this could be simplified
      void addCallback(ioCallback * callback);
      void removeCallback(ioCallback * callback);

      double getCpuUsage() const;

      const std::vector<std::unique_ptr<ioDeviceType>> & getDeviceTypes();

    private:
      class callbackHandler;
      friend class callbackHandler;
      std::shared_ptr<callbackHandler> handler;

      void internalCallback(const float** inputChannelData, int numInputChannels, float** outputChannelData, int numOutputChannels, int numSamples);
      void internalStart(ioDevice * device);
      void internalStop();
      void internalError(const std::string & message);

      void createDeviceTypes();
      void scanDevices();
      void stopDevice();
      void deleteCurrentDevice();
      Dbl pickBestSampleRate(Dbl preferred) const;
      Int pickBestBufferSize(Int preferred) const;

      std::vector<std::unique_ptr<ioDeviceType>> deviceTypes;
      std::vector<ioSetup> deviceTypeConfigs;

      std::shared_ptr<ioDevice> currentDevice;
      //TODO: one one callback is supported, so this could be simplified
      std::vector<ioCallback*> callbacks;
      ioSetup currentSetup;

      Int inputChannelsNeeded, outputChannelsNeeded;
      U64 inputChannels, outputChannels;
      std::string activeDeviceType;
      bool deviceScanNeeded;
      std::mutex audioCallbackMutex;

    };
  }
}



#endif  // IOMANAGER_H_INCLUDED
