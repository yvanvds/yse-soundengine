#pragma once
#include "utils/vector.hpp"
#include "headers/enums.hpp"

namespace YSE {

	class reverbimpl {
  public:
		reverbimpl& pos     (const  Vec &value); Vec pos      ();
		reverbimpl& size    (       Flt  value); Flt size     ();
		reverbimpl& rolloff (       Flt  value); Flt rolloff  ();
		reverbimpl& active  (       Bool value); Bool active  ();
		reverbimpl& roomsize(       Flt  value); Flt roomsize ();
		reverbimpl& damp    (       Flt  value); Flt damp     ();
		reverbimpl& wet     (       Flt  value); Flt wet      ();
		reverbimpl& dry     (       Flt  value); Flt dry      ();
		reverbimpl& modFreq (       Flt  value); Flt modFreq  ();
		reverbimpl& modWidth(       Flt  value); Flt modWidth ();

		reverbimpl& reflectionTime(Int reflection, Int value); Int reflectionTime(Int reflection); // reflection must be from 0 to 3
		reverbimpl& reflectionGain(Int reflection, Flt value); Flt reflectionGain(Int reflection);
		
		reverbimpl& preset(REVERB_PRESET value);

		reverbimpl();
		reverbimpl(const reverbimpl & cp);
   ~reverbimpl();

		reverbimpl& operator+=(const reverbimpl& cp);
		reverbimpl& operator*=(Flt value);
    reverbimpl operator*(Flt value);
    reverbimpl& operator/=(Flt value);
	
		Bool _active;
		Flt _roomsize, _damp, _wet, _dry;
		
		Flt _modFrequency,_modWidth; // modulation
	
		Int _earlyPtr [4]; // early reflections
		Flt _earlyGain[4];

		Vec _position;
		Flt _size, _rolloff;
    Bool _release;

    REVERB_PRESET _preset;

		Bool global;
    reverbimpl ** link;
	};

}
