/*
  ==============================================================================

    normalize.hpp
    Scales a signal so its running peak meets a target amplitude: level
    matching (issue #1111).

  ==============================================================================
*/

#ifndef YSE_DSP_MODULES_NORMALIZE_HPP
#define YSE_DSP_MODULES_NORMALIZE_HPP

#include "../../headers/defines.hpp"
#include "../../headers/types.hpp"
#include "../../headers/constants.hpp"
#include "../buffer.hpp"

namespace YSE {
  namespace DSP {

    /**
     *  @brief Normalizes a signal in real time: scales every sample by the
     *         target amplitude divided by the largest input amplitude so
     *         far.
     *
     *  Ported from MSP's ``normalize~``. The *peak* is the largest absolute
     *  input sample since construction, ``reset()`` or the last ``peak()``
     *  setting, and never less than ``MIN_PEAK``. Each sample first updates
     *  the peak and is then scaled, so the output never exceeds the target
     *  in magnitude, and the sample that sets a new peak comes out at
     *  exactly the target (with its sign). The peak only rises: a quieter
     *  passage after a loud one stays quiet. Output is
     *  ``in / peak * amplitude``, divided first so that a tiny peak cannot
     *  overflow the factor.
     *
     *  The target *amplitude* may be any finite number; a negative one
     *  inverts the signal. A value that is not finite is ignored. The
     *  two-buffer call reads it per sample; the getter keeps the last value
     *  read.
     *
     *  ``peak()`` sets the peak directly: its magnitude, raised to
     *  ``MIN_PEAK``, so 0 starts over like ``reset()``. A setting that is not
     *  finite is ignored. A higher input sample raises it again.
     *
     *  An input sample that is not a finite number does not touch the peak
     *  and gives 0, so a stray NaN or infinity cannot latch the output at 0
     *  for good. The class counts no time, so it has no sample-rate state.
     *
     *  Each call processes at most ``STANDARD_BUFFERSIZE`` samples. Call it
     *  from one thread at a time. Processing allocates nothing: the output
     *  is sized to ``STANDARD_BUFFERSIZE`` in the constructor.
     */
    class API normalize {
    public:
      /** @brief The target amplitude until it is set. */
      static constexpr Flt DEFAULT_AMPLITUDE = 1.f;
      /** @brief The lowest peak: the peak after construction and ``reset()``. */
      static constexpr Flt MIN_PEAK = 0.000001f;

      normalize();

      /** @brief The amplitude the peak is scaled to. Not finite: ignored. */
      normalize& amplitude(Flt value);

      /** @brief The target amplitude as last set or read. */
      Flt amplitude() const {
        return target;
      }

      /** @brief Sets the peak to the magnitude of @p value, at least
       *         ``MIN_PEAK``. Not finite: ignored. */
      normalize& peak(Flt value);

      /** @brief The largest input amplitude so far, at least ``MIN_PEAK``. */
      Flt peak() const {
        return top;
      }

      /** @brief Sets the peak to ``MIN_PEAK``, as after construction. Keeps
       *         the amplitude. */
      normalize& reset();

      /** @brief One sample. */
      Flt process(Flt in);

      /** @brief One block at the current amplitude. */
      buffer& operator()(buffer& in);

      /** @brief One block with the amplitude read per sample from
       *         @p amplitudeSignal; as long as the shorter. It also sets the
       *         getter. */
      buffer& operator()(buffer& in, buffer& amplitudeSignal);

    private:
      Flt Step(Flt in);

      buffer output;

      Flt target = DEFAULT_AMPLITUDE;
      Flt top = MIN_PEAK;
    };

  } // namespace DSP
} // namespace YSE

#endif // YSE_DSP_MODULES_NORMALIZE_HPP
