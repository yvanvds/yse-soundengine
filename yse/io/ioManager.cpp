/*
  ==============================================================================

    ioManager.cpp
    Created: 12 Jun 2014 6:29:57pm
    Author:  yvan

  ==============================================================================
*/

#include "ioManager.h"
#include "ioCallback.h"
#include "../utils/misc.hpp"

/////////////////////////////////////////////////////////////////
// ioCallback
/////////////////////////////////////////////////////////////////

namespace YSE {
  namespace IO {
    class ioManager::callbackHandler : public ioCallback{
    public:
      callbackHandler(ioManager & manager) : owner(manager) {}

    private:
      void onCallback(const float** inputChannelData, int numInputChannels, float** outputChannelData, int numOutputChannels, int numSamples) override {
        owner.internalCallback(inputChannelData, numInputChannels, outputChannelData, numOutputChannels, numSamples);
      }

      void onStart(ioDevice * device) override {
        owner.internalStart(device);
      }

      void onStop() override {
        owner.internalStop();
      }

      void onError(const std::string & message) override {
        owner.internalError(message);
      }

      ioManager & owner;
    };
  }
}

/////////////////////////////////////////////////////////////////
// ioSetup
/////////////////////////////////////////////////////////////////

YSE::IO::ioSetup::ioSetup() : sampleRate(0), bufferSize(0), useDefaultInput(true), useDefaultOutput(true) {
}

bool YSE::IO::ioSetup::operator==(const ioSetup& other) const {
  return outputName == other.outputName
    && inputName == other.inputName
    && sampleRate == other.sampleRate
    && bufferSize == other.bufferSize
    && inputChannels == other.inputChannels
    && useDefaultInput == other.useDefaultInput
    && outputChannels == other.outputChannels
    && useDefaultOutput == other.useDefaultOutput;
}

/////////////////////////////////////////////////////////////////
// ioManager
/////////////////////////////////////////////////////////////////

YSE::IO::ioManager::ioManager()
: inputChannelsNeeded(0),
outputChannelsNeeded(0),
deviceScanNeeded(true) {
  handler.reset(new callbackHandler(*this));
}

YSE::IO::ioManager::~ioManager() {
  currentDevice.reset();
}

void YSE::IO::ioManager::createDeviceTypes() {
  if (deviceTypes.empty()) {
    {
      std::unique_ptr<ioDeviceType> type(ioDeviceType::createWASAPI());
      if (type != nullptr) deviceTypes.push_back(std::move(type));
    }
    {
      std::unique_ptr<ioDeviceType> type(ioDeviceType::createDirectSound());
      if (type != nullptr) deviceTypes.push_back(std::move(type));
    }
    {
      std::unique_ptr<ioDeviceType> type(ioDeviceType::createASIO());
      if (type != nullptr) deviceTypes.push_back(std::move(type));
    }
    {
      std::unique_ptr<ioDeviceType> type(ioDeviceType::createCoreAudio());
      if (type != nullptr) deviceTypes.push_back(std::move(type));
    }
    {
      std::unique_ptr<ioDeviceType> type(ioDeviceType::createIosAudio());
      if (type != nullptr) deviceTypes.push_back(std::move(type));
    }
    {
      std::unique_ptr<ioDeviceType> type(ioDeviceType::createALSA());
      if (type != nullptr) deviceTypes.push_back(std::move(type));
    }
    {
      std::unique_ptr<ioDeviceType> type(ioDeviceType::createJACK());
      if (type != nullptr) deviceTypes.push_back(std::move(type));
    }
    {
      std::unique_ptr<ioDeviceType> type(ioDeviceType::createOpenSLES());
      if (type != nullptr) deviceTypes.push_back(std::move(type));
    }
    {
      std::unique_ptr<ioDeviceType> type(ioDeviceType::createAndroid());
      if (type != nullptr) deviceTypes.push_back(std::move(type));
    }

    if (!deviceTypes.empty()) {
      activeDeviceType = deviceTypes[0]->getTypeName();
    }

    deviceTypeConfigs.clear();
    deviceTypeConfigs.resize(deviceTypes.size());
  }
}

void YSE::IO::ioManager::scanDevices() {
  if (deviceScanNeeded) {
    deviceScanNeeded = false;

    createDeviceTypes();
    FOREACH(deviceTypes) {
      deviceTypes[i].get()->scan();
    }
  }
}

const std::vector<std::unique_ptr<YSE::IO::ioDeviceType>> & YSE::IO::ioManager::getDeviceTypes() {
  scanDevices();
  return deviceTypes;
}

YSE::IO::ioDeviceType * YSE::IO::ioManager::getCurrentDeviceTypeObject() const {
  FOREACH(deviceTypes) {
    if (deviceTypes[i]->getTypeName() == activeDeviceType) {
      return deviceTypes[i].get();
    }
  }
  if (!deviceTypes.empty()) {
    return deviceTypes[0].get();
  }
  return nullptr;
}

