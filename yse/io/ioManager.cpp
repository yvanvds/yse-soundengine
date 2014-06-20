/*
  ==============================================================================

    ioManager.cpp
    Created: 12 Jun 2014 6:29:57pm
    Author:  yvan

  ==============================================================================
*/

#include "ioManager.h"
#include "../internalHeaders.h"

YSE::IO::ioManager & YSE::IO::Manager() {
  static ioManager io;
  return io;
}

YSE::IO::ioManager::ioManager() : initDone(false), currentDevice(nullptr) {

}

YSE::IO::ioManager::~ioManager() {

}

Bool YSE::IO::ioManager::init() {
  // only do this once
  if (initDone) return currentDeviceType != nullptr;

  currentDeviceType = ioDeviceType::Create(DIRECTSOUND);
  if (currentDeviceType != nullptr) {
    currentDeviceType->scanDevices();
  }
  initDone = true;
  return (currentDeviceType != nullptr);
}

Bool YSE::IO::ioManager::isRunning() {
  if (currentDevice == nullptr) return false;
  return currentDevice->isRunning();
}

void YSE::IO::ioManager::startDevice() {
  assert(currentDevice != nullptr);
  currentDevice->start(this);
}

void YSE::IO::ioManager::stopDevice() {
  assert(currentDevice != nullptr);
  currentDevice->stop();
}

void YSE::IO::ioManager::restartDevice() {
  assert(currentDevice != nullptr);
  currentDevice->stop();
  currentDevice->start(this);
}

std::vector<std::wstring> YSE::IO::ioManager::getDeviceList() {
  std::vector<std::wstring> devices;
  for (Int i = 0; i < currentDeviceType->getNumDevices(); i++) {
    devices.emplace_back(currentDeviceType->getDevice(i)->getName());
  }
  return std::move(devices);
}

void YSE::IO::ioManager::setActiveDevice(int num) {
  if (num == -1) {
    // open default device
    for (int i = 0; i < currentDeviceType->getNumDevices(); i++) {
      if (currentDeviceType->getDevice(i)->isDefault()) {
        currentDevice = currentDeviceType->getDevice(i);
        currentDevice->open();
        break;
      }
    }
    return;
  }
  if (num < 0 || num >= currentDeviceType->getNumDevices()) {
    // this device does not exist
    assert(false);
  }

  currentDevice = currentDeviceType->getDevice(num);
  currentDevice->open();
}

void YSE::IO::ioManager::setDefaultDeviceAsActive() {
  setActiveDevice();
}

Bool YSE::IO::ioManager::isReadyToStart() {
  if (currentDevice == nullptr) return false;
  return true;
}

void YSE::IO::ioManager::setMaster(CHANNEL::implementationObject * ptr) {
  master = ptr;
}

YSE::CHANNEL::implementationObject & YSE::IO::ioManager::getMaster() {
  return *master;
}

////////////////////////////////////////////////////
// callback methods
////////////////////////////////////////////////////

void YSE::IO::ioManager::onCallback(const std::vector<AUDIOBUFFER> & inputChannels, std::vector<AUDIOBUFFER> & outputChannels) {

  if (master == nullptr) return;

  if (INTERNAL::Global().needsUpdate()) {
    // update global objects
    INTERNAL::Time().update();
    INTERNAL::Global().getListener().update();
    SOUND::Manager().update();
    CHANNEL::Manager().update();
    REVERB::Manager().update();
    // TODO: check if we still have to release sounds (see old code)
    INTERNAL::Global().updateDone();
  }

  if (SOUND::Manager().empty()) return;

  /* adjust channels if needed
  this actually realocates a lot of memory but it is only done when changing to an
  output that doesn't have the same amount of channels. Some jitter is to be expected
  at that point anyway.
  */
  if (CHANNEL::Manager().getNumberOfOutputs() != master->out.size()) {
    CHANNEL::Manager().changeChannelConf();
    master->setup();
  }

  UInt pos = 0;
  static UInt length = outputChannels[0].getLength();
  while (pos < length) {
    if (bufferPos == STANDARD_BUFFERSIZE) {
      master->dsp();
      master->buffersToParent();
      bufferPos = 0;
    }

    UInt size = (length - pos) >(STANDARD_BUFFERSIZE - bufferPos) ? (STANDARD_BUFFERSIZE - bufferPos) : (length - pos);
    for (UInt i = 0; i < master->out.size(); i++) {
      // this is not really a safe way to work with buffers, but it won't give any errors in here
      UInt l = size;
      Flt * ptr1 = outputChannels[i].getBuffer() + pos;
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

void YSE::IO::ioManager::onStart() {

}

void YSE::IO::ioManager::onStop() {

}

void YSE::IO::ioManager::onError(const std::wstring & errorMessage) {
  //INTERNAL::Global().getLog().emit(E_AUDIODEVICE, errorMessage);
}