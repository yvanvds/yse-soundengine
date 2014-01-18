#pragma once
#include "channel.hpp"
#include "sound.hpp"
#include "dsp/dsp.hpp"
#include "headers/enums.hpp"
#include "device.hpp"

namespace YSE {

	class API system {
  public:
		Bool init		();
		void update	();
		void close	();
				~system	();
		
		// audio device
		Bool	setDevice		(UInt ID, CHANNEL_TYPE conf = CT_AUTO, Int count = 2);
		void	speakerPos	(Int  nr, Flt angle);
		Int		numDevices	();
		Int		activeDevice();
    audioDevice & getDevice(UInt nr);

		// effects
		void insideCave(Bool status);

    // sound occlusion
    /* you should provide your own function for occlusion checks.
       Assuming that your game uses physics, this is quite easy to implement.
       All you have to do is a raycast form the first to the second position and see
       if there are any objects inbetween that should occlude the sound and decide how
       much you want to occlude it.
    */
    system& setOcclusionCallback(Flt(*func)(const YSE::Vec&, const YSE::Vec&));

		// config
		system& dopplerScale	(Flt scale	);	Flt dopplerScale		();
		system& distanceFactor(Flt factor	);	Flt distanceFactor	();
		system& rolloffScale	(Flt scale	);	Flt rolloffScale		(); 
		system& maxSounds			(Int value	);	Int maxSounds				(); // the maximum amount of sounds that are actually used. If the number of sounds exeeds this, the least significant ones will be turned virtual

		// statistics
		Flt cpuLoad(); // cpu load of the audio steam (not the YSE update system)
		// utiles
    void sleep(UInt ms); // usefull for console applications if you don't want to run update at max speed
	};

	extern API system System;

}