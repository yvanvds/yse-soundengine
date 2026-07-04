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

      // Per-output peak (absolute sample value), refreshed once per DSP block.
      // Sized to out.size() and resized on the same path as out (audio thread
      // tolerates allocation here only because the channel layout rarely
      // changes — see deviceManager::update). Audio thread stores with
      // release; control-thread readers load with acquire.
      std::vector<atomicPeak> lastPeakLinearPre;
      std::vector<atomicPeak> lastPeakLinearPost;

      Bool userChannel = true; // channel is created by user and not crucial for the system
      Bool allowVirtual;

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
      Flt initPan;
      Flt initGain;
      Flt effective;
      Flt ratio;
      Flt finalGain;
      // True for the low-frequency-effects (.1) output. Such an output is kept
      // out of azimuth panning: positional sounds are never panned into it
      // (issue #203).
      Bool isLFE;

      output()
        : angle(0.f),
          initPan(0.f),
          initGain(1.f),
          effective(1.f),
          ratio(1.f),
          finalGain(1.f),
          isLFE(false) {}
    };

  } // namespace CHANNEL
} // namespace YSE

#endif // CHANNELIMPLEMENTATION_H_INCLUDED
