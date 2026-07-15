/*
  ==============================================================================

    midifileImplementation.h
    Created: 12 Jul 2014 7:09:29pm
    Author:  yvan

  ==============================================================================
*/

#ifndef MIDIFILEIMPLEMENTATION_H_INCLUDED
#define MIDIFILEIMPLEMENTATION_H_INCLUDED

#include <array>
#include <atomic>
#include <cstdint>
#include <string>
#include <vector>
// Public umbrella header only (enums, types, and the MIDI::file class). NOT
// internalHeaders.h: the manager below now embeds an intrusive list keyed on a
// pointer-to-member of this class, so it needs `fileImpl` complete — pulling in
// the manager headers here would form an include cycle that leaves `fileImpl`
// incomplete at the manager's declaration (issue #266). This mirrors how the
// sound/reverb impl headers stay off internalHeaders.h.
#include "../yse.hpp"
#include "../synth/synth.hpp" // YSE::synth / SYNTH::interfaceObject forward decls

namespace YSE {
  namespace MIDI {

    class fileImpl {
    public:
      fileImpl(file* head);
      ~fileImpl();

      // ---- main thread -----------------------------------------------------

      /** Parse a standard MIDI file into the block-accurate event list. Reads
          the file off the audio thread (main thread). @return true on success. */
      bool create(const std::string& fileName);

      void play();
      void pause();
      void stop();

      /** Wire this file's playback to a synth. Every channel-voice event in the
          file is delivered to ``target``'s inbox as playback advances (§10 of
          docs/design/synth_core.md lists MIDI-file playback as a synth control
          producer). The synth must outlive the connection (disconnect, or
          destroy the file, before destroying the synth). Main-thread only. */
      void connect(SYNTH::interfaceObject* target);

      /** Stop delivering this file's events to ``target``. Main-thread only. */
      void disconnect(SYNTH::interfaceObject* target);

      // ---- audio thread ----------------------------------------------------

      /** Advance playback by ``numSamples`` and push every event that falls in
          this block onto the connected synths' inboxes. Called once per audio
          callback from MIDI::managerObject::updatePlayback(). Allocation-free,
          lock-free — every event is pre-parsed and every push is a non-blocking
          try_push (§11). */
      void advance(int numSamples);

      // ---- lifecycle -------------------------------------------------------

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

      // ---- parsed representation (also read by tests) ----------------------

      /** One decoded channel-voice event at an absolute sample position. Meta,
          tempo and SysEx events are consumed during parsing (tempo feeds the
          tick->sample conversion) and are not stored here. */
      struct fileEvent {
        uint64_t sampleTime; // absolute sample offset from playback start
        unsigned char status; // full status byte, incl. channel nibble
        unsigned char data1;
        unsigned char data2;
      };

      /** The decoded, time-sorted event list. Built by create() on the main
          thread; immutable during playback. Exposed for the parser tests. */
      const std::vector<fileEvent>& events() const {
        return midiEvents;
      }

    private:
      // Intrusive forward-list link (issue #266, mirrors #194). Threads this
      // impl through the MIDI file manager's audio-thread `inUse` list. Touched
      // only on the audio thread, replacing the std::forward_list<T*> node so
      // MIDI-file start/stop no longer churns a heap node per tick. MIDI files
      // skip the toLoad stage, so a single link suffices.
      fileImpl* _mgrNext = nullptr;

      // Deliver one decoded event to every connected synth (audio thread).
      void dispatchEvent(const fileEvent& event);
      // Release every held note on every connected synth (pause / stop / EOT).
      void allNotesOffToSynths();

      // Interface object. Atomic: the main thread nulls it in removeInterface()
      // while the audio thread reads it in hasInterface() (issue #190).
      std::atomic<file*> head;

      std::atomic<SOUND_STATUS> intent;

      // Lifecycle state for the audio-thread/slow-pool handoff. Starts
      // OBJECT_READY (there is no async setup stage) and only ever moves to
      // OBJECT_DELETE once the audio thread has retired the impl from `inUse`.
      std::atomic<OBJECT_IMPLEMENTATION_STATE> objectStatus;

      bool hasFile;

      // Decoded events, sorted by sampleTime. Written only by create() (main
      // thread, before play()); read by advance() (audio thread). The intent
      // release/acquire on play() publishes it to the audio thread.
      std::vector<fileEvent> midiEvents;

      // Playback cursor — audio-thread only (seeded by create() before play()).
      uint64_t playhead = 0; // absolute sample position
      std::size_t nextEvent = 0; // index of the next event to fire

      // Connected synths (§9 caller-owned lifetime). A fixed-size atomic table
      // so connect/disconnect (main thread) never lock and advance() (audio
      // thread) never allocates. 0 = all channels; the file carries channel in
      // each event's status byte, so no per-connection channel filter is needed.
      static constexpr std::size_t kMaxSynths = 8;
      std::array<std::atomic<SYNTH::interfaceObject*>, kMaxSynths> synths;

      // Grants the manager access to the intrusive `_mgrNext` link (issue #266).
      friend class managerObject;
    };

  } // namespace MIDI
} // namespace YSE

#endif // MIDIFILEIMPLEMENTATION_H_INCLUDED
