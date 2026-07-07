/*
  ==============================================================================

  channelImplementation.h
  Created: 30 Jan 2014 4:21:26pm
  Author:  yvan

  ==============================================================================
*/

#ifndef CHANNELIMPLEMENTATION_H_INCLUDED
#define CHANNELIMPLEMENTATION_H_INCLUDED

#include <atomic>
#include "../classes.hpp"
#include "../utils/lfQueue.hpp"
#include "../utils/intrusiveForwardList.hpp"
#include "../internal/threadPool.h"
// The `sounds` intrusive list forms a pointer-to-member into
// SOUND::implementationObject, which requires the complete type here (a bare
// forward declaration sufficed for the old std::forward_list<T*>). This is an
// acyclic dependency: soundImplementation.h refers to CHANNEL only through the
// forward declaration in classes.hpp. (issue #194)
#include "../sound/soundImplementation.h"

namespace YSE {
  namespace CHANNEL {
    class output;

    // Copy-constructible wrapper so a vector of atomic floats can be resized
    // alongside `out` when the device's channel count changes.
    struct atomicPeak {
      std::atomic<float> v{0.f};
      atomicPeak() = default;
      atomicPeak(const atomicPeak& o) : v(o.v.load(std::memory_order_relaxed)) {}
      atomicPeak& operator=(const atomicPeak& o) {
        v.store(o.v.load(std::memory_order_relaxed), std::memory_order_relaxed);
        return *this;
      }
    };

    // One aux-send slot on a channel (issue #165; design
    // docs/design/send_return_buses.md §6). A channel owns a fixed vector of
    // these, sized once at setup() on the slow pool and never resized on the
    // render path, so `&sends[i]` is a stable node address. A slot is active
    // when `target != nullptr`; the send then accumulates a ramped, scaled copy
    // of the channel's `out` into `target->out` during the serial summation
    // walk. `regNext` is the intrusive back-reference link: the slot is threaded
    // into `target`'s sendRegistry so a return teardown can sever every slot
    // pointing at it before the impl is freed (the many-to-one generalization of
    // the insert chain's `calledfrom` guard). All fields are touched only on the
    // audio thread.
    struct sendSlot {
      implementationObject* target = nullptr; // a return, or nullptr = empty
      Flt newLevel = 0.f; // control-thread target level (via SEND_LEVEL)
      Flt lastLevel = 0.f; // audio-thread ramp state
      Bool preFader = false; // tap point for this slot
      sendSlot* regNext = nullptr; // next slot in target's back-reference registry
    };

    /**
      This is the implementation side of a channel. It should only be used internally.
    */
    class implementationObject : public INTERNAL::threadPoolJob {
    public:
      //////////////////////////////////////////////////
      // Setup and maintenance functions
      //////////////////////////////////////////////////

      /**
        Creates a channel implementation.

        @param head   A pointer to the interface of this channel.
      */
      implementationObject(channel* head);

      /**
      Removes the implementation from the threadpool and moves all sounds and subchannels
      to its parent (if there is one).
      */
      ~implementationObject() noexcept;

      /** This function is called from channelManager::setup and creates the buffers
      needed for this channel.
      */
      void setup();

      /** This function resizes some containers to the number of output channels used
        by the current device.

        @param deep If true, children will also be resized

      */
      void resize(bool deep = false);

      /** Precompute the per-speaker density-compensation weights
          (output::effective) from the current speaker geometry.

          effective[i] = Σ_j computeSpeakerOverlap(angle_i, angle_j) over every
          non-LFE output j. It depends only on the speaker angles / LFE flags,
          never on the source or the audio block, so it is computed once on a
          layout change (setup()/resize(), off the audio thread) rather than
          being recomputed per source channel per block inside toChannels() —
          the term is the entire O(N^2)-transcendental cost of the panner and is
          a no-op on symmetric layouts. LFE outputs are skipped in both the i and
          j sums, matching toChannels() (issue #203); their effective is left
          untouched and never read. Kept as a pure static so the weighting stays
          unit-testable in isolation. See issue #211.
      */
      static void computeEffectiveSpeakerWeights(std::vector<output>& outConf);

      /** This function is called by channelManager::update (from dsp callback) and verifies
      if the channel is ready to be played. It will then be moved from toCreate
      to inUse.
      */
      Bool readyCheck();

