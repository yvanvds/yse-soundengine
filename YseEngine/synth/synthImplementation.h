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

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>

#include "../classes.hpp"
#include "../dsp/dspObject.hpp"
#include "../dsp/panner.hpp"
#include "../headers/enums.hpp"
#include "../headers/types.hpp"
#include "../utils/lfQueue.hpp"
#include "../utils/vector.hpp"
#include "dspVoice.hpp"
#include "positionHandler.hpp"
#include "synth.hpp"
#include "synthMessage.h"

namespace YSE {
  namespace INTERNAL {
    // The manager job templates befriended below (defined in managerJobs.hpp).
    template <typename Manager> class managerSetupJob;
    template <typename Manager> class managerDeleteJob;
  } // namespace INTERNAL

  namespace SYNTH {

    /** Audio-thread note-rewrite hook (§7). A free function / captureless
        lambda that may rewrite note number and velocity in place before the
        keyboard state and allocator see the event. Deliberately a plain
        function pointer so it carries no heap closure the audio thread would
        have to reason about. */
    using NoteCallback = void (*)(bool noteOn, float* noteNumber, float* velocity);

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

      /** Install (or clear, with nullptr) the audio-thread onNoteEvent hook.
          Set directly via an atomic pointer — it never crosses the inbox (§7).
          Called on the control thread; acquire-loaded on the audio thread. */
      void setNoteCallback(NoteCallback func);

      /** Record a pending voice-group request. The clones themselves are built
          later, on the setup pool, in setup(). Called on the control thread;
          guarded by buildMutex against the setup pool. */
      void addVoiceGroup(dspVoice* prototype, int numVoices, int channel, int lowestNote,
                         int highestNote);

      /** Record the position-handler prototype to clone once per voice slot in
          setup() (#170). nullptr clears it. Rejected (with a warning) after
          setup, like addVoiceGroup — the audio thread reads the handlers
          lock-free once built. Control thread; guarded by buildMutex. */
      void setPositionHandler(positionHandler* prototype);

      /** Total allocated voices across every group. Thread-safe. */
      int getNumVoices() const;

      // ---- keyboard-state introspection -----------------------------------
      // Read the last-seen per-channel keyboard values. Intended for tests and
      // future C-API/metering readback; all audio-thread state, read here
      // without synchronisation is a best-effort snapshot. channel is 1..16.
      Flt getChannelPitchWheel(int channel) const;
      Flt getChannelController(int channel, int number) const;
      Flt getChannelAftertouch(int channel) const;
      bool getSustain(int channel) const;
      bool getSostenuto(int channel) const;
      bool getSoftPedal(int channel) const;
      // Current position of a voice sounding (channel, note), or the origin if
      // none is. Best-effort audio-thread snapshot (like the getters above);
      // intended for tests and future C-API/metering readback (#170/#171).
      Pos getVoicePosition(int channel, int note) const;

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
        // ---- keyboard / pedal bookkeeping (§5) ----
        // A sounding voice is released (intent -> SS_WANTSTOSTOP) only once its
        // key is up AND no pedal still claims it. keyDown tracks the physical
        // key; heldBySustain / heldBySostenuto track a pedal deferring release.
        bool keyDown = false; // physical key is down (NOTE_ON..NOTE_OFF)
        bool heldBySustain = false; // sustain pedal is deferring this release
        bool heldBySostenuto = false; // captured + deferred by the sostenuto pedal
        // Position handler edge tracking (#170): set once onRelease() has fired
        // for this note's release, cleared when the slot is (re-)armed. Keeps
        // onRelease a single edge call however many times the slot is scanned.
        bool releaseNotified = false;
        // engine-owned declick for click-free stealing
        bool stealing = false; // fading the OLD note out before re-arming
        int stealPos = 0; // samples elapsed into the steal fade
        int pendNote = 0; // the NEW note to arm once the fade completes
        int pendChannel = 0;
        Flt pendVelocity = 0.f;
        uint64_t pendAge = 0;
        // ---- per-note 3D positioning (issue #169, Route 2) ----
        // The voice's current position, consumed by its panner. With no position
        // handler attached (this epic is infrastructure only — handlers land in
        // #170) it stays at the origin, so every voice spreads at the listener-
        // relative centre. panner spreads this voice's mono output across the
        // device-width aggregate bed; both are built on the setup pool.
        Pos position{0.f};
        DSP::panner panner;
      };

      // A group is one addVoices() call: n identical voices with a channel
      // filter and note range. Allocation is always within a matching group.
      struct voiceGroup {
        std::vector<std::unique_ptr<dspVoice>> voices;
        std::vector<voiceSlot> slots; // parallel to voices
        // Per-slot position handler (#170). Either empty (no handler attached —
        // every voice uses the aggregate position) or exactly one clone per
        // voice slot, parallel to voices/slots. Built on the setup pool.
        std::vector<std::unique_ptr<positionHandler>> handlers;
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

