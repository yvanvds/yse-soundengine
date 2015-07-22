/*
  ==============================================================================

    wavetable.hpp
    Created: 22 Jul 2015 10:51:14am
    Author:  yvan

  ==============================================================================
*/

#ifndef WAVETABLE_HPP_INCLUDED
#define WAVETABLE_HPP_INCLUDED

#include "buffer.hpp"

namespace YSE {

  namespace DSP {

    class API wavetable : public buffer {
    public:
      void createFourierTable(const std::vector<Flt> & harmonics, Int length, Flt phase);

    };

  }
}



#endif  // WAVETABLE_HPP_INCLUDED
