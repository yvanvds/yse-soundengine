#pragma once
#include "utils/vector.hpp"
#include "headers/enums.hpp"

namespace YSE {
  class reverbimpl;
  class system;

  class API reverb {
  public:
    reverb& create();
    reverb& pos     (const  Vec &value); Vec pos      ();
		reverb& size    (       Flt  value); Flt size     ();
		reverb& rolloff (       Flt  value); Flt rolloff  ();
		reverb& active  (       Bool value); Bool active  ();
		reverb& roomsize(       Flt  value); Flt roomsize ();
		reverb& damp    (       Flt  value); Flt damp     ();
		reverb& wet     (       Flt  value); Flt wet      ();
		reverb& dry     (       Flt  value); Flt dry      ();
		reverb& modFreq (       Flt  value); Flt modFreq  ();
		reverb& modWidth(       Flt  value); Flt modWidth ();

    reverb& reflectionTime(Int reflection, Int value); Int reflectionTime(Int reflection); // reflection must be from 0 to 3
		reverb& reflectionGain(Int reflection, Flt value); Flt reflectionGain(Int reflection);
		
		reverb& preset(REVERB_PRESET value);

    reverb& release();

		reverb();
   ~reverb();

  private:
    reverbimpl * pimpl;
    friend class system;
  };

  extern API reverb GlobalReverb;
}