      /** Will move all current subchannels and sounds to the parent channel.
      This function is called from channelManager::update(), when objectStatus
      is CIS_RELEASE. It is called again from the destructor, just in case, but
      children should already be moved by then.
      */
      void childrenToParent();

      void removeInterface();
      void doThisWhenReady();

      OBJECT_IMPLEMENTATION_STATE getStatus();
      void setStatus(OBJECT_IMPLEMENTATION_STATE value);

      //////////////////////////////////////////////////////
      // Message system functions
      //////////////////////////////////////////////////////
      /**
      This function will be called when the system is instructed to do an update. It looks
      for messages in the interface message pipe and forwards them to the parseMessage
      function.
      */
      void sync();
      void parseMessage(const messageObject& message); // < Parse a message, called by sync

      /**
        Called by an interface object to send a message to the implementation.
      */
      inline void sendMessage(const messageObject& message) {
        messages.push(message);
      }

      //////////////////////////////////////////////////////
      // Connectors
      //////////////////////////////////////////////////////
      /**
        Attach a subchannel to this channel.
        @param subChannel   A pointer to the channel that should be linked
                  to this channel.
      */
      Bool connect(CHANNEL::implementationObject* channel);

      /**
        Attach a sound to this channel.
        @param sound      A pointer to the sound that should be linked
                to this channel.
      */
      Bool connect(SOUND::implementationObject* sound);

      /**
        Remove a subchannel from this channel.
        @param channel    A pointer to the channel to disconnect
      */
      Bool disconnect(CHANNEL::implementationObject* channel);

      /**
        Remove a sound from this channel.
        @param sound      A pointer to the sound to disconnect
      */
      Bool disconnect(SOUND::implementationObject* sound);

      /////////////////////////////////////////////////////
      // DSP calculations
      /////////////////////////////////////////////////////
      /**
        This is the threadpool function that calls the dsp for this channel.
        Every channel has its own threadPoolJob for running the dsp calculations.
        This will scale all sounds nicely over several cpu's as long as you don't
        put them all in one channel.
      */
      virtual void run();

      /**
        This is the one that does all the work. It allso calls the dsp function
        of all child channels and of all sounds. Effects are also calculated
        here, but applied in the buffersToParent function.
      */
      void dsp();

      /**
        Waits until the dsp job is done and recursively calls this function for
        all subchannels. If this is not the Master channel, this will copy the
        current buffers to the parent channel.
      */
      void buffersToParent();

      /** dsp utility function to set all output buffers to zero before filling again
       */
      void clearBuffers();

      /** Creates a ramp to the new volume if needed, to avoid pops. Called by the dsp function.
       */
      void adjustVolume();

      /**
        Attach the premade special effect for 'underwater' simulation to this channel
      */

      void attachUnderWaterFX();

      inline std::vector<DSP::buffer>& GetBuffers() {
        return out;
      }

      /**
        Attach (or replace/detach) the pre-fader insert DSP chain for this
        channel. Mirrors SOUND::implementationObject::addDSP: the previously
        attached head's back-reference (calledfrom) is cleared before the swap
        so a later destruction of the old plugin can't write through a stale
        pointer into this impl's insert_dsp field (the sound-path UAF fixed in
        #298). Passing nullptr detaches the chain. Runs on the audio thread via
        parseMessage(ATTACH_DSP) — pointer-swap only, no allocation or locking.
      */
      void addDSP(DSP::dspObject* ptr);

      /**
        Run the attached insert chain over `out` in place. Called from dsp()
        pre-fader (before reverb); public so the pre-fader insert behaviour can
        be driven directly in tests after populating `out`.
      */
      void processInsertDSP();

      /////////////////////////////////////////////////////
      // Send / return buses (issue #165)
      /////////////////////////////////////////////////////
      /**
        Accumulate a ramped, scaled copy of this channel's `out` into every
        active send slot whose tap point matches @p preFaderPhase. Runs on the
        audio callback thread only, from the serial summation walk
        (buffersToParent) for source channels and from finalizeReturn() for
        returns — never on a parallel worker. The ramp is fused into the
        multiply-accumulate exactly like adjustVolume(), so live send-level moves
        are click-free. Public so tests can drive it after populating `out`.
      */
      void runSendTaps(bool preFaderPhase);

