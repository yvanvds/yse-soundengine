/*
  ==============================================================================

    soundImplementation.h
    Created: 28 Jan 2014 11:50:52am
    Author:  yvan

  ==============================================================================
*/

#ifndef SOUNDIMPLEMENTATION_H_INCLUDED
#define SOUNDIMPLEMENTATION_H_INCLUDED

#include <forward_list>
#include "../classes.hpp"
#include "sound.hpp"
#include "../dsp/buffer.hpp"
#include "../dsp/ramp.hpp"
#include "../utils/lfQueue.hpp"

namespace YSE {
  namespace SOUND {

    /**
        This is the internal counterpart of a sound. Maintenance of these objects is done
       internally. The user will never have to create or delete these objects, as the are all held
        by the soundManager object, in the soundObjects forward_list.
    */

    class implementationObject {
    public:
      /** Constructor
      @param head       Pass a pointer to the actual sound object (created by the user)
                        to this implementation. Implementations will be moved to a
                        eraser queue when the sound object goes out of scope.
      */
      implementationObject(sound* head);
      virtual ~implementationObject();

      /** Set up a new sound object. This is called by the sound class. When creating a
          sound (which must be loaded from disk), the initial state will be 'loading'. The object
          waits for the soundFile to be loaded (in another thread) before doing any updates
          or dsp processing.

          @param fileName   The name of the file to open. This can be an absolute path or
                            a path relative to the the working directory.
          @param stream     Indicates if the sound will be streamed from disk. Streaming sounds
                            do not share their buffer with other soundImplementations as it will
                            be loaded while playing. In contrast, non streaming sounds will load the
         whole sound in memory so that is can be used by multiple sound implementations.

          @return           False if the sound can't be found or opened. True otherwise.
      */
      Bool create(const std::string& fileName, channel* ch, Bool loop, Flt volume, Bool streaming);

      Bool create(YSE::DSP::buffer& buffer, channel* ch, Bool loop, Flt volume);
      Bool create(MULTICHANNELBUFFER& buffer, channel* ch, Bool loop, Flt volume);

      Bool create(DSP::dspSourceObject& ptr, channel* ch, Flt volume);

      bool create(PATCHER::patcherImplementation* patcher, channel* ch, float volume);

      // Bool create(SYNTH::implementationObject * ptr, channel * ch, Flt volume);

      /** Initialize some settings for the sound after creation.
          @param head       Passes a pointer to the actual sound object (created by the user)
                            to this implementation. Implementations will be deleted when this
                            object goes out of scope.
      */
      void initialize(sound* head);

      void sendMessage(const messageObject& message);

      /** Syncronises parameters between the 'head' and this implementation. All implementations
          are syncronized in one loop at the beginning of update, which is inside a crit section.
          This approach has two advantages:

          1. We can limit the amount of atomics to a minumum, while still having only one
             lock.
          2. The rest of the update function can run together with the dsp functions. It doesn't
             have to be locked.

          This function is overwritten from the base class because in cause of a sound, we need
          a short fade-out before we actually release a sound.
      */
      virtual void sync();

      void parseMessage(const messageObject& message);

      /** This function runs in the global System().update() and updates position, velocity
          and other stuff that should be calculated frequently but it not directly related
          to the actual dsp buffer.
      */
      void update();

      /** Calculates the next output buffer for this sound. This is called from the
          channelImplementation this object is linked to.
      */
      Bool dsp();

      /** After all channels, subchannels and soundImplementations are done with their
          dsp functions, the toChannels() method is called recursively from the main
          mix, as to gather all calculated buffers into a single main buffer.
      */
      void toChannels();

      /** This function is called from soundManager::setup and checks if the soundfile
          connected to this sound is ready. If so, it will setup the buffers needed for this
          sound.
      */
      void setup();

      /** This function resizes internal containers to the number of channels used by
          the current device
      */
      void resize();

      /** This function is called by soundManager::update (from dsp callback) and verifies
          if the sound is ready to be played. It will then be moved from soundsToLoad
          to soundsInUse.
      */
      Bool readyCheck();

      virtual void doThisWhenReady();

      /** This is a helper function for the standard forward_list sorting. It compares
          soundImplementations on the basis of their virtualDistance. That value indicates
          how important the sound is compared to other sounds. It is used to find out
          which sounds should be virtual.
      */
      static bool sortSoundObjects(implementationObject*, implementationObject*);

