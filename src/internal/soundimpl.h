#pragma once
#include "backend/soundfile.h"
#include <vector>
#include "utils/vector.hpp"
#include "dsp/delay.hpp"
#include "dsp/oscillators.hpp"
#include "dsp/math.hpp"
#include "dsp/dsp.hpp"
#include "dsp/ramp.hpp"
#include "internal/channelimpl.h"
#include "utils/guard.h"

namespace YSE {
	

	class channelimpl;



	class soundimpl {
  public:
		soundFile * file;

    // for sound positioning and changing that
		Flt filePtr;  // this is the real file position pointer
    aFlt newFilePos; // this is a new value, set from the front end
    aFlt currentFilePos; // this gets updated after dsp, so we can query the file position
    aBool setFilePos; // this signals the dsp function to get his position from newFilePos

    SOUND_STATUS _status;

		Vec newPos, lastPos, vel;
    aVec _pos;
    // constructor check
    Dbl _cCheck;
		
		// virtual sound calculation
		Bool isVirtual;
		Flt  virtualDist; // gain sum of all channels

		Bool create(const std::string &fileName, Bool stream = false);

		void initialize(); // sets variables and asks memory. Use this when create returns true;

		void update(); // runs at soundsystem update
		Bool dsp(); // runs in audio callback
		void toChannels(); //
		void calculateGain(Int channel, Int source);

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
		soundimpl& size(Flt value);
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
		DSP::dspSource * source_dsp;
		void addSourceDSP(DSP::dspSource & ptr);
		
    aBool _setPostDSP;
    std::atomic<DSP::dsp *> _postDspPtr;
    DSP::dsp * post_dsp;
		void addDSP(DSP::dsp & ptr);

		soundimpl();
    ~soundimpl();
		channelimpl * parent;
    soundimpl ** link; // will contain a reference to the pimpl of parent

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
    aInt _latency; // latency on signal in ms, as set on note on / off
    Int bufferLatency;
    Flt bufferVolume;
    UInt startOffset;
    UInt stopOffset;

    // info 
    aUInt _length;
    aBool _streaming;
    aBool _loading;

	};

	void AdjustLastGainBuffer();
	

}

