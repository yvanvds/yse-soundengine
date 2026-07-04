/*
  ==============================================================================

    midifileImplementation.h
    Created: 12 Jul 2014 7:09:29pm
    Author:  yvan

  ==============================================================================
*/

#ifndef MIDIFILEIMPLEMENTATION_H_INCLUDED
#define MIDIFILEIMPLEMENTATION_H_INCLUDED

#include <string>
#include <atomic>
#include "../internalHeaders.h"
// #include "../synth/synthImplementation.h"
#include <forward_list>

namespace YSE {
  namespace MIDI {

    class fileImpl {
    public:
      fileImpl(file* head);
      ~fileImpl();

      bool create(const std::string& fileName);

      void play();
      void pause();
      void stop();

      // void connect   (synth * player);
      // void disconnect(synth * player);

      // void getMessages(MidiBuffer & incomingMidi);

      // this is called only by a synth destructor which still
      // has pointers to this file acive
      // void removeDevice(SYNTH::implementationObject * player);

      // Called by the main thread from ~file(); publishes head==null so the
      // audio thread observes the orphaned impl on its next update() tick.
      void removeInterface();

      // Read by the audio thread in managerObject::update() to detect orphans.
      bool hasInterface();

      OBJECT_IMPLEMENTATION_STATE getStatus() const;
      void setStatus(OBJECT_IMPLEMENTATION_STATE value);

      // Used by the slow-pool managerDeleteJob's remove_if over the canonical
      // `implementations` list. The audio thread promotes an orphaned impl to
      // OBJECT_DELETE only after removing it from its own `inUse` list, so by
      // the time the slow pool can free it nothing on the audio thread still
      // references it.
      static bool canBeDeleted(const fileImpl& impl) {
        return impl.objectStatus.load() == OBJECT_DELETE;
      }

    private:
      // Interface object. Atomic: the main thread nulls it in removeInterface()
      // while the audio thread reads it in hasInterface() (issue #190).
      std::atomic<file*> head;

      // MidiFile midiFile;
      // MidiMessageSequence sequence;

      std::atomic<SOUND_STATUS> intent;

      // Lifecycle state for the audio-thread/slow-pool handoff. Starts
      // OBJECT_READY (there is no async setup stage) and only ever moves to
      // OBJECT_DELETE once the audio thread has retired the impl from `inUse`.
      std::atomic<OBJECT_IMPLEMENTATION_STATE> objectStatus;

      bool hasFile;
      // int startSample;

      // std::forward_list<YSE::SYNTH::implementationObject *> readers;
    };

  } // namespace MIDI
} // namespace YSE

#endif // MIDIFILEIMPLEMENTATION_H_INCLUDED
