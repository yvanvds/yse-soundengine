/*
  ==============================================================================

    scale.hpp
    Created: 10 Apr 2015 5:14:25pm
    Author:  yvan

  ==============================================================================
*/

#ifndef SCALE_HPP_INCLUDED
#define SCALE_HPP_INCLUDED

#include "../headers/defines.hpp"
#include "../headers/types.hpp"
#include <vector>

namespace YSE {

  namespace MUSIC {

    class API scale {
    public:
      // Add a pitch to the scale. By default it will be added at every
      // octave, but this can be changed with the step value.
      scale & add(Flt pitch, Flt step = 12);
      
      // Scales should be sorted before they are used.
      scale & sort();

      // Check if a pitch is part of the scale
      Bool has(Flt pitch);

      // Get the nearest match for this pitch
      Flt getNearest(Flt pitch);

    private:
      std::vector<Flt> pitches;
    };

  }

}



#endif  // SCALE_HPP_INCLUDED
