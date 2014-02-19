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
    /*  to avoid unneccesary atomic vars, in this class every variable used in dsp functions must
        end with dsp. If you use it in any other place, it must be atommic.
    */
    class soundImplementation {
    public:
      Bool create(const std::string &fileName, Bool stream = false);

      void initialize(); // sets variables and asks memory. Use this when create returns true;

      void update(); // runs at soundsystem update
      Bool dsp(); // runs in audio callback
      void toChannels(); //
      void calculateGain(Int channel, Int source);

      soundImplementation();
      ~soundImplementation();

      static bool sortSoundObjects(const soundImplementation &, const soundImplementation &);
    private:


      soundFile * file;

      // for sound positioning and changing that
      Flt filePtr_dsp;  // this is the real file position pointer
      aFlt newFilePos_dsp; // this is a new value, set from the front end
      aFlt currentFilePos_dsp; // this gets updated after dsp, so we can query the file position
      Bool setFilePos_dsp; // this signals the dsp function to get his position from newFilePos

      //TODO: this cannot be atomic and is changed a lot
      SOUND_STATUS intent_dsp;

      Vec newPos, lastPos, vel;
      Vec _pos;
      // constructor check
      Dbl _cCheck;

      // virtual sound calculation
      aBool isVirtual_dsp;
      Flt  virtualDist; // gain sum of all channels



      std::vector<DSP::sample> filebuffer_dsp;
      std::vector<DSP::sample> * buffer_dsp;
      DSP::sample channelBuffer_dsp; // temporary buffer to adjust channel gain
      std::vector< std::vector<Flt> > lastGain_dsp; // needed for each channel to smooth gain changes

      aFlt distance_dsp;
      aFlt angle_dsp;

      // for pitch shift and doppler
      aFlt velocity_dsp;
      aFlt pitch_dsp;

      // the distance before distance attenuation begins. 
      aFlt size_dsp;
      soundImplementation & size(Flt value);
      Flt size();

      // volume
      // TODO: check if ramp getValue is threadsafe
      DSP::ramp fader_dsp;
      Bool fadeAndStop_dsp;
      aBool setVolume_dsp;
      aFlt volumeValue_dsp;
      aFlt volumeTime_dsp;
      aFlt currentVolume_dsp;
      aBool setFadeAndStop_dsp;
      aFlt fadeAndStopTime_dsp;

      // for multichannel sounds
      aFlt spread_dsp;

      // dsp slots
      DSP::dspSourceObject * source_dsp;
      void addSourceDSP(DSP::dspSourceObject & ptr);

      Bool _setPostDSP;
      std::atomic<DSP::dspObject *> _postDspPtr;
      DSP::dspObject * post_dsp;
      void addDSP(DSP::dspObject & ptr);

      INTERNAL::channelImplementation * parent_dsp;
      soundImplementation ** link; // will contain a reference to the pimpl of parent

      Bool _release; // flag this for release in next update
      aBool looping_dsp;
            // 3D
      Bool _relative; // relative position and angle to player. Can be used for 2D sounds.
      Bool _noDoppler; // don't add doppler to this sound
      Bool _pan2D; // to remember shortcut (set relative and noDoppler) 
      Bool _occlusionActive;
      aFlt occlusion_dsp;

      // control vars
      aBool signalPlay_dsp;
      aBool signalPause_dsp;
      aBool signalStop_dsp;
      aBool signalToggle_dsp;
      aBool signalRestart_dsp;

      Flt bufferVolume_dsp;
      UInt startOffset;
      UInt stopOffset;

      // info 
      UInt _length;
      aBool streaming_dsp;
      aBool loading_dsp;

      friend class sound;
      friend class INTERNAL::channelImplementation;
      friend class channelManager;
      friend class soundManager;
      
    };
  }

}





#endif  // SOUNDIMPLEMENTATION_H_INCLUDED
