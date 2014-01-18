#pragma once
#include "dsp/sample.hpp"
#include <vector>

namespace YSE {

	struct speaker {
		Flt angle;
		Flt initPan;
		Flt initGain;
		Flt effective;
		Flt ratio;
		Flt finalGain;
		//DSP::sample * buffer;
	};

	class speakerSet {
  public:
		speakerSet& setMono();
		speakerSet& setStereo();
		speakerSet& setQuad();
		speakerSet& set51();
		speakerSet& set51Side();
		speakerSet& set61();
		speakerSet& set71();
		speakerSet& setAuto(Int count);
		
		speakerSet& set(Int count, Bool sub = false); // use this for custom speaker positions, in combination with the pos function below
		speakerSet& pos(Int nr, Flt angle); // set speaker to angle in degrees (-180 -> 180)
		
		//void connect(void *channels, unsigned long length); // internal use only, set channels from callback
		
		std::vector<speaker> channel;
  private:
		Bool sub;
	};

	enum SPEAKERCONF {
		SP_AUTO,
		SP_MONO,
		SP_STEREO,
		SP_QUAD,
		SP_51,
		SP_51SIDE,
		SP_61,
		SP_71,
		SP_CUSTOM,
	};

	extern speakerSet _Output;
}


