/*
  ==============================================================================

    ladderFilter.hpp
    Moog-style 4-pole resonant low-pass ladder filter (issue #175).

    Reusable standalone DSP primitive: a virtual-analog transistor-ladder
    low-pass with cutoff, resonance and self-oscillation. Used by
    SYNTH::vaVoice, but has no synth dependency and can be dropped into any
    DSP path on its own.

    ---------------------------------------------------------------------------
    Attribution

    This is a clean-room implementation of the zero-delay-feedback (ZDF /
    "topology-preserving transform", TPT) trapezoidal ladder described in
    Vadim Zavalishin, "The Art of VA Filter Design" (freely available, rev.
    2.1.2, chapter 5 — the transistor ladder filter). No third-party source
    code was copied; the algorithm is derived from the freely published
    equations. The original Moog transistor-ladder topology (US patent
    3,475,623) expired in 1986 and is unencumbered. The engine deliberately
    avoids the LGPL Huovilainen model referenced in the MoogLadders project.
    ---------------------------------------------------------------------------

  ==============================================================================
*/

#ifndef YSE_DSP_LADDERFILTER_HPP
#define YSE_DSP_LADDERFILTER_HPP

#include "../headers/defines.hpp"
#include "../headers/types.hpp"
#include "buffer.hpp"

namespace YSE {
  namespace DSP {

    /**
     *  @brief Moog-style 4-pole (24 dB/oct) resonant low-pass ladder filter.
     *
     *  A zero-delay-feedback trapezoidal ladder: four cascaded one-pole
     *  low-pass stages wrapped in a resonant feedback loop, with a saturating
     *  nonlinearity in the loop for stability. At maximum resonance the loop
     *  self-oscillates at the cutoff frequency, giving the classic sine-like
     *  tone used for lead and bass patches.
     *
     *  Cutoff changes are glided internally (a short one-pole smoother on the
     *  filter coefficient) so fast sweeps stay click-free — set a new cutoff
     *  every block without zipper noise.
     *
     *  Real-time safe: allocates nothing, locks nothing. Construct off the
     *  audio thread; ``process`` / ``operator()`` run on the audio thread.
     *  Not thread-safe for concurrent use of a single instance.
     */
    class API ladderFilter {
    public:
      /** @brief Construct with a default cutoff (well below Nyquist) and zero resonance. */
      ladderFilter();

      /** @brief Set the target cutoff frequency in Hz (glided internally). */
      void setCutoff(Flt hz);

      /** @brief Current (target) cutoff frequency in Hz. */
      Flt getCutoff() const {
        return cutoffHz;
      }

      /**
       *  @brief Set the resonance in [0, 1].
       *
       *  0 is no resonance; 1 pushes the feedback past the self-oscillation
       *  threshold so the filter sings at the cutoff frequency. Values are
       *  clamped to [0, 1].
       */
      void setResonance(Flt r);

      /** @brief Current resonance in [0, 1]. */
      Flt getResonance() const {
        return resonance;
      }

      /** @brief Clear the filter state and snap the coefficient glide to the target. */
      void reset();

      /** @brief Process one sample. Audio-thread only. */
      Flt process(Flt x);

      /** @brief Process a whole block in place. Audio-thread only. */
      buffer& operator()(buffer& in);

    private:
      void computeTargetG();

      Flt cutoffHz; // target cutoff (Hz)
      Flt resonance; // [0,1]
      Flt gTarget; // prewarped coefficient for cutoffHz
      Flt gCur; // smoothed coefficient actually used
      Flt smoothCoef; // per-sample glide coefficient for gCur
      Flt s1, s2, s3, s4; // one-pole integrator states
    };

  } // namespace DSP
} // namespace YSE

#endif // YSE_DSP_LADDERFILTER_HPP
