/*
  ==============================================================================

    deviceManager.cpp
    Created: 27 Jan 2014 8:04:27pm
    Author:  yvan

  ==============================================================================
*/

#include "../internalHeaders.h"

UInt YSE::SAMPLERATE = 44100;

YSE::DEVICE::managerObject & YSE::DEVICE::Manager() {
  static managerObject d;
  return d;
}

YSE::DEVICE::managerObject::managerObject() : initialized(false), open(false),
                                      started(false), master(nullptr), 
                                      bufferPos(STANDARD_BUFFERSIZE) {
  updateDeviceList();
  currentInputChannels = 0;
  currentOutputChannels = 2;
}


Bool YSE::DEVICE::managerObject::init() {
  if (!initialized) {
    deviceSetup.sampleRate = 44100;
    deviceSetup.bufferSize = STANDARD_BUFFERSIZE;

    _lastError = audioDeviceManager.initialise(currentInputChannels, currentOutputChannels, nullptr, true, "", &deviceSetup);
    if (_lastError.isNotEmpty()) {
      jassertfalse;
      return false;
    }
    initialized = true;
    SAMPLERATE = static_cast<UInt>(audioDeviceManager.getCurrentAudioDevice()->getCurrentSampleRate());
    defaultTypeName = audioDeviceManager.getCurrentAudioDevice()->getTypeName().toStdString();
    defaultDeviceName = audioDeviceManager.getCurrentAudioDevice()->getName().toStdString();
    
    if (!open) {
      audioDeviceManager.addAudioCallback(this);
      open = true;
    }  
  }
  return true;
}

void YSE::DEVICE::managerObject::openDevice(const YSE::DEVICE::setupObject & object) {
  // device should be closed at this point
  jassert(open == false);
  
  // transform this to a juce setup object  
  currentInputChannels = 0;
  currentOutputChannels = 0;
  if (object.in != nullptr) {
    deviceSetup.inputDeviceName = object.in->pimpl->getName();
    currentInputChannels = object.in->pimpl->getInputChannelNames().size();
    deviceSetup.inputChannels.setRange(0, currentInputChannels, true);
    deviceSetup.useDefaultInputChannels = false;
  }

  if (object.out != nullptr) {
    deviceSetup.outputDeviceName = object.out->pimpl->getName();
    currentOutputChannels = object.out->pimpl->getOutputChannelNames().size();
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

void YSE::DEVICE::managerObject::setMaster(CHANNEL::implementationObject * ptr) {
  master = ptr;
}

YSE::CHANNEL::implementationObject & YSE::DEVICE::managerObject::getMaster() {
  return *master;
}

void YSE::DEVICE::managerObject::audioDeviceIOCallback(const float ** inputChannelData,
  int      numInputChannels,
  float ** outputChannelData,
  int      numOutputChannels,
  int      numSamples) {
  if (!open) return;
  if (master == nullptr) return;  

  if (INTERNAL::Global().needsUpdate()) {
    // update global objects
    INTERNAL::Time().update();
    INTERNAL::ListenerImpl().update();
    SOUND::Manager().update();
    CHANNEL::Manager().update();
    REVERB::Manager().update();
    MIDI::Manager().update();
    SCALE::Manager().update();
    MOTIF::Manager().update();
    // TODO: check if we still have to release sounds (see old code)
    INTERNAL::Global().updateDone();
  }

  // player and synth managers update all the time, because midi messages might come in
  // between two buffer updates and should have the least latency possible
  INTERNAL::DeviceTime().update();
  PLAYER::Manager().update((Flt)numSamples / (Flt)SAMPLERATE);
  SYNTH::Manager().update();

  if (SOUND::Manager().empty()) return;
  
  /* adjust channels if needed
     this actually realocates a lot of memory but it is only done when changing to an
     output that doesn't have the same amount of channels. Some jitter is to be expected
     at that point anyway.
  */
  if (CHANNEL::Manager().getNumberOfOutputs() != master->out.size()) {
    CHANNEL::Manager().changeChannelConf();
    master->resize(true);
  }

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
        devices.emplace_back(device);
      }
    }
  }
}

const std::vector<YSE::DEVICE::interfaceObject> & YSE::DEVICE::managerObject::getDeviceList() {
  return devices;
}

const std::string & YSE::DEVICE::managerObject::getDefaultDeviceName() {
  return defaultDeviceName;
}

const std::string & YSE::DEVICE::managerObject::getDefaultTypeName() {
  return defaultTypeName;
}