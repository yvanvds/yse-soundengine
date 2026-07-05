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

    int interfaceObject::getNumVoices() const {
      return pimpl != nullptr ? pimpl->getNumVoices() : 0;
    }

    bool interfaceObject::isValid() const {
      return pimpl != nullptr;
    }

  } // namespace SYNTH
} // namespace YSE
