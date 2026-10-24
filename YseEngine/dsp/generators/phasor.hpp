/*
  ==============================================================================

    phasor.hpp
    A 0..1 phase ramp (issue #1045).

  ==============================================================================
*/

#ifndef YSE_DSP_GENERATORS_PHASOR_HPP
#define YSE_DSP_GENERATORS_PHASOR_HPP

#include "../../headers/defines.hpp"
#include "../../headers/types.hpp"
#include "../../headers/constants.hpp"
#include "../buffer.hpp"

namespace YSE {
  namespace DSP {

    /**
     *  @brief A phase ramp: a sawtooth rising from 0 towards 1 at a given
     *         frequency, then wrapping back to 0.
     *
     *  The phase source behind wavetable reads, sequencers and anything else
     *  that needs to know "where in the cycle" it is. The output is the phase
     *  itself, not a bipolar audio saw, and it is not band-limited: it jumps
     *  from just under 1 to 0 once per cycle. For an antialiased sawtooth use
     *  ``blSaw``.
     *
     *  A negative frequency runs the ramp backwards, from 1 down to 0. The
     *  phase is kept in double precision, so a slow ramp does not drift over
     *  long runs.
     *
     *  The sample rate is read from ``SAMPLERATE`` on every block, so the
     *  ramp keeps its frequency across an engine restart at another rate.
     *
     *  Apart from the constructor and the phase accessors, call it only from
     *  the audio thread. Processing allocates nothing: the output is sized to
     *  ``STANDARD_BUFFERSIZE`` in the constructor, and a block is at most that
     *  long (a longer one is processed up to ``STANDARD_BUFFERSIZE`` samples).
     */
    class API phasor {
    public:
      phasor();

      /** @brief One block at a constant ``frequency`` in Hz. */
      buffer& operator()(Flt frequency, UInt length = STANDARD_BUFFERSIZE);

      /** @brief One block driven by a per-sample frequency in Hz. */
      buffer& operator()(buffer& frequency);

      /**
       *  @brief Jump to a phase now. The value is wrapped into 0..1, so 1.25
       *         and -0.75 both mean 0.25. The next sample produced is this
       *         phase.
       */
      phasor& phase(Flt value);

      /** @brief The phase the next sample will have, in 0..1. */
      Flt phase() const;

    private:
      Dbl current;
      buffer output;
    };

  } // namespace DSP
} // namespace YSE

#endif // YSE_DSP_GENERATORS_PHASOR_HPP
