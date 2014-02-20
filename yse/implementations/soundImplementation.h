/*
  ==============================================================================

    soundImplementation.h
    Created: 28 Jan 2014 11:50:52am
    Author:  yvan

  ==============================================================================
*/

#ifndef SOUNDIMPLEMENTATION_H_INCLUDED
#define SOUNDIMPLEMENTATION_H_INCLUDED

#include "JuceHeader.h"
#include "../internal/soundFile.h"
#include "../utils/vector.hpp"
#include "../dsp/ramp.hpp"
#include "../dsp/dspObject.hpp"
#include <forward_list>
#include "../classes.hpp"

namespace YSE {
  namespace INTERNAL {
    

    class soundImplementation {
    public:

      Bool create(const std::string &fileName, Bool stream = false);

      void initialize(sound * head); // sets variables and asks memory. Use this when create returns true;
      void sync(); // used by soundmanager to sync variables between sound and soundimplementation
      void update(); // runs at soundsystem update
      Bool dsp(); // runs in audio callback
      void toChannels(); //
      void calculateGain(Int channel, Int source);

      soundImplementation();
      ~soundImplementation();

      static bool sortSoundObjects(const soundImplementation &, const soundImplementation &);
    private:
      void dspFunc_parseIntent();


      // for streaming sounds
      soundFile * file;

      // buffers
      std::vector<DSP::sample> filebuffer_dsp;
      std::vector<DSP::sample> * buffer_dsp;
      DSP::sample channelBuffer_dsp; // temporary buffer to adjust channel gain
      std::vector< std::vector<Flt> > lastGain_dsp; // needed for each channel to smooth gain changes

      // for sound positioning and changing that
      Flt filePtr_dsp;  // this is the real file position pointer
      Flt newFilePos_dsp; // this is a new value, set from the front end
      Flt currentFilePos_dsp; // this gets updated after dsp, so we can query the file position
      Bool setFilePos_dsp; // this signals the dsp function to get his position from newFilePos

      //TODO: this cannot be atomic and is changed a lot
      SOUND_STATUS intent_dsp;
      // this contains an action from the sound interface
      SOUND_INTENT headIntent;

      Vec pos; // desired position
      Vec newPos, lastPos, velocityVec;
      
      // constructor check
      Dbl _cCheck;

      // virtual sound calculation
      Bool isVirtual_dsp;
      Flt  virtualDist; // gain sum of all channels





      Flt distance_dsp;
      Flt angle_dsp;

      // for pitch shift and doppler
      aFlt velocity;
      Flt pitch_dsp;

      // the distance before distance attenuation begins. 
      Flt size_dsp;
      soundImplementation & size(Flt value);
      Flt size();

      // volume
      // TODO: check if ramp getValue is threadsafe
      DSP::ramp fader;
      Bool setVolume;
      Flt volumeValue;
      Flt volumeTime;
      Flt currentVolume_dsp;    // the actual volume as seen in dsp func
      Flt currentVolume_upd; // the actual volume as seen in update func
      Bool setFadeAndStop;
      Flt  fadeAndStopTime;
      Bool stopAfterFade;

      // for multichannel sounds
      Flt spread;

      // dsp slots
      DSP::dspSourceObject * source_dsp;
      void addSourceDSP(DSP::dspSourceObject & ptr);

      Bool _setPostDSP;
      std::atomic<DSP::dspObject *> _postDspPtr;
      DSP::dspObject * post_dsp;
      void addDSP(DSP::dspObject & ptr);

      INTERNAL::channelImplementation * parent_dsp;
      sound * head; // will contain a pointer to the public part of the sound

      Bool release; // flag this for release in next update
      Bool looping_dsp;
            // 3D
      Bool relative; // relative position and angle to player. Can be used for 2D sounds.
      Bool doppler; // add doppler to this sound
      Bool occlusionActive;
      Flt occlusion_dsp;



      Flt bufferVolume_dsp;
      UInt startOffset;
      UInt stopOffset;

      // info 
      UInt _length;
      Bool streaming_dsp;
      aBool loading;

      friend class sound;
      friend class INTERNAL::channelImplementation;
      friend class channelManager;
      friend class soundManager;
      
    };
  }

}





#endif  // SOUNDIMPLEMENTATION_H_INCLUDED
