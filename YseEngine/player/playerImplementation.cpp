/*
  ==============================================================================

    player.cpp
    Created: 8 Apr 2015 5:16:24pm
    Author:  yvan

  ==============================================================================
*/

#include "player.hpp"
#include "../internalHeaders.h"
#include "../music/note.hpp"
// #include "../synth/synthInterface.hpp"

YSE::PLAYER::implementationObject::implementationObject(player* head)
  : head(head), waitTime(0.f), activeVoices(1), playing(false) {
  minimumPitch.set(0.f);
  maximumPitch.set(128.f);
  minimumVelocity.set(0.f);
  maximumVelocity.set(1.f);
  minGap.set(0.f);
  maxGap.set(1.f);
  minLength.set(0.1f);
  maxLength.set(1.f);
  numVoices.set(1.f);

  // Preallocate every audio-thread pool up to its fixed ceiling so the note
  // generator in update() never allocates (issue #195). All the voices exist
  // up front; activeVoices selects how many are live. Each voice's motif buffer
  // and the shared note / motif pools are reserved once here.
  notes.reserve(MAX_NOTES);
  motifs.reserve(MAX_MOTIFS);
  voices.reserve(MAX_VOICES);
  for (UInt i = 0; i < MAX_VOICES; i++) {
    voices.emplace_back(false);
    voices.back().motif.reserve(MAX_MOTIF_NOTES);
  }
  voices[0].isActive = true;

  if (head != nullptr) head->pimpl = this;
}

YSE::PLAYER::implementationObject::~implementationObject() {
  if (head.load() != nullptr) {
    head.load()->pimpl = nullptr;
  }
}

bool YSE::PLAYER::implementationObject::update(Flt delta) {
  if (head.load() == nullptr) return false;

  // instrument is not connected
  // if (!instrument) return true;

  // parse messages
  messageObject message;
  while (messages.try_pop(message)) {
    parseMessage(message);
  }

  // update notes and delete on note off. Swap-and-pop keeps this allocation-free
  // (pop_back never releases the reserved buffer) on the audio thread (#195).
  {
    UInt i = 0;
    while (i < notes.size()) {
      if (!notes[i].update(delta)) {
        // instrument->noteOff(notes[i]);
        notes[i] = notes.back();
        notes.pop_back();
      } else
        i++;
    }
  }

  // update all modifiers
  minimumPitch.update(delta);
  maximumPitch.update(delta);
  minimumVelocity.update(delta);
  maximumVelocity.update(delta);
  minGap.update(delta);
  maxGap.update(delta);
  minLength.update(delta);
  maxLength.update(delta);
  numVoices.update(delta);
  partialMotif.update(delta);
  playMotif.update(delta);
  motifToScale.update(delta);
  scale.update(delta);

  // don't play new notes if we're stopped
  if (!playing) return true;

  // adjust number of active voices if needed. All MAX_VOICES voices are
  // preallocated, so growing just flips isActive — never allocates — and the
  // fixed polyphony ceiling caps activeVoices at voices.size() (#195).
  while ((UInt)numVoices() > activeVoices && activeVoices < voices.size()) {
    voices[activeVoices].isActive = true;
    activeVoices++;
  }

  while ((UInt)numVoices() < activeVoices && activeVoices > 0) {
    voices[activeVoices - 1].isActive = false;
    activeVoices--;
  }

  // evaluate voices state
  for (UInt i = 0; i < voices.size(); i++) {

    // check motif if it's playing
    if (voices[i].motifPlaying) {
      setVoiceFromMotif(voices[i], delta);
    }

    else if (voices[i].isActive) {
      voices[i].duration -= delta;
      if (voices[i].duration <= 0) {
        // first check if a motif should be played
        if (motifs.size() && !voices[i].motifPlaying && RandomF() < playMotif()) {
          voices[i].motifPlaying = true;
          setVoiceFromMotif(voices[i], delta);
        }
        // else insert rest if desired
        else if (voices[i].notePlaying && maxGap() > 0) {
          voices[i].duration = RandomF(minGap(), maxGap());
          voices[i].notePlaying = false;
        }
        // else play a random note
        else {
          voices[i].duration = RandomF(minLength(), maxLength());
          voices[i].notePlaying = true;
          Flt pitch = static_cast<Flt>(
              Random(static_cast<Int>(minimumPitch()), static_cast<Int>(maximumPitch())));
          if (scale.isSet()) pitch = scale()->getNearest(pitch);
          // Drop the note rather than grow the pool if the note ceiling is hit,
          // keeping generation allocation-free on the audio thread (#195).
          if (notes.size() < notes.capacity()) {
            notes.emplace_back(pitch, RandomF(minimumVelocity(), maximumVelocity()),
                               voices[i].duration);
            // instrument->noteOn(notes.back());
          }
        }
      }
    }
  }

  return true;
}

