/*
  ==============================================================================

    soundLoader.cpp
    Created: 28 Jan 2014 11:49:12am
    Author:  yvan

  ==============================================================================
*/

#include "soundManager.h"
#include "../internal/global.h"
#include "../internal/virtualFinder.h"
#include "../channel/channelManager.h"

juce_ImplementSingleton(YSE::SOUND::managerObject)


YSE::SOUND::managerObject::managerObject() : managerTemplate<soundSubSystem>("soundManager") {
  formatManager.registerBasicFormats();
}

YSE::SOUND::managerObject::~managerObject() {
  // remove all sounds that are still in memory
  soundFiles.clear();
  clearSingletonInstance();
}

YSE::INTERNAL::soundFile * YSE::SOUND::managerObject::addFile(const File & file) {
  // find out if this file already exists
  for (auto i = soundFiles.begin(); i != soundFiles.end(); ++i) {
    if ( i->contains(file)) {
      i->clients++;
      return &(*i);
    }
  }

  // if we got here, the file does not exist yet
  soundFiles.emplace_front(file);
  INTERNAL::soundFile & sf = soundFiles.front();
  sf.clients++;
  if (sf.create()) {
    return &sf;
  }
  else {
    sf.release();
    return nullptr;
  }
}


void YSE::SOUND::managerObject::update() {
  ///////////////////////////////////////////
  // update actual soundfiles
  ///////////////////////////////////////////
  auto iMinus = soundFiles.before_begin();
  for (auto i = soundFiles.begin(); i != soundFiles.end(); ) {
    
    // don't handle files currently in the thread pool
    if (INTERNAL::Global.containsSlowJob(&(*i))) {
      iMinus = i;
      ++i;
      continue;
    }

    // invalid files or files no longer in use should be removed from memory
    if (!i->inUse()) {
      i = soundFiles.erase_after(iMinus);
    }
    else {
      iMinus = i;
      ++i;
    }
  }


  ///////////////////////////////////////////
  // update sound implementations
  ///////////////////////////////////////////
  VirtualSoundFinder().reset();
  managerTemplate<soundSubSystem>::update();
  VirtualSoundFinder().calculate();
  
}


//void YSE::SOUND::managerObject::setMaxSounds(Int value) {
//  VirtualSoundFinder().setLimit(value);
//}

//Int YSE::SOUND::managerObject::getMaxSounds() {
//  return VirtualSoundFinder().getLimit();
//}

//Bool YSE::SOUND::managerObject::inRange(Flt dist) {
//  return VirtualSoundFinder().inRange(dist);
//}

AudioFormatReader * YSE::SOUND::managerObject::getReader(const File & f) {
  return formatManager.createReaderFor(f);
}

void YSE::SOUND::managerObject::adjustLastGainBuffer() {
  for (auto i = inUse.begin(); i != inUse.end(); ++i) {
    UInt j = (*i)->lastGain.size(); // need to store previous size for deep resize
    (*i)->lastGain.resize(INTERNAL::Global.getChannelManager().getNumberOfOutputs());
    for (; j < (*i)->lastGain.size(); j++) {
      (*i)->lastGain[j].resize((*i)->buffer->size(), 0.0f);
    }
  }
}
