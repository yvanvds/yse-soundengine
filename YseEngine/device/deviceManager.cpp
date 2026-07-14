/*
  ==============================================================================

    abstractDeviceManager.cpp
    Created: 27 Jul 2016 1:46:50pm
    Author:  yvan

  ==============================================================================
*/

#include "../internalHeaders.h"

YSE::DEVICE::deviceManager::deviceManager()
  : master(nullptr), currentInputChannels(0), currentOutputChannels(2) {}

YSE::DEVICE::deviceManager::~deviceManager() {
  close();
}

Bool YSE::DEVICE::deviceManager::init(bool openDevice) {
  if (openDevice) updateDeviceList();
  return true;
}

bool YSE::DEVICE::deviceManager::doOnCallback(int numSamples) {
  if (master == nullptr) return false;

  if (INTERNAL::Global().needsUpdate()) {
    // update global objects
    INTERNAL::Time().update();
    INTERNAL::ListenerImpl().update();
    SOUND::Manager().update();
    SYNTH::Manager().update();
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
  // Advance every domain clock by this block's duration (issue #249). Domain
  // clocks derive from the single sample clock, so they tick here — every audio
  // callback, regardless of whether any sound is playing — and are advanced
  // before the empty()-sounds early-out below.
  CLOCK::Manager().update((Flt)numSamples / (Flt)SAMPLERATE);
  // Clip transports (issue #250) are advanced right after the clocks, every
  // audio callback, so each transport reads its bound clock's freshly-updated
  // beat window and fires the note events that fall inside this block.
  CLIP::Manager().update();
  // MIDI file playback is advanced every block too (issue #155) so events reach
  // the connected synths block-accurately, before the synths render this block.
  MIDI::Manager().updatePlayback(numSamples);

  if (SOUND::Manager().empty()) return false;

  /* adjust channels if needed
  this actually realocates a lot of memory but it is only done when changing to an
  output that doesn't have the same amount of channels. Some jitter is to be expected
  at that point anyway.
  */
  if (CHANNEL::Manager().getNumberOfOutputs() != master->out.size()) {
    CHANNEL::Manager().changeChannelConf();
    master->resize(true);
  }

  return true;
}

void YSE::DEVICE::deviceManager::renderOneBlock() {
  master->dsp();
  master->buffersToParent();
}

void YSE::DEVICE::deviceManager::renderOffline(int blocks) {
  for (int i = 0; i < blocks; ++i) {
    if (!doOnCallback(STANDARD_BUFFERSIZE)) continue;
    renderOneBlock();
  }
}

void YSE::DEVICE::deviceManager::setMaster(CHANNEL::implementationObject* ptr) {
  master = ptr;
}

YSE::CHANNEL::implementationObject& YSE::DEVICE::deviceManager::getMaster() {
  return *master;
}

const std::vector<YSE::device>& YSE::DEVICE::deviceManager::getDeviceList() {
  return devices;
}

const std::string& YSE::DEVICE::deviceManager::getDefaultTypeName() {
  return defaultTypeName;
}

const std::string& YSE::DEVICE::deviceManager::getDefaultDeviceName() {
  return defaultDeviceName;
}