void YSE::PLAYER::implementationObject::setVoiceFromMotif(voice& v, Flt delta) {
  // pick a new motif if needed
  if (v.motif.empty()) {
    // TODO is this really the best way to select a motif on weight?
    UInt totalWeight = 0;
    FOREACH(motifs) {
      totalWeight += motifs[i].weight;
    }

    UInt weight = Random(totalWeight);
    MOTIF::implementationObject* m = motifs[0].motif;

    totalWeight = 0;
    FOREACH(motifs) {
      totalWeight += motifs[i].weight;
      if (weight < totalWeight) {
        m = motifs[i].motif;
        break;
      }
    }

    if (RandomF() < partialMotif()) {
      // copy just a part from the motif
      UInt start = Random(m->size() - 1);
      UInt count = 1 + Random(m->size() - start - 1);
      UInt end = start + count;
      // Never copy past the voice's reserved motif buffer (#195).
      if (end - start > v.motif.capacity()) end = start + static_cast<UInt>(v.motif.capacity());

      Flt offset = m->getNote(start).getPosition();
      for (; start < end; start++) {
        v.motif.emplace_back(m->getNote(start));
        v.motif.back().setPosition(v.motif.back().getPosition() - offset);
      }

      v.duration = v.motif.back().getPosition() + v.motif.back().getLength();
    } else {
      UInt count = m->size();
      // Never copy past the voice's reserved motif buffer (#195).
      if (count > v.motif.capacity()) count = static_cast<UInt>(v.motif.capacity());
      for (UInt i = 0; i < count; i++) {
        v.motif.emplace_back(m->getNote(i));
      }
      v.duration = m->getLength();
    }

    // transpose within range
    Flt transposition;
    if (m->getValidPitches() != nullptr && m->getValidPitches()->size()) {
      Flt pitch = static_cast<Flt>(m->getValidPitches()->getNearest(static_cast<Flt>(
          (Random(static_cast<Int>(minimumPitch()), static_cast<Int>(maximumPitch()))))));
      transposition = pitch - v.motif[0].getPitch();
    } else {
      transposition =
          static_cast<Flt>(Random(static_cast<Int>(minimumPitch() - v.motif[0].getPitch()),
                                  static_cast<Int>(maximumPitch() - v.motif[0].getPitch())));
    }
    FOREACH(v.motif) {
      v.motif[i].setPitch(v.motif[i].getPitch() + transposition);
    }

    // set volume
    v.motifVolume = RandomF(minimumVelocity(), maximumVelocity());

    v.motifPos = 0;
    v.motifTime = 0;
  }

  v.motifTime += delta;

  while (v.motifPos < v.motif.size() && v.motif[v.motifPos].getPosition() < v.motifTime) {
    MUSIC::note note;
    note = v.motif[v.motifPos];
    // TODO implement other channels later
    note.setChannel(1);
    note.setVolume(note.getVolume() * v.motifVolume);
    if (RandomF() < motifToScale()) {
      note.setPitch(scale()->getNearest(note.getPitch()));
    }
    // Drop rather than grow the pool if the note ceiling is hit (#195).
    if (notes.size() < notes.capacity()) {
      notes.push_back(note);
      // instrument->noteOn(note);
    }
    v.motifPos++;
  }

  v.motifTime += delta;
  if (v.motifTime >= v.duration) {
    v.motif.clear();
    v.motifPlaying = false;
    v.duration = 0;
  }
}

void YSE::PLAYER::implementationObject::removeInterface() {
  /*if (instrument != nullptr) {
    std::deque<MUSIC::note>::iterator i = notes.begin();
    while (i != notes.end()) {
      instrument->noteOff(*i);
      ++i;
    }
  }*/
  head.store(nullptr);
}

void YSE::PLAYER::implementationObject::parseMessage(const messageObject& message) {
  switch (message.ID) {
  case PLAY:
    playing = message.boolValue;
    break;
  case MIN_PITCH:
    minimumPitch.set(message.floatPair[0], message.floatPair[1]);
    break;
  case MAX_PITCH:
    maximumPitch.set(message.floatPair[0], message.floatPair[1]);
    break;
  case MIN_VELOCITY:
    minimumVelocity.set(message.floatPair[0], message.floatPair[1]);
    break;
  case MAX_VELOCITY:
    maximumVelocity.set(message.floatPair[0], message.floatPair[1]);
    break;
  case MIN_GAP:
    minGap.set(message.floatPair[0], message.floatPair[1]);
    break;
  case MAX_GAP:
    maxGap.set(message.floatPair[0], message.floatPair[1]);
    break;
  case MIN_LENGTH:
    minLength.set(message.floatPair[0], message.floatPair[1]);
    break;
  case MAX_LENGTH:
    maxLength.set(message.floatPair[0], message.floatPair[1]);
    break;
  case VOICES:
    numVoices.set(message.floatPair[0], message.floatPair[1]);
    break;
  case SCALE:
    scale.set((SCALE::implementationObject*)message.object.ptr, message.object.time);
    break;
  case ADD_MOTIF:
    wMotif m;
    m.motif = (MOTIF::implementationObject*)message.object.ptr;
    m.weight = static_cast<UInt>(message.object.time);
    FOREACH(motifs) {
      // no doubles!
      if (motifs[i].motif == m.motif) assert(false);
    }
    // Bounded by MAX_MOTIFS so the motif pool never reallocates on the audio
    // thread; extra motifs past the ceiling are ignored (#195).
    if (motifs.size() < motifs.capacity()) motifs.emplace_back(m);
    break;
  case REM_MOTIF:
    FOREACH(motifs) {
      if (motifs[i].motif == (MOTIF::implementationObject*)message.object.ptr) {
        motifs.erase(motifs.begin() + i);
        break;
      }
    }
    break;
  case ADJUST_MOTIF:
    FOREACH(motifs) {
      if (motifs[i].motif == (MOTIF::implementationObject*)message.object.ptr) {
        motifs[i].weight = (UInt)message.object.time;
        break;
      }
    }
    break;
  case PARTIAL_MOTIF:
    partialMotif.set(message.floatPair[0], message.floatPair[1]);
    break;
  case PLAY_MOTIF:
    playMotif.set(message.floatPair[0], message.floatPair[1]);
    break;
  case MOTIF_FITS_SCALE:
    motifToScale.set(message.floatPair[0], message.floatPair[1]);
    break;
  }
}