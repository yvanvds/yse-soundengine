/*
  ==============================================================================

    midifileImplementation.cpp
    Created: 12 Jul 2014 7:09:29pm
    Author:  yvan

  ==============================================================================
*/

#include "midifileImplementation.h"

YSE::MIDI::fileImpl::fileImpl(file * head)
: head(head)
, intent(SS_STOPPED)
, hasFile(false)
//, startSample(0)
{

}

YSE::MIDI::fileImpl::~fileImpl() {
  // disconnect all players
  /*for (auto i = readers.begin(); i != readers.end(); i++) {
    (*i)->removeMidiFile(this);
  }*/
}

bool YSE::MIDI::fileImpl::create(const std::string & fileName) {
  assert(!hasFile);

  /*if (!IO().getActive()) {
    File file = File::getCurrentWorkingDirectory().getChildFile(juce::String(fileName));
    if (!file.existsAsFile()) {
      INTERNAL::LogImpl().emit(E_FILE_ERROR, "file not found for " + file.getFullPathName().toStdString());
      return false;
    }
    FileInputStream * midiFileInputStream = file.createInputStream();
    midiFile.readFrom(*midiFileInputStream);
  }
  else {
    if (!INTERNAL::CALLBACK::fileExists(fileName.c_str())) {
      INTERNAL::LogImpl().emit(E_FILE_ERROR, "file not found for " + fileName);
      return false;
    }
    INTERNAL::customFileReader * cfr = new INTERNAL::customFileReader;
    cfr->create(fileName.c_str());
    midiFile.readFrom(*cfr);
  }

  midiFile.convertTimestampTicksToSeconds();
  for (int i = 0; i < midiFile.getNumTracks(); i++) {
    sequence.addSequence(*midiFile.getTrack(i), 0, 0, midiFile.getTrack(i)->getEndTime() + 1);
  }
  sequence.sort();
  sequence.updateMatchedPairs();

  hasFile = true;*/
  return true;
}

void YSE::MIDI::fileImpl::play() {
  assert(hasFile);
  intent = SS_WANTSTOPLAY;
}

void YSE::MIDI::fileImpl::pause() {
  assert(hasFile);
  intent = SS_WANTSTOPAUSE;
}

void YSE::MIDI::fileImpl::stop() {
  assert(hasFile);
  intent = SS_WANTSTOSTOP;
}

/*void YSE::MIDI::fileImpl::connect(synth * player) {
  for (auto i = readers.begin(); i != readers.end(); i++) {
    if (*i == player->pimpl) {
      // this synth is already connected
      assert(false);
      return;
    }
  }
  readers.push_front(player->pimpl);
  player->pimpl->registerMidiFile(this);
}
void YSE::MIDI::fileImpl::disconnect(synth * player) {
  auto previous = readers.before_begin();
  for (auto i = readers.begin(); i != readers.end(); i++) {
    if (*i == player->pimpl) {
      player->pimpl->removeMidiFile(this);
      readers.erase_after(previous);
      return;
    }
    previous++;
  }
  // this synth was not found!
  assert(false);
}

void YSE::MIDI::fileImpl::removeDevice(SYNTH::implementationObject * player) {
  auto previous = readers.before_begin();
  for (auto i = readers.begin(); i != readers.end(); i++) {
    if (*i == player) {
      readers.erase_after(previous);
      return;
    }
    previous++;
  }
}
*/

/*
void YSE::MIDI::fileImpl::getMessages(MidiBuffer & incomingMidi) {
  switch (intent.load()) {
    case SS_PLAYING: break;
    case SS_STOPPED: return;
    case SS_PAUSED: return;
    case SS_WANTSTOPAUSE: {
      for (int i = 1; i < 17; i++) {
        incomingMidi.addEvent(MidiMessage::allNotesOff(i), 0);
      }
      intent = SS_PAUSED;
      return;
                          
    }
    case SS_WANTSTOSTOP: {
      for (int i = 1; i < 17; i++) {
        incomingMidi.addEvent(MidiMessage::allNotesOff(i), 0);
      }
      intent = SS_STOPPED;
      startSample = 0;
      return;
    }
    case SS_WANTSTOPLAY: {
      intent = SS_PLAYING;
      break;
    }
  }
    
  Int currentEvent = sequence.getNextIndexAtTime(startSample / (double)SAMPLERATE);
  Int currentSample = (Int)(sequence.getEventTime(currentEvent) * SAMPLERATE);

  while ((currentSample - startSample) < (Int)STANDARD_BUFFERSIZE && currentEvent < sequence.getNumEvents()) {  
    incomingMidi.addEvent(sequence.getEventPointer(currentEvent)->message, currentSample - startSample);
    currentEvent++;
    currentSample = (Int)(sequence.getEventTime(currentEvent) * SAMPLERATE);
  }

  if (currentEvent > sequence.getNumEvents()) {
    intent = SS_STOPPED;
    startSample = 0;
  }
  else {
    startSample += STANDARD_BUFFERSIZE;
  }
  return;
}*/

void YSE::MIDI::fileImpl::removeInterface() {
  head = nullptr;
}

bool YSE::MIDI::fileImpl::hasInterface() {
  return (head != nullptr);
}