std::string YSE::IO::ioManager::initialise(int inputChannels, int outputChannels, const std::string & devicename, const ioSetup * preferredSetup) {
  scanDevices();

  inputChannelsNeeded = inputChannels;
  outputChannelsNeeded = outputChannels;

  ioSetup setup;
  // set to preferred setup if we have one
  if (preferredSetup != nullptr) {
    setup = *preferredSetup;
  }

  // otherwise look if there is a devicename set
  else if (!devicename.empty()) {
    // get output name
    FOREACH(deviceTypes) {
      const std::vector<std::string> & out = deviceTypes[i]->getDeviceNames();
      FOREACH_D(j, out) {
        if (matchesWildcard(out[j], devicename)) {
          setup.outputName = out[i];
          break;
        }
      }
    }

    // get inputname
    FOREACH(deviceTypes) {
      const std::vector<std::string> & out = deviceTypes[i]->getDeviceNames(true);
      FOREACH_D(j, out) {
        if (matchesWildcard(out[j], devicename)) {
          setup.inputName = out[i];
          break;
        }
      }
    }
  }

  // load default name if a name is empty
  if (ioDeviceType * type = getCurrentDeviceTypeObject()) {
    if (setup.outputName.empty()) {
      setup.outputName = type->getDeviceNames(false)[type->getDefaultDeviceIndex(false)];
    }
    if (setup.inputName.empty()) {
      setup.inputName = type->getDeviceNames(true)[type->getDefaultDeviceIndex(true)];
    }
  }

  return setDeviceSetup(setup);
}

void YSE::IO::ioManager::getDeviceSetup(YSE::IO::ioSetup & setup) {
  setup = currentSetup;
}

std::string YSE::IO::ioManager::setDeviceSetup(const ioSetup& newSetup) {
  assert(&newSetup != &currentSetup);

  if (newSetup == currentSetup && currentDevice != nullptr) {
    return std::string();
  }

  stopDevice();

  const std::string newInputName(inputChannelsNeeded == 0 ? "" : newSetup.inputName);
  const std::string newOutputName(outputChannelsNeeded == 0 ? "" : newSetup.outputName);

  std::string error;
  ioDeviceType * type = getCurrentDeviceTypeObject();

  if (type == nullptr || (newInputName.empty() && newOutputName.empty())) {
    deleteCurrentDevice();
    return std::string();
  }

  if (currentSetup.inputName != newInputName
    || currentSetup.outputName != newOutputName
    || currentDevice == nullptr) {
    
    deleteCurrentDevice();
    scanDevices();

    if (!newOutputName.empty()) {
      bool found = false;
      const std::vector<std::string> & names = type->getDeviceNames();
      FOREACH(names) {
        if (names[i].compare(newOutputName) == 0) {
          found = true;
          break;
        }
      }
      if (!found) return "No such device: " + newOutputName;
    }

    if (!newInputName.empty()) {
      bool found = false;
      const std::vector<std::string> & names = type->getDeviceNames(true);
      FOREACH(names) {
        if (names[i].compare(newInputName) == 0) {
          found = true;
          break;
        }
      }
      if (!found) return "No such device: " + newInputName;
    }
    
    currentDevice.reset(type->createDevice(newOutputName, newInputName));

    if (currentDevice == nullptr) {
      error = "Can't open the audio device!\n\n"
        "This may be because another application is currently using the same device - "
        "if so, you should close any other applications and try again!";
    }
    else {
      error = currentDevice->getLastError();
    }

    if (!error.empty()) {
      deleteCurrentDevice();
      return error;
    }

    if (newSetup.useDefaultInput) {
      inputChannels = 0;
      inputChannels = (inputChannelsNeeded == 64 ? 0xFFFFFFFFFFFFFFFF : ((U64)1 << inputChannelsNeeded) - 1);
    }

    if (newSetup.useDefaultOutput) {
      outputChannels = 0;
      outputChannels = (outputChannelsNeeded == 64 ? 0xFFFFFFFFFFFFFFFF : ((U64)1 << outputChannelsNeeded) - 1);
    }

    if (newInputName.empty()) inputChannels = 0;
    if (newOutputName.empty()) outputChannels = 0;
  }

  if (!newSetup.useDefaultInput) inputChannels = newSetup.inputChannels;
  if (!newSetup.useDefaultOutput) outputChannels = newSetup.outputChannels;

  currentSetup = newSetup;
  currentSetup.sampleRate = pickBestSampleRate(newSetup.sampleRate);
  currentSetup.bufferSize = pickBestBufferSize(newSetup.bufferSize);

  error = currentDevice->open(inputChannels, outputChannels, currentSetup.sampleRate, currentSetup.bufferSize);
  if (error.empty()) {
    activeDeviceType = currentDevice->getTypeName();

    currentDevice->start(handler.get());

    currentSetup.sampleRate = currentDevice->getCurrentSampleRate();
    currentSetup.bufferSize = currentDevice->getCurrentBufferSize();
    currentSetup.inputChannels = currentDevice->getActiveInputChannels();
    currentSetup.outputChannels = currentDevice->getActiveOutputChannels();

    FOREACH(deviceTypes) {
      if (deviceTypes[i]->getTypeName().compare(activeDeviceType) == 0) {
        deviceTypeConfigs[i] = currentSetup;
      }
    }
  }
  else {
    deleteCurrentDevice();
  }

  return error;
}

