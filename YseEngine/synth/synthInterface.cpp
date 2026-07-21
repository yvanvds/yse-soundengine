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
#include "positionHandler.hpp"
#include "synthManager.h"

#include <cmath>
#include <mutex>
#include <string>
#include <unordered_set>
#include <variant>
#include <vector>

namespace {
  // Process-wide registry of synth names currently claimed as bus producers
  // (issue #388). The "synth." prefix lives in its own namespace, independent
  // of the sound / channel / patcher name registries. Naming happens on the
  // control thread; the mutex is a cheap defensive guard against construction
  // from multiple threads and never sits on the audio path.
  std::mutex& synthNameMutex() {
    static std::mutex m;
    return m;
  }

  std::unordered_set<std::string>& synthNames() {
    static std::unordered_set<std::string> names;
    return names;
  }

  // Returns true when the name was free and is now claimed by the caller.
  bool claimSynthName(const std::string& name) {
    std::lock_guard<std::mutex> lock(synthNameMutex());
    return synthNames().insert(name).second;
  }

  void releaseSynthName(const std::string& name) {
    std::lock_guard<std::mutex> lock(synthNameMutex());
    synthNames().erase(name);
  }

  // Channel / note / controller numbers arrive as float — the bus's only
  // sequence type is list[float] — and are rounded to the int the synth API
  // expects (same rounding the MUSIC::note overloads use).
  int toInt(float v) {
    return static_cast<int>(std::lround(v));
  }
} // namespace

namespace YSE {
  namespace SYNTH {

    interfaceObject::interfaceObject() : pimpl(nullptr) {}

    interfaceObject::~interfaceObject() {
      unregisterFromBus();
      if (pimpl != nullptr) {
        pimpl->removeInterface();
        pimpl = nullptr;
      }
    }

    interfaceObject& interfaceObject::name(const std::string& n) {
      if (n == _name) return *this;
      unregisterFromBus();
      _name = n;
      registerOnBus();
      return *this;
    }

    void interfaceObject::registerOnBus() {
      if (_name.empty()) return;
      // No bus before System::init() or after System::close(). Naming while
      // the engine is down is a silent no-op (mirrors the sound contract).
      if (!INTERNAL::Global().isActive()) return;

      if (!claimSynthName(_name)) {
        INTERNAL::LogImpl().emit(E_FILE_ERROR,
                                 "synth name '" + _name +
                                     "' is already in use; bus registration rejected");
        return;
      }
      _busOwner = true;

      using YSE::INTERNAL::Bus;
      using YSE::INTERNAL::BusValue;
      const std::string base = "synth." + _name + ".";

      // The callbacks fire on the control thread (synchronous T_GUI publish
      // or the drainPending() tick) and reuse the public setters, which
      // enqueue into the implementation's RT-safe message inbox — exactly
      // what the C++ calls do, so no new audio-thread surface is opened. The
      // setters already no-op without an implementation, so events arriving
      // before create() — or after release — are dropped safely. Payloads of
      // the wrong shape are ignored per the DSL spec (fire-and-forget); the
      // shape contract lives in docs/design/live_coding_dsl.md.
      _busHandles[0] = Bus().subscribe(base + "note", [this](const BusValue& v) {
        if (auto* vec = std::get_if<std::vector<float>>(&v)) {
          if (vec->size() == 3) noteOn(toInt((*vec)[0]), toInt((*vec)[1]), (*vec)[2]);
        }
      });
      _busHandles[1] = Bus().subscribe(base + "off", [this](const BusValue& v) {
        if (auto* vec = std::get_if<std::vector<float>>(&v)) {
          if (vec->size() == 2)
            noteOff(toInt((*vec)[0]), toInt((*vec)[1]));
          else if (vec->size() == 3)
            noteOff(toInt((*vec)[0]), toInt((*vec)[1]), (*vec)[2]);
        }
      });
      _busHandles[2] = Bus().subscribe(base + "cc", [this](const BusValue& v) {
        if (auto* vec = std::get_if<std::vector<float>>(&v)) {
          if (vec->size() == 3) controller(toInt((*vec)[0]), toInt((*vec)[1]), (*vec)[2]);
        }
      });
      _busHandles[3] = Bus().subscribe(base + "bend", [this](const BusValue& v) {
        if (auto* vec = std::get_if<std::vector<float>>(&v)) {
          if (vec->size() == 2) pitchWheel(toInt((*vec)[0]), (*vec)[1]);
        }
      });
      _busHandles[4] = Bus().subscribe(base + "aftertouch", [this](const BusValue& v) {
        if (auto* vec = std::get_if<std::vector<float>>(&v)) {
          if (vec->size() == 3) aftertouch(toInt((*vec)[0]), toInt((*vec)[1]), (*vec)[2]);
        }
      });
      _busHandles[5] = Bus().subscribe(base + "alloff", [this](const BusValue& v) {
        if (auto* i = std::get_if<int>(&v)) {
          allNotesOff(*i);
        } else if (auto* f = std::get_if<float>(&v)) {
          allNotesOff(toInt(*f));
        } else if (std::holds_alternative<std::monostate>(v)) {
          allNotesOff(0); // bang (e.g. a patcher gSend) = release all channels
        }
      });
    }

    void interfaceObject::unregisterFromBus() {
      if (!_busOwner) return;
      // Guard the bus access: a synth destructed after System::close() must
      // not touch the torn-down bus. The name registry is independent of the
      // bus, so releasing the name is always safe.
      if (INTERNAL::Global().isActive()) {
        for (auto& handle : _busHandles) {
          if (handle != 0) INTERNAL::Bus().unsubscribe(handle);
        }
      }
      for (auto& handle : _busHandles)
        handle = 0;
      releaseSynthName(_name);
      _busOwner = false;
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

    interfaceObject& interfaceObject::positionHandler(YSE::SYNTH::positionHandler& prototype) {
      // Records the prototype under buildMutex; cloned per voice slot in setup().
      // Like addVoices/onNoteEvent, harmless before create() has an impl.
      if (pimpl == nullptr) create();
      pimpl->setPositionHandler(&prototype);
      return *this;
    }

    interfaceObject& interfaceObject::handlerParam(int index, float value) {
      if (pimpl == nullptr) return *this;
      messageObject m;
      m.ID = HANDLER_PARAM;
      m.handlerParam.index = index;
      m.handlerParam.value = value;
      pimpl->sendMessage(m);
      return *this;
    }

    interfaceObject& interfaceObject::notePosition(int channel, int noteNumber, const Pos& pos) {
      if (pimpl == nullptr) return *this;
      messageObject m;
      m.ID = NOTE_POSITION;
      m.notePosition.channel = channel;
      m.notePosition.note = noteNumber;
      m.notePosition.x = pos.x;
      m.notePosition.y = pos.y;
      m.notePosition.z = pos.z;
      pimpl->sendMessage(m);
      return *this;
    }

    Pos interfaceObject::getVoicePosition(int channel, int noteNumber) const {
      return pimpl != nullptr ? pimpl->getVoicePosition(channel, noteNumber) : Pos(0.f);
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
