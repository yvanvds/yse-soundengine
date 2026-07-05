/*
  ==============================================================================

    synthImplementation.h
    Audio-thread implementation object for the YSE::synth subsystem.

    Owns the voice groups, the per-impl lock-free note inbox, the aggregate
    output source, the voice allocator (with the #151 stealing policy) and the
    OBJECT_* lifecycle. Implements §2 / §4 / §8 / §9 of
    docs/design/synth_core.md.

    Threading:
      * control thread  — sendMessage() (push inbox), addVoiceGroup() (record
                          a pending group request under buildMutex).
      * setup pool      — setup() clones voices under buildMutex. The ONLY place
                          voices are allocated.
      * audio thread    — renderBlock() (via outputSource::process): drains the
                          inbox, runs the keyboard/allocator, drives every voice
                          and mixes, all allocation-free and lock-free.

  ==============================================================================
*/

#ifndef YSE_SYNTH_SYNTHIMPLEMENTATION_H
#define YSE_SYNTH_SYNTHIMPLEMENTATION_H

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>

#include "../classes.hpp"
#include "../dsp/dspObject.hpp"
#include "../headers/enums.hpp"
#include "../headers/types.hpp"
#include "../utils/lfQueue.hpp"
#include "dspVoice.hpp"
#include "synth.hpp"
#include "synthMessage.h"

namespace YSE {
  namespace INTERNAL {
    // The manager job templates befriended below (defined in managerJobs.hpp).
    template <typename Manager> class managerSetupJob;
    template <typename Manager> class managerDeleteJob;
  } // namespace INTERNAL

  namespace SYNTH {

    /**
        Internal counterpart of a ``YSE::synth``. Created and destroyed by the
        SYNTH::managerObject; the user never touches one directly.
    */
    class implementationObject {
    public:
      explicit implementationObject(interfaceObject* head);
      ~implementationObject();

      // ---- control thread -------------------------------------------------

      /** Push a note/control message onto the SPSC inbox (control thread). */
      void sendMessage(const messageObject& message);

      /** Record a pending voice-group request. The clones themselves are built
          later, on the setup pool, in setup(). Called on the control thread;
          guarded by buildMutex against the setup pool. */
      void addVoiceGroup(dspVoice* prototype, int numVoices, int channel, int lowestNote,
                         int highestNote);

      /** Total allocated voices across every group. Thread-safe. */
      int getNumVoices() const;

      // ---- attachment -----------------------------------------------------

      /** The aggregate source the owning ``YSE::sound`` renders. */
      DSP::dspSourceObject& getOutputSource();

      // ---- lifecycle (manager + job templates) ----------------------------

      /** Atomically claim OBJECT_CREATED -> OBJECT_SETTING_UP for the setup
          pool (used by INTERNAL::managerSetupJob). */
      bool tryClaimForSetup();

      /** Setup-pool entry point: clone the pending voice groups. Publishes
          OBJECT_SETUP on completion. */
      void setup();

      /** Promote OBJECT_SETUP -> OBJECT_READY (audio thread). */
      bool readyCheck();

      /** Audio-thread lifecycle tick: detects interface teardown and flags
          the impl for release. */
      void sync();

      void removeInterface();
      OBJECT_IMPLEMENTATION_STATE getStatus();
      void setStatus(OBJECT_IMPLEMENTATION_STATE value);

      /** Predicate for the delete job's remove_if. */
      static bool canBeDeleted(const implementationObject& impl) {
        return impl.objectStatus.load(std::memory_order_acquire) == OBJECT_DELETE;
      }

      /** Predicate for the audio-thread toLoad scrub: an impl that is already
          ready/released/deleted no longer belongs in the loading list. */
      static bool canBeRemovedFromLoading(const implementationObject* impl) {
        OBJECT_IMPLEMENTATION_STATE s = impl->objectStatus.load(std::memory_order_acquire);
        return s == OBJECT_READY || s == OBJECT_RELEASE || s == OBJECT_DELETE;
      }

