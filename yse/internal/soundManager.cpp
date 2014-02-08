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

YSE::INTERNAL::soundManager::soundManager() : Thread(juce::String("soundManager")) {
  formatManager.registerBasicFormats();
}

YSE::INTERNAL::soundManager::~soundManager() {
  clearSingletonInstance();
}

YSE::INTERNAL::soundFile * YSE::INTERNAL::soundManager::add(const File & file) {
  // find out if this file already exists
  for (std::forward_list<soundFile>::iterator i = soundFiles.begin(); i != soundFiles.end(); ++i) {
    if ( i->_file == file) {
      i->clients++;
      return &(*i);
    }
  }

  // if we got here, the file does not exist yet
  soundFiles.emplace_front();
  soundFile & sf = soundFiles.front();
  sf._file = file;
  sf.clients++;
  return &sf;
}

void YSE::INTERNAL::soundManager::addToQue(soundFile * elm) {
  const ScopedLock readQueLock(readQue);
  soundFilesQue.push_back(elm);
}


Bool YSE::INTERNAL::soundManager::empty() {
  if (soundObjects.empty()) return true;
  return false;
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
  // update soundFiles
  std::forward_list<soundFile>::iterator iMinus = soundFiles.before_begin();
  for (std::forward_list<soundFile>::iterator i = soundFiles.begin(); i != soundFiles.end(); ++i) {
    if (!i->inUse()) {
      soundFiles.erase_after(iMinus);
      i = iMinus;
    }
    iMinus = i;
  }

  // update sound objects & calculate virtual sounds
  std::vector<soundImplementation*>::iterator index = nonVirtual.begin(); 
  std::forward_list<soundImplementation>::iterator previous = soundObjects.before_begin();
  for (std::forward_list<soundImplementation>::iterator i = soundObjects.begin(); i != soundObjects.end(); ++i) {
    i->update();

    // delete sounds that are stopped and not in use any more
    if (i->intent == YSE::SS_STOPPED && i->_release) {
      soundObjects.erase_after(previous);
      i = previous;
      continue;
    }
    previous = i;

    // skip sound that don't play
    if (i->_loading) continue;
    if (i->intent == YSE::SS_STOPPED) continue;
    if (i->intent == YSE::SS_PAUSED) continue;

    // fill buffer while not full
    if (index != nonVirtual.end()) {
      (*index) = &(*i);
      ++index;
      if (index == nonVirtual.end()) findLeastImportant();
    }
    else {
      if ((!i->parent->allowVirtual) || (i->virtualDist < nonVirtual[leastImportant]->virtualDist)) {
        // if leastImportant == -1 no sounds are allowed to go virtual, so we can't add any more sounds
        // sounds that are not allowed to go virtual are played anyway because isVirtual is
        // set to false in the update loop

        if (leastImportant > -1) {
          nonVirtual[leastImportant] = &(*i);
          findLeastImportant();
        }
      }
    }
  }

  for (std::vector<soundImplementation*>::iterator i = nonVirtual.begin(); i < index; ++i) {
    (*i)->isVirtual = false;
  }
}

void YSE::INTERNAL::soundManager::maxSounds(Int value) {
  nonVirtual.resize(value);
  nonVirtualSize = value;
}

Int YSE::INTERNAL::soundManager::maxSounds() {
  return nonVirtualSize;
}

void YSE::INTERNAL::soundManager::findLeastImportant() {
  // call this only when buffer is completely filled !!!
  leastImportant = 0;
  for (UInt i = 1; i < nonVirtual.size(); i++) {
    if (!(nonVirtual[i]->parent->allowVirtual)) continue;
    if (nonVirtual[i]->virtualDist > nonVirtual[leastImportant]->virtualDist) leastImportant = i;
  }
  if (!nonVirtual[leastImportant]->parent->allowVirtual) leastImportant = -1;
}

YSE::INTERNAL::soundImplementation * YSE::INTERNAL::soundManager::addImplementation() {
  soundObjects.emplace_front();
  return &soundObjects.front();
}

void YSE::INTERNAL::soundManager::removeImplementation(YSE::INTERNAL::soundImplementation * ptr) {
  ptr->_release = true;
  ptr->intent = SS_WANTSTOSTOP;
}

void YSE::INTERNAL::soundManager::adjustLastGainBuffer() {
  const ScopedLock lock(Global.getDeviceManager().getLock());

  for (std::forward_list<soundImplementation>::iterator i = soundObjects.begin(); i != soundObjects.end(); ++i) {
    // if a sound is still loading, it will be adjusted during initialize
    if (i->_loading) continue;

    UInt j = i->lastGain.size(); // need to store previous size for deep resize
    i->lastGain.resize(Global.getChannelManager().getNumberOfOutputs());
    for (; j < i->lastGain.size(); j++) {
      i->lastGain[j].resize(i->buffer->size(), 0.0f);
    }
  }
}