Dbl YSE::IO::ioManager::pickBestSampleRate(Dbl rate) const {
  assert(currentDevice != nullptr);

  const std::vector<Dbl> & rates = currentDevice->getAvailableSampleRates();
  if (rate > 0) {
    FOREACH(rates) {
      if (rates[i] == rate) return rate;
    }
  }

  Dbl lowestAbove44 = 0.0;

  for (int i = rates.size(); --i >= 0;)
  {
    const Dbl sr = rates[i];

    if (sr >= 44100.0 && (lowestAbove44 < 1.0 || sr < lowestAbove44))
      lowestAbove44 = sr;
  }

  if (lowestAbove44 > 0.0)
    return lowestAbove44;

  return rates[0];
}

Int YSE::IO::ioManager::pickBestBufferSize(Int bufferSize) const {
  assert(currentDevice != nullptr);

  const std::vector<Int> & buffersizes = currentDevice->getAvailableBufferSizes();
  if (bufferSize > 0) {
    FOREACH(buffersizes) {
      if (buffersizes[i] == bufferSize) return bufferSize;
    }
  }

  return currentDevice->getDefaultBufferSize();
}

void YSE::IO::ioManager::stopDevice() {
  if (currentDevice != nullptr) currentDevice->stop();
}

void YSE::IO::ioManager::closeDevice() {
  stopDevice();
  currentDevice.reset();
}

void YSE::IO::ioManager::restartDevice() {
  if (currentDevice == nullptr) {
    if (currentSetup.inputName.empty() && currentSetup.outputName.empty()) {
      // a device must have been active before
      assert(false);
      return;
    }

    ioSetup s(currentSetup);
    setDeviceSetup(s);
  }
}

void YSE::IO::ioManager::addCallback(YSE::IO::ioCallback * newCallback) {
  {
    LOCK(audioCallbackMutex);
    FOREACH(callbacks) {
      if (callbacks[i] == newCallback) return;
    }
  }

  if (currentDevice != nullptr && newCallback != nullptr) {
    newCallback->onStart(currentDevice.get());
  }

  LOCK(audioCallbackMutex);
  callbacks.emplace_back(newCallback);
}

void YSE::IO::ioManager::removeCallback(YSE::IO::ioCallback * toRemove) {
  if (toRemove != nullptr) {
    bool inform = currentDevice != nullptr;

    {
      LOCK(audioCallbackMutex);
      bool found = false;

      for(auto i = callbacks.begin(); i != callbacks.end(); ++i) {
        if ((*i) == toRemove) {
          found = true;
          callbacks.erase(i);
          break;
        }
      }

      inform = inform && found;
    }

    if (inform) toRemove->onStop();
  }
}

void YSE::IO::ioManager::internalCallback(const float** inputChannelData, int numInputChannels, float** outputChannelData, int numOutputChannels, int numSamples) {
  LOCK(audioCallbackMutex);

  if (!callbacks.empty()) {
    callbacks[0]->onCallback(inputChannelData, numInputChannels, outputChannelData, numOutputChannels, numSamples);
  }
  else {
    for (int i = 0; i < numOutputChannels; ++i) {
      memset(outputChannelData[i], 0, sizeof(outputChannelData[i]));
    }
  }
}

void YSE::IO::ioManager::internalStart(YSE::IO::ioDevice * const device) {
  LOCK(audioCallbackMutex);
  if (!callbacks.empty()) {
    callbacks[0]->onStart(device);
  }
}

void YSE::IO::ioManager::internalStop() {
  LOCK(audioCallbackMutex);
  if (!callbacks.empty()) {
    callbacks[0]->onStop();
  }
}

void YSE::IO::ioManager::internalError(const std::string & message) {
  LOCK(audioCallbackMutex);
  if (!callbacks.empty()) {
    callbacks[0]->onError(message);
  }
}

double YSE::IO::ioManager::getCpuUsage() const {
  assert(false); // not implemented yet
  return 0;
}

