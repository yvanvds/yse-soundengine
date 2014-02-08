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

      void initialize(); // sets variables and asks memory. Use this when create returns true;

      void update(); // runs at soundsystem update
      Bool dsp(); // runs in audio callback
      void toChannels(); //
      void calculateGain(Int channel, Int source);

      soundImplementation();
      ~soundImplementation();

    private:


      soundFile * file;

      // for sound positioning and changing that
      Flt filePtr;  // this is the real file position pointer
      aFlt newFilePos; // this is a new value, set from the front end
      aFlt currentFilePos; // this gets updated after dsp, so we can query the file position
      aBool setFilePos; // this signals the dsp function to get his position from newFilePos

      SOUND_STATUS intent;

      Vec newPos, lastPos, vel;
      aVec _pos;
      // constructor check
      Dbl _cCheck;

      // virtual sound calculation
      Bool isVirtual;
      Flt  virtualDist; // gain sum of all channels



      std::vector<DSP::sample> filebuffer;
      std::vector<DSP::sample> * buffer;
      DSP::sample channelBuffer; // temporary buffer to adjust channel gain
      std::vector< std::vector<Flt> > lastGain; // needed for each channel to smooth gain changes

      Flt distance;
      Flt angle;

      // for pitch shift and doppler
      Flt velocity;
      aFlt _pitch;

      // the distance before distance attenuation begins. 
      aFlt _size;
      soundImplementation & size(Flt value);
      Flt size();

      // volume
      DSP::ramp _fader;
      Bool _fadeAndStop;
      aBool _setVolume;
      aFlt _volumeValue;
      aFlt _volumeTime;
      aFlt _currentVolume;
      aBool _setFadeAndStop;
      aFlt _fadeAndStopTime;

      // for multichannel sounds
      aFlt _spread;

      // dsp slots
      DSP::dspSourceObject * source_dsp;
      void addSourceDSP(DSP::dspSourceObject & ptr);

      aBool _setPostDSP;
      std::atomic<DSP::dspObject *> _postDspPtr;
      DSP::dspObject * post_dsp;
      void addDSP(DSP::dspObject & ptr);

      INTERNAL::channelImplementation * parent;
      soundImplementation ** link; // will contain a reference to the pimpl of parent

      Bool _release; // flag this for release in next update
      aBool looping;

      // 3D
      aBool _relative; // relative position and angle to player. Can be used for 2D sounds.
      aBool _noDoppler; // don't add doppler to this sound
      aBool _pan2D; // to remember shortcut (set relative and noDoppler) 
      aBool _occlusionActive;
      Flt _occlusion;

      // control vars
      aBool _signalPlay;
      aBool _signalPause;
      aBool _signalStop;
      aBool _signalToggle;
      aBool _signalRestart;

      Flt bufferVolume;
      UInt startOffset;
      UInt stopOffset;

      // info 
      aUInt _length;
      aBool _streaming;
      aBool _loading;

      friend class sound;
      friend class INTERNAL::channelImplementation;
      friend class channelManager;
      friend class soundManager;
    };
  }

}





#endif  // SOUNDIMPLEMENTATION_H_INCLUDED
