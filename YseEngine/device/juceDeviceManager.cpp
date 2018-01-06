/*
  ==============================================================================

    deviceManager.cpp
    Created: 27 Jan 2014 8:04:27pm
    Author:  yvan

  ==============================================================================
*/

#if JUCE_BACKEND

#include "../internalHeaders.h"
#include <Windows.h>

UInt YSE::SAMPLERATE = 44100;

YSE::DEVICE::managerObject & YSE::DEVICE::Manager() {
  static managerObject d;
  return d;
}

YSE::DEVICE::managerObject::managerObject() 
  : initialized(false)
  , open(false)
  , started(false)
  , bufferPos(STANDARD_BUFFERSIZE)
  , coInitialized(false) {
}

YSE::DEVICE::managerObject::~managerObject() {
  close();
#if defined YSE_WINDOWS
  if (coInitialized) {
    CoUninitialize();
    coInitialized = false;
  }
#endif
}

Bool YSE::DEVICE::managerObject::init() {
  // on windows, the COM library has to be initialized. This may or may not
  // also happen in your application. It doesn't matter.
#if defined YSE_WINDOWS
  if (SUCCEEDED(CoInitialize(nullptr))) {
    coInitialized = true;
    INTERNAL::LogImpl().emit(E_DEBUG, "COM library initialized");
  }
  else coInitialized = false;
#endif

  abstractDeviceManager::init();

  if (!initialized) {
    deviceSetup.sampleRate = 44100;
    deviceSetup.bufferSize = STANDARD_BUFFERSIZE;

    _lastError = audioDeviceManager.initialise(currentInputChannels, currentOutputChannels, nullptr, true, "", &deviceSetup);
    if (_lastError.isNotEmpty()) {
      assert(false);
      return false;
    }
    initialized = true;
    SAMPLERATE = static_cast<UInt>(audioDeviceManager.getCurrentAudioDevice()->getCurrentSampleRate());
    defaultTypeName = audioDeviceManager.getCurrentAudioDevice()->getTypeName().toStdString();
    defaultDeviceName = audioDeviceManager.getCurrentAudioDevice()->getName().toStdString();
    
     
  }
  return true;
}

void YSE::DEVICE::managerObject::addCallback() {
  if (!open) {
    audioDeviceManager.addAudioCallback(this);
    open = true;
  }
}

void YSE::DEVICE::managerObject::openDevice(const YSE::DEVICE::setupObject & object) {
  // device should be closed at this point
  assert(open == false);
  
  // transform this to a juce setup object  
  currentInputChannels = 0;
  currentOutputChannels = 0;
  if (object.in != nullptr) {
    deviceSetup.inputDeviceName = object.in->getName();
    currentInputChannels = object.in->getNumInputChannelNames();
    deviceSetup.inputChannels.setRange(0, currentInputChannels, true);
    deviceSetup.useDefaultInputChannels = false;
  }

  if (object.out != nullptr) {
    deviceSetup.outputDeviceName = object.out->getName();
    currentOutputChannels = object.out->getNumOutputChannelNames();
    deviceSetup.outputChannels.setRange(0, currentOutputChannels, true);
    deviceSetup.useDefaultOutputChannels = false;
  }

  deviceSetup.sampleRate = object.sampleRate;
  deviceSetup.bufferSize = object.bufferSize;

  openDevice();
}

void YSE::DEVICE::managerObject::openDevice() {
  // change to the new device, enabling the default device if it can't be opened.
  _lastError = audioDeviceManager.initialise(currentInputChannels, currentOutputChannels, nullptr, true, "", &deviceSetup);
  if (_lastError.isNotEmpty()) {
    // default device is chosen.
    // Should we inform the user?
  }

  SAMPLERATE = static_cast<UInt>(audioDeviceManager.getCurrentAudioDevice()->getCurrentSampleRate());
  audioDeviceManager.addAudioCallback(this);
  open = true;
}

void YSE::DEVICE::managerObject::close() {
  if (open) {
    open = false;
    audioDeviceManager.removeAudioCallback(this);
    ScopedLock lock(audioDeviceManager.getAudioCallbackLock());
    audioDeviceManager.closeAudioDevice();    
  }
}

Flt YSE::DEVICE::managerObject::cpuLoad() {
  return static_cast<Flt>(audioDeviceManager.getCpuUsage());
}