      // ---- per-channel keyboard state (§5) -------------------------------
      // One entry per MIDI channel (1..16). Owned and mutated only on the audio
      // thread (written in parseMessage, read while allocating / delivering).
      // Fixed-size — no allocation on any control path.
      struct channelState {
        bool sustain = false; // CC 64 held
        bool sostenuto = false; // CC 66 held
        bool softPedal = false; // CC 67 held
        Flt pitchWheel = 0.f; // last wheel value, [-1, 1]
        Flt aftertouch = 0.f; // last channel-wide pressure, [0, 1]
        std::array<Flt, 128> controller{}; // last value of each CC, [0, 1]
      };
      static constexpr int kNumChannels = 16;
      // Map a MIDI channel (1..16) to a channels[] index, clamped so a stray
      // channel value can never index out of bounds on the audio thread.
      static int channelIndex(int channel) {
        if (channel < 1) return 0;
        if (channel > kNumChannels) return kNumChannels - 1;
        return channel - 1;
      }
      channelState& channelFor(int channel) {
        return channels[channelIndex(channel)];
      }

      // ---- audio-thread render path --------------------------------------
      // Ensure the aggregate bed and every voice's panner are sized to the
      // current device output width. A no-op in the steady state (one int
      // compare); on the first call and on a device restart it re-sizes, which
      // allocates — the same accepted device-restart exception the engine makes
      // in deviceManager (master->resize(true)). Never allocates at note rate.
      void ensureDeviceWidth();
      void renderBlock(SOUND_STATUS& masterIntent);
      void parseMessage(const messageObject& message);
      void handleNoteOn(int channel, int note, Flt velocity);
      void handleNoteOff(int channel, int note);
      void handleAllNotesOff(int channel);
      // keyboard / controller ops (§5 / §6)
      void handlePitchWheel(int channel, Flt value);
      void handleController(int channel, int number, Flt value);
      void handleAftertouch(int channel, int note, Flt value);
      // per-note positioning ops (#170)
      void handleHandlerParam(int index, Flt value);
      void handleNotePosition(int channel, int note, const Pos& pos);
      // Fill a voiceContext for the handler paired with slot i of `group`.
      // Reads the voice's live note atomics and the channel/handler-param state.
      voiceContext buildContext(voiceGroup& group, size_t i);
      // The handler paired with slot i, or nullptr when no handler is attached.
      static positionHandler* handlerFor(voiceGroup& group, size_t i) {
        return group.handlers.empty() ? nullptr : group.handlers[i].get();
      }
      void handleSustain(int channel, bool down);
      void handleSostenuto(int channel, bool down);
      void handleSoftPedal(int channel, bool down);
      // release a slot only if its key is up and no pedal still claims it
      void releaseIfUnheld(voiceSlot& slot);
      // forward the channel's current wheel value to one voice / all its voices
      void primeVoicePitchWheel(dspVoice& voice, int channel);
      void allocateInGroup(voiceGroup& group, int channel, int note, Flt velocity);
      void freeAllVoices();
      // Fill the per-voice fader scratch: all-ones for a normal voice, or the
      // declining steal ramp for a voice being stolen (issue #169). Consumed by
      // panner.spread() as its per-sample gain envelope.
      void fillUnitFader(int blockLen);
      void fillStealFader(const voiceSlot& slot, int blockLen);

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
      // Position-handler prototype recorded by setPositionHandler(), cloned once
      // per voice slot in setup() (#170). Raw, caller-owned (like a voice
      // prototype); nullptr = no handler attached.
      positionHandler* handlerPrototype = nullptr;
      std::mutex buildMutex; // guards pendingGroups / groups / handlerPrototype

      // Shared handler-param block (#170, §9). Written by HANDLER_PARAM on the
      // audio thread, read by every live handler through voiceContext the same
      // block. Audio thread only — no atomics needed (same-thread access).
      static constexpr int kMaxHandlerParams = 16;
      std::array<Flt, kMaxHandlerParams> handlerParams{};

      std::atomic<int> numVoicesTotal{0};
      uint64_t ageCounter = 0; // audio thread only
      int stealFadeSamples = 1; // computed in setup()
      // Device output width the aggregate bed + voice panners are currently
      // sized for (issue #169). -1 until first sized; compared in
      // ensureDeviceWidth() to detect a device restart. Audio thread + setup.
      int builtForOutputs = -1;
      // Per-voice gain envelope scratch for spread(): filled with 1.0 for a
      // normal voice or the declining steal ramp for a voice being stolen.
      // Sized once (device-restart path); never reallocated at note rate.
      std::vector<Flt> voiceFader;

      // Per-channel keyboard state (§5). Audio thread only.
      std::array<channelState, kNumChannels> channels;

      // onNoteEvent hook (§7): release-store from the control thread,
      // acquire-load on the audio thread; never crosses the message inbox.
      std::atomic<NoteCallback> noteCallback{nullptr};

      friend class SYNTH::managerObject;
      friend class INTERNAL::managerSetupJob<managerObject>;
      friend class INTERNAL::managerDeleteJob<managerObject>;
    };

  } // namespace SYNTH
} // namespace YSE

#endif // YSE_SYNTH_SYNTHIMPLEMENTATION_H
