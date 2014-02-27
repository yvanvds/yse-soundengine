/*
  ==============================================================================

    deviceManager.cpp
    Created: 27 Jan 2014 8:04:27pm
    Author:  yvan

  ==============================================================================
*/

#include "deviceManager.h"
#include "global.h"
#include "JuceHeader.h"
#include "../headers/constants.hpp"
#include "../internal/soundManager.h"
#include "../internal/time.h"
#include "../internal/channelManager.h"
#include "../implementations/channelImplementation.h"
#include "../implementations/logImplementation.h"
#include "../implementations/soundImplementation.h"
#include "../implementations/listenerImplementation.h"

juce_ImplementSingleton(YSE::INTERNAL::deviceManager)

YSE::INTERNAL::deviceManager::deviceManager() : initialized(false), open(false),
                                      started(false), master(NULL), 
                                      bufferPos(STANDARD_BUFFERSIZE) {

}

YSE::INTERNAL::deviceManager::~deviceManager() {
  close();
  clearSingletonInstance();
}

Bool YSE::INTERNAL::deviceManager::init() {
  if (!initialized) {
    _lastError = audioDeviceManager.initialise(0, 2, NULL, true);
    if (_lastError.isNotEmpty()) {
      //Error.emit(E_AUDIODEVICE, _lastError.toStdString());
      jassertfalse
      return false;
    }
    initialized = true;

    if (!open) {
      audioDeviceManager.addAudioCallback(this);
      open = true;
    }  
  }
  return true;
}

void YSE::INTERNAL::deviceManager::close() {
  if (open) {
    audioDeviceManager.closeAudioDevice();
    open = false;
  }
}

Flt YSE::INTERNAL::deviceManager::cpuLoad() {
  return static_cast<Flt>(audioDeviceManager.getCpuUsage());
}

void YSE::INTERNAL::deviceManager::setMaster(channelImplementation * ptr) {
  master = ptr;
}

YSE::INTERNAL::channelImplementation & YSE::INTERNAL::deviceManager::getMaster() {
  return *master;
}

void YSE::INTERNAL::deviceManager::audioDeviceIOCallback(const float ** inputChannelData,
  int      numInputChannels,
  float ** outputChannelData,
  int      numOutputChannels,
  int      numSamples) {
  
  if (master == NULL) return;  

  if (Global.needsUpdate()) {
    // update global objects
    INTERNAL::Global.getTime().update();
    INTERNAL::Global.getListener().update();
    INTERNAL::Global.getSoundManager().update();
    INTERNAL::Global.getChannelManager().update();
    // TODO: check if we still have to release sounds (see old code)
    Global.updateDone();
  }

  if (Global.getSoundManager().empty()) return;
  
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
      Flt * ptr2 = master->out[i].getBuffer() + bufferPos;
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

void YSE::INTERNAL::deviceManager::audioDeviceAboutToStart(AudioIODevice * device) {

}

void YSE::INTERNAL::deviceManager::audioDeviceStopped() {

}

void YSE::INTERNAL::deviceManager::audioDeviceError(const juce::String & errorMessage) {
  Global.getLog().emit(E_AUDIODEVICE, errorMessage);
}