void YSE::DEVICE::managerObject::audioDeviceIOCallback(const float ** inputChannelData,
  int      numInputChannels,
  float ** outputChannelData,
  int      numOutputChannels,
  int      numSamples) {

  if (!open) return;
  if (!doOnCallback(numSamples)) return;

  UInt pos = 0;

  while (pos < static_cast<UInt>(numSamples)) {
    if (bufferPos == STANDARD_BUFFERSIZE) {
      master->dsp();
      master->buffersToParent();
      bufferPos = 0;
    }

    UInt size = (numSamples - pos) > (STANDARD_BUFFERSIZE - bufferPos) ? (STANDARD_BUFFERSIZE - bufferPos) : ((UInt)numSamples - pos);
    for (UInt i = 0; i < master->out.size(); i++) {
      // this is not really a safe way to work with buffers, but it won't give any errors in here
      UInt l = size;
      Flt * ptr1 = ((Flt **)outputChannelData)[i] + pos;
      Flt * ptr2 = master->out[i].getPtr() + bufferPos;
      for (; l > 7; l -= 8, ptr1 += 8, ptr2 += 8) {
        ptr1[0] = ptr2[0] < -1.f ? -1.f : ptr2[0] > 1.f ? 1.f : ptr2[0];
		    ptr1[1] = ptr2[1] < -1.f ? -1.f : ptr2[1] > 1.f ? 1.f : ptr2[1];
		    ptr1[2] = ptr2[2] < -1.f ? -1.f : ptr2[2] > 1.f ? 1.f : ptr2[2];
		    ptr1[3] = ptr2[3] < -1.f ? -1.f : ptr2[3] > 1.f ? 1.f : ptr2[3];
		    ptr1[4] = ptr2[4] < -1.f ? -1.f : ptr2[4] > 1.f ? 1.f : ptr2[4];
		    ptr1[5] = ptr2[5] < -1.f ? -1.f : ptr2[5] > 1.f ? 1.f : ptr2[5];
		    ptr1[6] = ptr2[6] < -1.f ? -1.f : ptr2[6] > 1.f ? 1.f : ptr2[6];
		    ptr1[7] = ptr2[7] < -1.f ? -1.f : ptr2[7] > 1.f ? 1.f : ptr2[7];

      }
      while (l--) *ptr1++ = *ptr2++;
    }
    bufferPos += size;
    pos += size;
  }
}

void YSE::DEVICE::managerObject::audioDeviceAboutToStart(AudioIODevice * device) {

}

void YSE::DEVICE::managerObject::audioDeviceStopped() {
  if (open) {
    // device is supposed to be open, but it's not
    openDevice();
  }
}

void YSE::DEVICE::managerObject::audioDeviceError(const juce::String & errorMessage) {
  INTERNAL::LogImpl().emit(E_AUDIODEVICE, errorMessage);
}

void YSE::DEVICE::managerObject::updateDeviceList() {
  OwnedArray<AudioIODeviceType> types;
  audioDeviceManager.createAudioDeviceTypes(types);
  devices.clear();

  for (int i = 0; i < types.size(); ++i) {
    types[i]->scanForDevices();
    StringArray deviceNames(types[i]->getDeviceNames());

    for (int j = 0; j < deviceNames.size(); ++j) {
      AudioIODevice * device = types[i]->createDevice(deviceNames[j], deviceNames[j]);
      // primary sound driver is not a real driver anyway, and it seems to crash on use
      if (!device->getName().equalsIgnoreCase("Primary Sound Driver")) {
        YSE::device d;
        d.setName(device->getName().toStdString());
        d.setTypeName(device->getTypeName().toStdString());

        StringArray in = device->getInputChannelNames();
        for (UInt i = 0; i < in.size(); i++) d.addInputChannelName(in[i].toStdString());

        StringArray out = device->getOutputChannelNames();
        for (UInt i = 0; i < out.size(); i++) d.addOutputChannelName(out[i].toStdString());

        Array<double> sr = device->getAvailableSampleRates();
        for (UInt i = 0; i < sr.size(); i++) d.addAvailableSampleRate(sr[i]);

        Array<int> bs = device->getAvailableBufferSizes();
        for (UInt i = 0; i < bs.size(); i++) d.addAvailableBufferSize(bs[i]);

        d.setDefaultBufferSize(device->getDefaultBufferSize());
        d.setOutputLatency(device->getOutputLatencyInSamples());
        d.setInputLatency(device->getInputLatencyInSamples());

        devices.push_back(d);
      }
    }
  }
}

#endif // JUCE_BACKEND