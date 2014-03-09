/*
  ==============================================================================

    soundLoader.cpp
    Created: 28 Jan 2014 11:49:12am
    Author:  yvan

  ==============================================================================
*/

#include "soundManager.h"
#include "soundFile.h"
#include "../headers/constants.hpp"
#include "global.h"
#include "../implementations/soundImplementation.h"
#include "../implementations/channelImplementation.h"
#include "../internal/deviceManager.h"
#include "../internal/channelManager.h"
#include "../implementations/logImplementation.h"

juce_ImplementSingleton(YSE::INTERNAL::soundManager)

ThreadPoolJob::JobStatus YSE::INTERNAL::soundSetupJob::runJob() {
  for (auto i = Global.getSoundManager().soundsToLoad.begin(); i != Global.getSoundManager().soundsToLoad.end(); ++i) {
    i->load()->setup();
  }
  return jobHasFinished;
}

ThreadPoolJob::JobStatus YSE::INTERNAL::soundDeleteJob::runJob() {
  Global.getSoundManager().soundImplementations.remove_if(soundImplementation::canBeDeleted);
  return jobHasFinished;
}


YSE::INTERNAL::soundManager::soundManager() : vFinder(10) {
  formatManager.registerBasicFormats();
}

YSE::INTERNAL::soundManager::~soundManager() {
  // wait for jobs to finish
  Global.waitForSlowJob(&soundSetup);
  Global.waitForSlowJob(&soundDelete);
  // remove all sounds that are still in memory
  soundsToLoad.clear();
  soundsInUse.clear();
  soundImplementations.clear();
  soundFiles.clear();
  clearSingletonInstance();
}

YSE::INTERNAL::soundFile * YSE::INTERNAL::soundManager::addFile(const File & file) {
  // find out if this file already exists
  for (auto i = soundFiles.begin(); i != soundFiles.end(); ++i) {
    if ( i->contains(file)) {
      i->clients++;
      return &(*i);
    }
  }

  // if we got here, the file does not exist yet
  soundFiles.emplace_front(file);
  soundFile & sf = soundFiles.front();
  sf.clients++;
  if (sf.create()) {
    return &sf;
  }
  else {
    sf.release();
    return nullptr;
  }
}

YSE::INTERNAL::soundImplementation * YSE::INTERNAL::soundManager::addImplementation(sound * head) {
  soundImplementations.emplace_front(head);
  return &soundImplementations.front();
}


void YSE::INTERNAL::soundManager::setup(YSE::INTERNAL::soundImplementation * impl) {
  soundsToLoad.emplace_front(impl);
}

void YSE::INTERNAL::soundManager::update() {
  ///////////////////////////////////////////
  // check if there are soundimplementations that need setup
  ///////////////////////////////////////////
  if (!soundsToLoad.empty() && !Global.containsSlowJob(&soundSetup)) {
    // removing cannot be done in a separate thread because we are iterating over this
    // list a during this update fuction
    soundsToLoad.remove_if(soundImplementation::canBeRemovedFromLoading);
    Global.addSlowJob(&soundSetup);
  }

  if (runDelete && !Global.containsSlowJob(&soundDelete)) {
    Global.addSlowJob(&soundDelete);
  }
  runDelete = false;

  ///////////////////////////////////////////
  // check if loading soundimplementations are ready
  ///////////////////////////////////////////
  {
    for (auto i = soundsToLoad.begin(); i != soundsToLoad.end(); i++) {
      if (i->load()->readyCheck()) {
        soundImplementation * ptr = i->load();
        // place ptr in active sound list
        soundsInUse.emplace_front(ptr);
        // add the sound to the channel that is supposed to use
        ptr->parent->connect(ptr);
      }
    }
  }

  ///////////////////////////////////////////
  // update actual soundfiles
  ///////////////////////////////////////////
  auto iMinus = soundFiles.before_begin();
  for (auto i = soundFiles.begin(); i != soundFiles.end(); ) {
    
    // don't handle files currently in the thread pool
    if (Global.containsSlowJob(&(*i))) {
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
  // sync and update sound implementations
  ///////////////////////////////////////////
  {
    vFinder.reset();
    auto previous = soundsInUse.before_begin();
    for (auto i = soundsInUse.begin(); i != soundsInUse.end();) {
      (*i)->sync();
      if ((*i)->objectStatus == SIS_RELEASE) {
        soundImplementation * ptr = (*i);
        i = soundsInUse.erase_after(previous);
        ptr->objectStatus = SIS_DELETE;
        runDelete = true;
        continue;
      }
      // update
      (*i)->update();

      // don't count sounds that are not playing 
      if ((*i)->objectStatus < SIS_READY || (*i)->status_upd == YSE::SS_STOPPED || (*i)->status_upd == YSE::SS_PAUSED) {
        previous = i;
        ++i;
        continue;
      }

      // add to virtual sound finder
      vFinder.add((*i)->virtualDist);
      previous = i;
      ++i;
    }
    vFinder.calculate();
  }
}


Bool YSE::INTERNAL::soundManager::empty() {
  return soundImplementations.empty();
}

void YSE::INTERNAL::soundManager::setMaxSounds(Int value) {
  vFinder.setLimit(value);
}

Int YSE::INTERNAL::soundManager::getMaxSounds() {
  return vFinder.getLimit();
}

Bool YSE::INTERNAL::soundManager::inRange(Flt dist) {
  return vFinder.inRange(dist);
}

AudioFormatReader * YSE::INTERNAL::soundManager::getReader(const File & f) {
  return formatManager.createReaderFor(f);
}

void YSE::INTERNAL::soundManager::adjustLastGainBuffer() {
  for (auto i = soundsInUse.begin(); i != soundsInUse.end(); ++i) {
    UInt j = (*i)->lastGain.size(); // need to store previous size for deep resize
    (*i)->lastGain.resize(Global.getChannelManager().getNumberOfOutputs());
    for (; j < (*i)->lastGain.size(); j++) {
      (*i)->lastGain[j].resize((*i)->buffer->size(), 0.0f);
    }
  }
}