      /** Compute the virtualization priority metric for a sound. Lower values
          are more important (kept real); the VirtualSoundFinder virtualizes the
          sounds with the *highest* values first. Importance rises with volume
          and falls with distance, so quiet distant sounds are virtualized
          before loud near ones. A muted sound (volume ~ 0) yields a very large
          value and is virtualized outright rather than promoted. See issue #205.
      */
      static Flt computeVirtualDist(Flt distance, Flt size, Flt volume);

      /** The per-block virtualization decision, kept as a pure static helper so
          the transition logic stays unit-testable in isolation (see issue #206).
          @param real       The VirtualSoundFinder's hysteresis-aware verdict for
                            this block (true = the sound should be audible).
          @param wasVirtual This sound's virtual state after the previous block.
       */
      struct virtualAction {
        bool render; // run dsp() + toChannels() for this block?
        bool fadeOut; // force channel gains to 0 so the block ramps to silence
        bool nowVirtual; // this sound's virtual state after this block
      };
      static virtualAction computeVirtualAction(bool real, bool wasVirtual);

      /** Cardioid overlap weight between two speakers, in [0..1]. Used by the
          density compensation in toChannels() to count how many speakers
          effectively overlap a given one (coincident speakers -> 1, opposite
          speakers -> 0). Kept as a pure static helper so the weighting stays
          unit-testable and mirrors the pan term's parenthesization. See #207.
      */
      static Flt computeSpeakerOverlap(Flt angleA, Flt angleB);

      /** Doppler playback-rate multiplier for a moving source/listener pair.

          The observed frequency is the emitted frequency times a *ratio*
          ``(c + rList) / (c + rSource)`` where ``c`` is the speed of sound and
          ``rSource`` / ``rList`` are the radial (along the source→listener axis)
          components of the source and listener velocities. Doppler is a
          frequency ratio, so it must be applied multiplicatively to the
          playback speed (``pitch * ratio``), not added to it — the old additive
          linearisation was only accurate at pitch = 1 and low speeds. Returns
          exactly 1.0 (no shift) when neither party moves or the pair is
          co-located. ``dopplerScale`` scales the deviation from unity, and the
          result is clamped to a sane band so a super-sonic closing speed can't
          drive the playback rate negative or unbounded on the audio thread.
          Kept as a pure static helper so the ratio math stays unit-testable in
          isolation. See issue #208.
      */
      static Flt computeDopplerRatio(const Pos& sourceVel, const Pos& listenerVel, const Pos& dist,
                                     Flt dopplerScale);

      void removeInterface();

      OBJECT_IMPLEMENTATION_STATE getStatus();
      void setStatus(OBJECT_IMPLEMENTATION_STATE value);

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
      `toLoad` until setup completes (status transitions to OBJECT_SETUP).
      */
      static bool canBeRemovedFromLoading(const implementationObject* impl) {
        if (impl->objectStatus == OBJECT_READY || impl->objectStatus == OBJECT_RELEASE ||
            impl->objectStatus == OBJECT_DELETE) {
          return true;
        }

        return false;
      }

      /** Attempt to atomically transition this impl from OBJECT_CREATED to
      OBJECT_SETTING_UP. Returns true if this caller now owns the right to run
      setup() on it. Used by the slow-pool's setupJob to claim impls without
      racing other slow-pool ticks. */
      bool tryClaimForSetup() {
        OBJECT_IMPLEMENTATION_STATE expected = OBJECT_CREATED;
        return objectStatus.compare_exchange_strong(expected, OBJECT_SETTING_UP);
      }

      // these are frequently updated by the implementation and to be read by head
      // originally they were in the interface, but atomics must be shielded from this
      // when creating a managed dll
      aBool _head_streaming;
      aUInt _head_length;
      aFlt _head_time;
      std::atomic<SOUND_STATUS> _head_status; // what it is currently doing

    private:
      void dspFunc_parseIntent();
      void dspFunc_calculateGain(Int channel, Int source);

      // for streaming sounds
      INTERNAL::soundFile* file;

      // buffers
      std::vector<DSP::buffer> filebuffer;
      std::vector<DSP::buffer>* buffer;
      DSP::buffer channelBuffer; // temporary buffer to adjust channel gain
      std::vector<std::vector<Flt>> lastGain; // needed for each channel to smooth gain changes
      Flt bufferVolume; // keep track of actual volume in buffer (may vary all the time, not used
                        // elsewhere)

