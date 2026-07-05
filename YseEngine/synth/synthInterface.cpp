/*
  ==============================================================================

    synthInterface.cpp
    Public YSE::synth interface — a thin chainable pimpl over the
    implementationObject. See synthInterface.hpp and docs/design/synth_core.md
    §12.

  ==============================================================================
*/

#include "synthInterface.hpp"
#include "../internalHeaders.h"
#include "../music/note.hpp"
#include "synthManager.h"

#include <cmath>

namespace YSE {
  namespace SYNTH {

    interfaceObject::interfaceObject() : pimpl(nullptr) {}

    interfaceObject::~interfaceObject() {
      if (pimpl != nullptr) {
        pimpl->removeInterface();
        pimpl = nullptr;
      }
    }

    interfaceObject& interfaceObject::create() {
      if (pimpl != nullptr) return *this; // idempotent
      pimpl = Manager().addImplementation(this);
      Manager().setup(pimpl);
      return *this;
    }

    interfaceObject& interfaceObject::addVoices(dspVoice& prototype, int numVoices, int channel,
                                                int lowestNote, int highestNote) {
      if (pimpl == nullptr) create();
      pimpl->addVoiceGroup(&prototype, numVoices, channel, lowestNote, highestNote);
      return *this;
    }

    interfaceObject& interfaceObject::noteOn(int channel, int noteNumber, float velocity) {
      if (pimpl == nullptr) return *this;
      messageObject m;
      m.ID = NOTE_ON;
      m.noteOn.channel = channel;
      m.noteOn.note = noteNumber;
      m.noteOn.velocity = velocity;
      pimpl->sendMessage(m);
      return *this;
    }

    interfaceObject& interfaceObject::noteOff(int channel, int noteNumber, float velocity) {
      if (pimpl == nullptr) return *this;
      messageObject m;
      m.ID = NOTE_OFF;
      m.noteOff.channel = channel;
      m.noteOff.note = noteNumber;
      m.noteOff.velocity = velocity;
      pimpl->sendMessage(m);
      return *this;
    }

    interfaceObject& interfaceObject::noteOn(const MUSIC::note& note) {
      return noteOn(note.getChannel(), static_cast<int>(std::lround(note.getPitch())),
                    note.getVolume());
    }

    interfaceObject& interfaceObject::noteOff(const MUSIC::note& note) {
      return noteOff(note.getChannel(), static_cast<int>(std::lround(note.getPitch())),
                     note.getVolume());
    }

    interfaceObject& interfaceObject::allNotesOff(int channel) {
      if (pimpl == nullptr) return *this;
      messageObject m;
      m.ID = ALL_NOTES_OFF;
      m.allOff.channel = channel;
      pimpl->sendMessage(m);
      return *this;
    }

    interfaceObject& interfaceObject::pitchWheel(int channel, float value) {
      if (pimpl == nullptr) return *this;
      messageObject m;
      m.ID = PITCH_WHEEL;
      m.wheel.channel = channel;
      m.wheel.value = value;
      pimpl->sendMessage(m);
      return *this;
    }

    interfaceObject& interfaceObject::controller(int channel, int number, float value) {
      if (pimpl == nullptr) return *this;
      messageObject m;
      m.ID = CONTROLLER;
      m.cc.channel = channel;
      m.cc.number = number;
      m.cc.value = value;
      pimpl->sendMessage(m);
      return *this;
    }

    interfaceObject& interfaceObject::aftertouch(int channel, int noteNumber, float value) {
      if (pimpl == nullptr) return *this;
      messageObject m;
      m.ID = AFTERTOUCH;
      m.touch.channel = channel;
      m.touch.note = noteNumber;
      m.touch.value = value;
      pimpl->sendMessage(m);
      return *this;
    }

    interfaceObject& interfaceObject::sustain(int channel, bool down) {
      if (pimpl == nullptr) return *this;
      messageObject m;
      m.ID = SUSTAIN;
      m.pedal.channel = channel;
      m.pedal.down = down;
      pimpl->sendMessage(m);
      return *this;
    }

    interfaceObject& interfaceObject::sostenuto(int channel, bool down) {
      if (pimpl == nullptr) return *this;
      messageObject m;
      m.ID = SOSTENUTO;
      m.pedal.channel = channel;
      m.pedal.down = down;
      pimpl->sendMessage(m);
      return *this;
    }

    interfaceObject& interfaceObject::softPedal(int channel, bool down) {
      if (pimpl == nullptr) return *this;
      messageObject m;
      m.ID = SOFTPEDAL;
      m.pedal.channel = channel;
      m.pedal.down = down;
      pimpl->sendMessage(m);
      return *this;
    }

    interfaceObject& interfaceObject::onNoteEvent(void (*func)(bool, float*, float*)) {
      // The hook is set directly on the impl via an atomic pointer — it never
      // crosses the message inbox (there is no CALLBACK op). See §7. Safe to
      // call before create(): with no impl there is nothing to drive yet.
      if (pimpl == nullptr) return *this;
      pimpl->setNoteCallback(func);
      return *this;
    }

    int interfaceObject::getNumVoices() const {
      return pimpl != nullptr ? pimpl->getNumVoices() : 0;
    }

    bool interfaceObject::isValid() const {
      return pimpl != nullptr;
    }

  } // namespace SYNTH
} // namespace YSE