    private:
      // ---- aggregate output source ---------------------------------------
      // The single dspSourceObject the owning sound renders. Its process() is
      // the synth's per-block heartbeat; it simply forwards to renderBlock().
      class outputSource : public DSP::dspSourceObject {
      public:
        outputSource(implementationObject& owner, Int channels)
          : dspSourceObject(channels), owner(owner) {}
        void process(SOUND_STATUS& masterIntent) override {
          owner.renderBlock(masterIntent);
        }
        void frequency(Flt) override {} // no-op: notes carry pitch
      private:
        implementationObject& owner;
      };

      // ---- voice bookkeeping ---------------------------------------------
      // Per-voice slot state owned and mutated only on the audio thread.
      struct voiceSlot {
        SOUND_STATUS intent = SS_STOPPED; // this voice's gate (passed to process)
        int note = -1; // MIDI note sounding here (-1 = free)
        int channel = 0; // channel that triggered it
        uint64_t age = 0; // allocation stamp; smaller = older
        // engine-owned declick for click-free stealing
        bool stealing = false; // fading the OLD note out before re-arming
        int stealPos = 0; // samples elapsed into the steal fade
        int pendNote = 0; // the NEW note to arm once the fade completes
        int pendChannel = 0;
        Flt pendVelocity = 0.f;
        uint64_t pendAge = 0;
      };

      // A group is one addVoices() call: n identical voices with a channel
      // filter and note range. Allocation is always within a matching group.
      struct voiceGroup {
        std::vector<std::unique_ptr<dspVoice>> voices;
        std::vector<voiceSlot> slots; // parallel to voices
        int channel = 0; // 0 = omni
        int lowNote = 0;
        int highNote = 127;
      };

      // A not-yet-built group, recorded by addVoiceGroup() and consumed by
      // setup(). Holds a raw prototype pointer the caller must keep alive.
      struct groupRequest {
        dspVoice* prototype;
        int numVoices;
        int channel;
        int lowNote;
        int highNote;
      };

      // ---- audio-thread render path --------------------------------------
      void renderBlock(SOUND_STATUS& masterIntent);
      void parseMessage(const messageObject& message);
      void handleNoteOn(int channel, int note, Flt velocity);
      void handleNoteOff(int channel, int note);
      void handleAllNotesOff(int channel);
      void allocateInGroup(voiceGroup& group, int channel, int note, Flt velocity);
      void freeAllVoices();
      void mixVoice(dspVoice& voice, Flt gain);
      void mixVoiceRamp(dspVoice& voice, voiceSlot& slot);

      static bool groupMatches(const voiceGroup& group, int channel, int note) {
        return (group.channel == 0 || group.channel == channel) && note >= group.lowNote &&
               note <= group.highNote;
      }

      // Intrusive manager-list link (issue #194): threads this impl through the
      // manager's audio-thread toLoad/inUse lists (mutually exclusive).
      implementationObject* _mgrNext = nullptr;

      std::atomic<interfaceObject*> head; // owning interface (null once gone)
      std::atomic<OBJECT_IMPLEMENTATION_STATE> objectStatus;
      lfQueue<messageObject> messages; // SPSC note inbox

      outputSource output; // aggregate source

      // Built on the setup pool, read on the audio thread. Published via
      // objectStatus (release in setup(), acquire in renderBlock()): the audio
      // thread only touches `groups` once status >= OBJECT_SETUP, after which
      // the container is immutable for the impl's lifetime.
      std::vector<voiceGroup> groups;
      std::vector<groupRequest> pendingGroups; // control -> setup pool
      std::mutex buildMutex; // guards pendingGroups / groups build

      std::atomic<int> numVoicesTotal{0};
      uint64_t ageCounter = 0; // audio thread only
      int stealFadeSamples = 1; // computed in setup()

      friend class SYNTH::managerObject;
      friend class INTERNAL::managerSetupJob<managerObject>;
      friend class INTERNAL::managerDeleteJob<managerObject>;
    };

  } // namespace SYNTH
} // namespace YSE

#endif // YSE_SYNTH_SYNTHIMPLEMENTATION_H
