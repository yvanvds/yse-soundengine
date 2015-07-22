/*
  ==============================================================================

    hilbert.h
    Created: 31 Jan 2014 2:56:08pm
    Author:  yvan

  ==============================================================================
*/

#ifndef HILBERT_H_INCLUDED
#define HILBERT_H_INCLUDED

#include "../../headers/defines.hpp"
#include "../filters.hpp"

namespace YSE {
  namespace DSP {

    class API hilbert {
    public:
      biQuad L1, L2;
      biQuad R1, R2;

      hilbert();
      void operator()(YSE::DSP::buffer & in, YSE::DSP::buffer & out1, YSE::DSP::buffer & out2);
    };

  }
}



#endif  // HILBERT_H_INCLUDED