      // for sound positioning and changing that
      // only use these inside sync and dsp functions
      Flt filePtr; // this is the real file position pointer
      Flt newFilePos; // this is a new value, set from the front end
      Flt currentFilePos; // this gets updated after dsp, so we can query the file position
      Bool setFilePos; // this signals the dsp function to get his position from newFilePos

      SOUND_STATUS status_dsp; // use in dsp only
      SOUND_STATUS status_upd; // use outside dsp, is synced
      // this contains an action from the sound interface
      SOUND_INTENT headIntent;

      // sound properties
      Pos pos; // desired position
      Pos newPos, lastPos, velocityVec;
      Flt distance;
      Flt angle;
      // for pitch shift and doppler
      // dopplerRatio is a playback-rate *multiplier* (1.0 = no shift), written
      // by update() on the control thread and read by dsp() on the audio thread.
      // dopplerSlew slews that stepwise target at block rate so an uneven
      // update tick rate can't inject a pitch warble (issue #208).
      Flt dopplerRatio;
      DSP::lint dopplerSlew;
      Flt pitch;
      // the distance before distance attenuation begins.
      Flt size;
      // soundImplementation & size(Flt value);
      // Flt size();

      // virtual sound calculation

      Flt virtualDist; // gain sum of all channels
      Bool isVirtual; // this sound's virtual state after the previous dsp block (#206)
      Bool virtualFadeOut; // set in dsp(), read in toChannels(): force gains to 0 for
                           // one farewell block so going virtual fades instead of clicks (#206)

      // volume
      // TODO: check if ramp getValue is threadsafe
      DSP::ramp fader; // only use in dsp and sync
      Bool setVolume; // only use in dsp and sync
      Flt volumeValue; // only use in dsp and sync
      Flt volumeTime; // only use in dsp and sync
      Bool setFadeAndStop; // only use in dsp and sync
      Flt fadeAndStopTime; // only use in dsp and sync
      Bool stopAfterFade; // only use in dsp and sync

      Flt currentVolume_dsp; // the actual volume as seen in dsp func
      Flt currentVolume_upd; // the actual volume as seen in update func

      Bool looping;
      Bool relative; // relative position and angle to player. Can be used for 2D sounds.
      Bool doppler; // add doppler to this sound
      Bool occlusionActive;
      Flt occlusion_dsp;

      // for multichannel sounds
      Flt spread;

      // patcher
      PATCHER::patcherImplementation* patcher;

      // dsp slots
      // Atomic: written once on the main thread in create(DSP::dspSourceObject&...),
      // read by the audio thread in dsp() every callback, and nullified by the
      // audio thread at the OBJECT_RELEASE→OBJECT_DELETE transition (defensive,
      // protects against a user-supplied source object whose lifetime ends
      // slightly before the impl's deleteJob runs). See sound.hpp for the
      // lifetime contract callers must satisfy.
      std::atomic<DSP::dspSourceObject*> source_dsp;

      Bool _setPostDSP;
      std::atomic<DSP::dspObject*> _postDspPtr;
      DSP::dspObject* post_dsp;
      void addDSP(DSP::dspObject& ptr);

      CHANNEL::implementationObject* parent;
      // True once the audio thread has called parent->connect(this) and the
      // impl appears in parent->sounds. Cleared by the audio thread before it
      // transitions the impl to OBJECT_DELETE in SOUND::Manager::update(), so
      // the slow-pool's destructor-driven disconnect becomes a no-op. Read by
      // both the audio thread (write side) and the slow-pool thread
      // (destructor read side); atomic guarantees correct visibility.
      std::atomic<bool> connectedToParent{false};

      UInt startOffset;
      UInt stopOffset;

      // synth
      // SYNTH::implementationObject * synth;

      // info
      Bool streaming;

      std::atomic<sound*> head; // < The interface connected to this object
      std::atomic<OBJECT_IMPLEMENTATION_STATE> objectStatus; // < the status of this object
      lfQueue<messageObject> messages;

      enum PLAYER_TYPE {
        PT_FILE,
        PT_DSP,
        PT_PATCHER,
      };

      PLAYER_TYPE playerType;

      friend class YSE::SOUND::managerObject;
      friend class YSE::CHANNEL::implementationObject;
    };
  } // namespace SOUND

} // namespace YSE

#endif // SOUNDIMPLEMENTATION_H_INCLUDED