      /**
        The fast-pool job body for a return bus: runs the return's own DSP
        (insert chain + optional reverb / underwater FX) in place over the
        already-accumulated `out`, then publishes the pre-fader peak. Mirrors the
        tail of dsp(); dispatched from CHANNEL::Manager::processReturns() exactly
        like a source channel's dsp(). Single-writer over this return's own `out`.
      */
      void processReturnInsert();

      /**
        Serial finalize step for a return, run on the audio thread after its
        processReturnInsert() job is joined: applies the return fader, publishes
        the post-fader peak, runs the return's own send taps (targets are always
        a strictly higher generation), then folds the return into @p master. See
        design §7.
      */
      void finalizeReturn(implementationObject* master);

      /**
        Audio-thread teardown of every send this impl participates in, run at
        OBJECT_RELEASE before the impl becomes eligible for the slow-pool delete.
        Unlinks this channel's own active slots from their targets' registries,
        and — if this is a return — severs every slot pointing at it and unlinks
        it from the manager's returns list. Guarantees no live send slot
        dereferences a freed return (design §9).
      */
      void detachSends();

      /** @brief True when this channel is a send/return bus (issue #165). */
      bool getIsReturn() const {
        return isReturn;
      }

      /////////////////////////////////////////////////////
      // Output peak metering (control-thread readers)
      /////////////////////////////////////////////////////
      /** @brief Number of output channels currently allocated for this implementation. */
      int getNumOutputs() const;

      /** @brief Latest pre-volume peak for the given output. Returns 0 when idx is out of range. */
      float getPeakLinearPre(int outputIdx) const;

      /** @brief Latest post-volume peak for the given output. Returns 0 when idx is out of range.
       */
      float getPeakLinearPost(int outputIdx) const;

      /** @brief Latest pre-volume peak combined across all outputs (max). */
      float getPeakLinearPreCombined() const;

      /** @brief Latest post-volume peak combined across all outputs (max). */
      float getPeakLinearPostCombined() const;

      /////////////////////////////////////////////////////
      // static functions for remove_if
      /////////////////////////////////////////////////////
      /**
      This function is used by the forward_list remove_if function
      */
      static bool canBeDeleted(const implementationObject& impl) {
        return impl.objectStatus == OBJECT_DELETE;
      }

      /**
      This function is used by the forward_list remove_if function on the
      audio-thread-owned `toLoad` list. OBJECT_SETTING_UP is treated as
      "keep" so an impl currently being prepared by the slow-pool stays in
      `toLoad` until setup completes.
      */
      static bool canBeRemovedFromLoading(const implementationObject* impl) {
        if (impl->objectStatus == OBJECT_READY || impl->objectStatus == OBJECT_RELEASE ||
            impl->objectStatus == OBJECT_DELETE) {
          return true;
        }

        return false;
      }

      /** Atomically claim this impl for setup. Returns true if the caller now
      owns the right to run setup() on it. Used by the slow-pool's setupJob. */
      bool tryClaimForSetup() {
        OBJECT_IMPLEMENTATION_STATE expected = OBJECT_CREATED;
        return objectStatus.compare_exchange_strong(expected, OBJECT_SETTING_UP);
      }

    private:
      // Intrusive forward-list links (issue #194). `_mgrNext` threads this impl
      // through the CHANNEL manager's audio-thread toLoad/inUse lists (mutually
      // exclusive membership). `_childNext` threads it through its parent
      // channel's `children` list. Both are touched only on the audio thread,
      // replacing the std::forward_list<T*> nodes without per-tick heap churn.
      implementationObject* _mgrNext = nullptr;
      implementationObject* _childNext = nullptr;
      // Threads a return bus through the CHANNEL manager's audio-thread `returns`
      // list (issue #165). Only returns use it; a return is in `returns` (render
      // phase) in addition to `inUse` (lifecycle/sync), so it needs a link
      // distinct from `_mgrNext`. nullptr for ordinary channels.
      implementationObject* _returnNext = nullptr;

      std::atomic<channel*> head; // < The interface connected to this object
      std::atomic<OBJECT_IMPLEMENTATION_STATE> objectStatus; // < the status of this object
      lfQueue<messageObject> messages;

      Flt newVolume;
      Flt lastVolume;

