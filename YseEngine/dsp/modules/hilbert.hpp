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

    /**
     *  @brief Hilbert transform — splits a signal into two outputs 90° out of phase.
     *
     *  Used to build single-side-band modulators, frequency shifters, and
     *  analytic-signal envelope followers. Implemented as two biquad cascades
     *  per channel; ``L1``/``L2`` form the in-phase path, ``R1``/``R2`` the
     *  quadrature path.
     */
    class API hilbert {
    public:
      /** @brief In-phase path biquads (output 1). */
      biQuad L1, L2;
      /** @brief Quadrature path biquads (output 2, 90° lagged). */
      biQuad R1, R2;

      hilbert();

      /**
       *  @brief Run the transform.
       *
       *  @param in   Input mono buffer.
       *  @param out1 In-phase output.
       *  @param out2 Quadrature output (90° lagged from ``out1``).
       */
      void operator()(YSE::DSP::buffer& in, YSE::DSP::buffer& out1, YSE::DSP::buffer& out2);
    };

  } // namespace DSP
} // namespace YSE

#endif // HILBERT_H_INCLUDED
