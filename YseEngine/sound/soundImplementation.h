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
        This is the internal counterpart of a sound. Maintenance of these objects is done internally.
        The user will never have to create or delete these objects, as the are all held 
        by the soundManager object, in the soundObjects forward_list.
    */

    class implementationObject {
    public:

      /** Constructor
      @param head       Pass a pointer to the actual sound object (created by the user)
                        to this implementation. Implementations will be moved to a 
                        eraser queue when the sound object goes out of scope.
      */
      implementationObject(sound * head);
      ~implementationObject();

      /** Set up a new sound object. This is called by the sound class. When creating a 
          sound (which must be loaded from disk), the initial state will be 'loading'. The object
          waits for the soundFile to be loaded (in another thread) before doing any updates 
          or dsp processing.
          
          @param fileName   The name of the file to open. This can be an absolute path or
                            a path relative to the the working directory.
          @param stream     Indicates if the sound will be streamed from disk. Streaming sounds
                            do not share their buffer with other soundImplementations as it will 
                            be loaded while playing. In contrast, non streaming sounds will load the whole
                            sound in memory so that is can be used by multiple sound implementations.

          @return           False if the sound can't be found or opened. True otherwise.
      */
      Bool create(const std::string &fileName, channel * ch, Bool loop, Flt volume, Bool streaming);

      Bool create(YSE::DSP::buffer & buffer, channel * ch, Bool loop, Flt volume);
      Bool create(MULTICHANNELBUFFER & buffer, channel * ch, Bool loop, Flt volume);

      Bool create(DSP::dspSourceObject & ptr, channel * ch, Flt volume);

      bool create(PATCHER::patcherImplementation * patcher, channel * ch, float volume);

      //Bool create(SYNTH::implementationObject * ptr, channel * ch, Flt volume);

      /** Initialize some settings for the sound after creation.
          @param head       Passes a pointer to the actual sound object (created by the user)
                            to this implementation. Implementations will be deleted when this
                            object goes out of scope.
      */
      void initialize(sound * head); 

      void sendMessage(const messageObject & message);

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

      void parseMessage(const messageObject & message);

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
      static bool sortSoundObjects(implementationObject *, implementationObject *);

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
      This function is used by the forward_list remove_if function
      */
      static bool canBeRemovedFromLoading(const std::atomic<implementationObject*> & impl) {
        if (impl.load()->objectStatus == OBJECT_READY
          || impl.load()->objectStatus == OBJECT_RELEASE
          || impl.load()->objectStatus == OBJECT_DELETE) {
          return true;
        }

        return false;
      }

	  // these are frequently updated by the implementation and to be read by head
	  // originally they were in the interface, but atomics must be shielded from this
	  // when creating a managed dll
	  aBool _head_streaming;
	  aUInt _head_length;
	  aFlt  _head_time;
	  std::atomic<SOUND_STATUS> _head_status; // what it is currently doing

    private:
      void dspFunc_parseIntent();
      void dspFunc_calculateGain(Int channel, Int source);

      // for streaming sounds
      INTERNAL::soundFile * file;

      // buffers
      std::vector<DSP::buffer> filebuffer;
      std::vector<DSP::buffer> * buffer;
      DSP::buffer channelBuffer; // temporary buffer to adjust channel gain
      std::vector< std::vector<Flt> > lastGain; // needed for each channel to smooth gain changes
      Flt bufferVolume; // keep track of actual volume in buffer (may vary all the time, not used elsewhere)

      // for sound positioning and changing that
      // only use these inside sync and dsp functions
      Flt filePtr;  // this is the real file position pointer
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
      Flt velocity;
      Flt pitch;
      // the distance before distance attenuation begins. 
      Flt size;
      //soundImplementation & size(Flt value);
      //Flt size();

      // virtual sound calculation
      
      Flt  virtualDist; // gain sum of all channels

      // volume
      // TODO: check if ramp getValue is threadsafe
      DSP::ramp fader; // only use in dsp and sync
      Bool setVolume; // only use in dsp and sync
      Flt volumeValue; // only use in dsp and sync
      Flt volumeTime; // only use in dsp and sync
      Bool setFadeAndStop; // only use in dsp and sync
      Flt  fadeAndStopTime; // only use in dsp and sync
      Bool stopAfterFade; // only use in dsp and sync

      Flt currentVolume_dsp;    // the actual volume as seen in dsp func
      Flt currentVolume_upd; // the actual volume as seen in update func

      Bool looping;
      Bool relative; // relative position and angle to player. Can be used for 2D sounds.
      Bool doppler; // add doppler to this sound
      Bool occlusionActive;
      Flt occlusion_dsp;

      // for multichannel sounds
      Flt spread;

      // patcher
      PATCHER::patcherImplementation * patcher;

      // dsp slots
      DSP::dspSourceObject * source_dsp;

      Bool _setPostDSP;
      std::atomic<DSP::dspObject *> _postDspPtr;
      DSP::dspObject * post_dsp;
      void addDSP(DSP::dspObject & ptr);

      CHANNEL::implementationObject * parent;
   
      UInt startOffset;
      UInt stopOffset;

      // synth
      //SYNTH::implementationObject * synth;

      // info 
      Bool streaming;

      std::atomic<sound *> head; // < The interface connected to this object
      std::atomic<OBJECT_IMPLEMENTATION_STATE> objectStatus; // < the status of this object
      lfQueue<messageObject> messages;

      enum PLAYER_TYPE 
      {
        PT_FILE,
        PT_DSP,
        PT_PATCHER,
      };

      PLAYER_TYPE playerType;

      friend class YSE::SOUND::managerObject;
      friend class YSE::CHANNEL::implementationObject;
    };
  }

}





#endif  // SOUNDIMPLEMENTATION_H_INCLUDED