      CHANNEL::implementationObject* parent;
      // True once the audio thread has called parent->connect(this) (i.e.
      // setMaster or a user-created subchannel made it into parent->children).
      // Cleared by the audio thread in CHANNEL::Manager::update before
      // OBJECT_DELETE, so the slow-pool destructor skips parent->children
      // and parent->sounds traversal.
      std::atomic<bool> connectedToParent{false};

      // Intrusive lists keyed on the per-impl link fields — zero heap churn on
      // the audio-thread connect()/disconnect() paths (issue #194).
      IntrusiveForwardList<CHANNEL::implementationObject,
                           &CHANNEL::implementationObject::_childNext>
          children;
      IntrusiveForwardList<SOUND::implementationObject, &SOUND::implementationObject::_channelNext>
          sounds;

      std::vector<output> outConf;
      std::vector<DSP::buffer> out;

      // Head of the pre-fader insert DSP chain, or nullptr when none is
      // attached. Owned by the caller (the interface's dspObject), not by this
      // impl. Swapped only on the audio thread in addDSP(); the plugin's
      // `calledfrom` back-reference points here so a plugin destroyed before
      // detach nulls this field instead of leaving it dangling.
      DSP::dspObject* insert_dsp;

      // Per-output peak (absolute sample value), refreshed once per DSP block.
      // Sized to out.size() and resized on the same path as out (audio thread
      // tolerates allocation here only because the channel layout rarely
      // changes — see deviceManager::update). Audio thread stores with
      // release; control-thread readers load with acquire.
      std::vector<atomicPeak> lastPeakLinearPre;
      std::vector<atomicPeak> lastPeakLinearPost;

      Bool userChannel = true; // channel is created by user and not crucial for the system
      Bool allowVirtual;

      // ─── Send / return bus state (issue #165) ───
      // `isReturn`, `sendSlotCount` are control-thread writes applied before the
      // impl is handed to the slow/audio threads (mirrors `parent`), then treated
      // as immutable — published once via the setup/ready handshake, read after.
      // `generation` and `sends` are mutated only on the audio thread.
      Bool isReturn = false; // this channel is a return bus (excluded from the dsp tree)
      Int generation = 0; // return processing-order layer (SET_GENERATION); audio thread only
      Int sendSlotCount = 4; // number of send slots; sized once in setup()
      Bool sendsSized = false; // guards the one-time setup() sizing of `sends`

      // Fixed vector of send slots, sized once at setup() and never resized on
      // the render path (so slot addresses stay stable as registry nodes).
      std::vector<sendSlot> sends;

      // Back-reference registry: the send slots currently targeting THIS return.
      // Mutated only on the audio thread (ADD_SEND / REMOVE_SEND / detachSends).
      // Empty for non-return channels.
      IntrusiveForwardList<sendSlot, &sendSlot::regNext> sendRegistry;

      /** Accumulate one active slot's ramped, scaled `out` into its target
          return's `out`. Audio thread only; no allocation or locking. */
      void accumulateSend(sendSlot& slot);

      friend class SOUND::implementationObject;
      friend class YSE::channel;
      friend class YSE::REVERB::managerObject;
      friend class DEVICE::managerObject;
      friend class DEVICE::deviceManager;
      friend class CHANNEL::managerObject;
    };

    /**
    This is a helper class for calculating the channel and sound volume. Don't use it
    anywhere else.
    */
    class output {
    public:
      Flt angle;
      // Speaker-density normalisation term, precomputed on layout change in
      // computeEffectiveSpeakerWeights() (issue #211). Read by the sound
      // panner's computeFinalGains().
      Flt effective;
      // True for the low-frequency-effects (.1) output. Such an output is kept
      // out of azimuth panning: positional sounds are never panned into it
      // (issue #203).
      Bool isLFE;

      // NB: the per-source pan scratch (initPan/initGain/ratio/finalGain) that
      // used to live here was removed in issue #212. It was shared per-channel
      // state written as per-sound scratch, which is exactly what forbade
      // caching a sound's gain vector; each sound now owns its own scratch and
      // cache (SOUND::implementationObject::finalGainCache / initGainScratch).
      output() : angle(0.f), effective(1.f), isLFE(false) {}
    };

  } // namespace CHANNEL
} // namespace YSE

#endif // CHANNELIMPLEMENTATION_H_INCLUDED
