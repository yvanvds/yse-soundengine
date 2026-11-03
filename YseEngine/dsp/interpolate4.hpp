/*
  ==============================================================================

    interpolate4.hpp
    Created: 4 Sep 2015 11:53:08am
    Author:  yvan

  ==============================================================================
*/

#ifndef INTERPOLATE4_HPP_INCLUDED
#define INTERPOLATE4_HPP_INCLUDED

#include "buffer.hpp"

namespace YSE {
  namespace DSP {

    /**
     *  @brief 4-point cubic interpolator.
     *
     *  Reads from a source ``buffer`` at fractional positions and produces
     *  smoothly interpolated output. Used internally for pitch-shifting and
     *  wavetable lookups; expose it directly when you need to resample or
     *  re-pitch a buffer at user-defined positions.
     */
    class API interpolate4 {
    public:
      interpolate4();

      /** @brief Set the source buffer to read from. */
      interpolate4& source(buffer& data);

      /** @brief Currently attached source buffer. */
      buffer* source();

      /** @brief Set the read-offset relative to the start of the source. */
      interpolate4& onset(Int value);

      /** @brief Current read-offset. */
      Int onset();

      /** @brief Resample ``in`` (treated as fractional positions) through the cubic kernel. */
      buffer& operator()(YSE::DSP::buffer& in);

      /**
       *  @brief The 4-point cubic kernel itself: the value at @p frac (0 to
       *         1) between @p b and @p c, with @p a before @p b and @p d
       *         after @p c.
       *
       *  Shared by ``operator()`` here and ``sampleView::cubic``, which
       *  the buffer players read with (issue #1140), so both sound alike.
       *  Exact at the samples: ``frac`` 0 gives @p b and 1 gives @p c.
       */
      static Flt kernel(Flt a, Flt b, Flt c, Flt d, Flt frac) {
        const Flt cminusb = c - b;
        return b +
               frac * (cminusb - 0.1666667f * (1.f - frac) *
                                     ((d - a - 3.f * cminusb) * frac + (d + 2.f * a - 3.f * b)));
      }

    private:
      buffer* data;
      buffer out;
      aInt parmOnset;
    };

  } // namespace DSP
} // namespace YSE

#endif // INTERPOLATE4_HPP_INCLUDED
