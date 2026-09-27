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
  // MIDI file playback is advanced every block too (issue #155) so events reach
  // the connected synths block-accurately, before the synths render this block.
  MIDI::Manager().updatePlayback(numSamples);

  if (SOUND::Manager().empty()) {
    // Nothing renders this callback, so renderOneBlock() will not tick the
    // domain clocks. They derive from the sample clock and must keep moving
    // whether or not a sound is playing (issue #249), so advance them here by
    // the whole callback.
    advanceDomainClocks(numSamples);
    return false;
  }

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

void YSE::DEVICE::deviceManager::advanceDomainClocks(int numSamples) {
  // Domain clocks (issue #249) first, then the clip transports (issue #250),
  // so each transport reads its bound clock's freshly-updated beat window and
  // fires the note events that fall inside it.
  CLOCK::Manager().update((Flt)numSamples / (Flt)SAMPLERATE);
  CLIP::Manager().update();
}

void YSE::DEVICE::deviceManager::renderOneBlock() {
  // Domain clocks advance per rendered block, not per device callback
  // (issue #944). A callback larger than one block renders several blocks back
  // to back; ticking the clocks once per callback put every beat deadline in
  // that callback into its first block, up to one device buffer early. Ticking
  // here keeps a patcher's beat waits (which it checks at the top of each
  // block) and the clip transports accurate to one block at any buffer size,
  // exactly as renderOffline() always was.
  advanceDomainClocks(STANDARD_BUFFERSIZE);
  CHANNEL::Manager().render(*master);
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
