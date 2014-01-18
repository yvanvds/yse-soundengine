#pragma once
#include <string>
#include <vector>
#include "boost/ptr_container/ptr_vector.hpp"
#include "headers/defines.hpp"
#include "internal/soundimpl.h"
#include "backend/reverbbackend.h"
#include "channel.hpp"
#include "utils/guard.h"
#include "dsp/filters.hpp"
#include "reverb.hpp"

namespace YSE {
  class soundimpl;

	struct output {
		Flt angle;
		Flt initPan;
		Flt initGain;
		Flt effective;
		Flt ratio;
		Flt finalGain;
		output() { angle = 0.0f; }
	};

  class channelimpl {
  public:
		channelimpl& volume(Flt value);
		Flt volume();

		channelimpl();
   ~channelimpl();
		
		Bool add(channelimpl * ch);
		Bool add(soundimpl * s);
		Bool remove(channelimpl * ch);
		Bool remove(soundimpl * s);
		void create();
    void update();

		Flt _volume;
		Flt _lastVolume;
		aBool _setVolume;
    aFlt _newVolume;
    aFlt _currentVolume;

    // underwater FX
    aFlt underWaterDepth;


    channelimpl * parent;
		std::vector<channelimpl*> children;
		std::vector<soundimpl*> sounds;
		boost::ptr_vector<output> outConf;
		std::vector<DSP::sample> out;

		void clearBuffers();
		void dsp();
    void adjustVolume();
		void buffersToParent();
		Bool userChannel; // channel is created by user and not crucial for the system
		Bool _allowVirtual;
		channelimpl& allowVirtual(Bool value);
		Bool allowVirtual();

		channelimpl& set(Int count); // use this for custom speaker positions, in combination with the pos function below
		channelimpl& pos(Int nr, Flt angle); // set speaker to angle in degrees (-180 -> 180)
	
		// reverb
		reverbBackend * reverb;

    Bool _release;
    channelimpl ** link;
	};



	
	
}

	

