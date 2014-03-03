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


YSE::INTERNAL::soundManager::soundManager() : Thread(juce::String("soundManager")) {
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

YSE::INTERNAL::soundFile * YSE::INTERNAL::soundManager::add(const File & file) {
  // find out if this file already exists
  for (auto i = soundFiles.begin(); i != soundFiles.end(); ++i) {
    if ( i->_file == file) {
      i->clients++;
      return &(*i);
    }
  }

  // if we got here, the file does not exist yet
  soundFiles.emplace_front();
  soundFile & sf = soundFiles.front();
  sf.clients++;
  if (sf.create(file)) {
    return &sf;
  }
  else {
    sf.release();
    return NULL;
  }
}

void YSE::INTERNAL::soundManager::addToQue(soundFile * elm) {
  const ScopedLock readQueLock(readQue);
  soundFilesQue.push_back(elm);
}


Bool YSE::INTERNAL::soundManager::empty() {
  return soundImplementations.empty();
}

void YSE::INTERNAL::soundManager::run() {
  for (;;) {
    { // <- extra braces are needed to unlock readQueLock when no files are queued.
      // lock because we deque is not threadsafe
      const ScopedLock readQueLock(readQue);
      while (soundFilesQue.size()) {
        soundFile * s = (soundFile *)soundFilesQue.front();
        soundFilesQue.pop_front();

        // done with soundFilesQue for now. We can unlock.
        const ScopedUnlock readQueUnlock(readQue);

        // Now try to read the soundfile in a memory buffer
        currentAudioFileSource = nullptr;
        juce::ScopedPointer<AudioFormatReader> reader = formatManager.createReaderFor(s->_file);
        if (reader != nullptr) {
          s->_buffer.setSize(reader->numChannels, (Int)reader->lengthInSamples);
          reader->read(&s->_buffer, 0, (Int)reader->lengthInSamples, 0, true, true);

          // sample rate adjustment
          s->_sampleRateAdjustment = static_cast<Flt>(reader->sampleRate) / static_cast<Flt>(SAMPLERATE);
          s->_length = s->_buffer.getNumSamples();

          // file is ready for use now
          s->_state = YSE::INTERNAL::READY;
        }
        else {
          Global.getLog().emit(E_FILEREADER, "Unable to read " + s->_file.getFullPathName().toStdString());
        }

        // check for thread exit signal (in case multiple sounds are loaded)
        if (threadShouldExit()) {
          return;
        }
      }

      // check for thread exit signal (in case no sounds are loaded)
      if (threadShouldExit()) {
        return;
      }
    }
    // enter wait state
    wait(-1);
  }
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
        ptr->parent->add(ptr);
      }
    }
  }

  ///////////////////////////////////////////
  // update actual soundfiles
  ///////////////////////////////////////////
  auto iMinus = soundFiles.before_begin();
  for (auto i = soundFiles.begin(); i != soundFiles.end(); ) {
    if (!i->inUse()) {
      i = soundFiles.erase_after(iMinus);
    }
    else {
      iMinus = i;
      ++i;
    }
  }

  Int playingSounds = 0;

  ///////////////////////////////////////////
  // sync and update sound implementations
  ///////////////////////////////////////////
  {
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
        ++i;
        continue;
      }

      // count playing sounds
      playingSounds++;
      previous = i;
      ++i;
    }
  }

  if (nonVirtualSize < playingSounds) {
    soundsInUse.sort(soundImplementation::sortSoundObjects);
  }

  auto index = soundsInUse.begin();
  for (int i = 0; i < nonVirtualSize && index != soundsInUse.end(); index++, i++) {
    (*index)->isVirtual = false;
  }
}

void YSE::INTERNAL::soundManager::maxSounds(Int value) {
  nonVirtualSize = value;
}

Int YSE::INTERNAL::soundManager::maxSounds() {
  return nonVirtualSize;
}

YSE::INTERNAL::soundImplementation * YSE::INTERNAL::soundManager::addImplementation(sound * head) {
  soundImplementations.emplace_front(head);
  return &soundImplementations.front();
}

void YSE::INTERNAL::soundManager::setup(YSE::INTERNAL::soundImplementation * impl) {
  soundsToLoad.emplace_front(impl);
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
