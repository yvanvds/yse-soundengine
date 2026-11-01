/*
  ==============================================================================

    overdrive.hpp
    A soft-clipping waveshaper with a drive factor: warm saturation of a
    signal (issue #1112).

  ==============================================================================
*/

#ifndef YSE_DSP_OPERATORS_OVERDRIVE_HPP
#define YSE_DSP_OPERATORS_OVERDRIVE_HPP

#include "../../headers/defines.hpp"
#include "../../headers/types.hpp"
#include "../../headers/constants.hpp"
#include "../buffer.hpp"

namespace YSE {
  namespace DSP {

    /**
     *  @brief A soft clipper: amplifies a signal by a drive factor and
     *         rounds its peaks off into -1..1.
     *
     *  Ported from MSP's ``overdrive~``. With the drive ``d`` an input
     *  sample ``x`` with ``|x| < 1`` gives ``y = sign(x) * (1 - (1 - |x|)^d)``
     *  and a sample with ``|x| >= 1`` gives ``sign(x)``. The curve is odd,
     *  passes through 0 with a slope of ``d`` and reaches -1 and 1 at the
     *  ends of the range:
     *
     *  - ``d = 1`` is linear: a sample inside -1..1 passes unchanged (bit
     *    for bit), one outside is clipped to -1 or 1.
     *  - ``d > 1`` amplifies small samples by about ``d`` and bends the
     *    curve towards the ends: the peaks are rounded off and meet -1 and
     *    1 without a corner. The usual range is 1 to 10.
     *  - ``0 <= d < 1`` bends the curve the other way, pushing small samples
     *    towards 0 with a sharp corner at the ends: a heavy, harsh
     *    distortion. At 0 every sample inside -1..1 gives 0.
     *  - ``d < 0`` turns the curve over: inside -1..1 the result has the
     *    opposite sign and is limited to -1..1, an extreme distortion.
     *
     *  The result is always within -1..1. A drive that is not a finite
     *  number is ignored. An input sample that is NaN gives 0, an infinite
     *  one -1 or 1. There is no state between samples and no time is
     *  counted, so the class has no sample-rate state: it follows the live
     *  rate by construction.
     *
     *  The two-buffer call reads the drive per sample; the getter keeps the
     *  last value read.
     *
     *  Each call processes at most ``STANDARD_BUFFERSIZE`` samples. Call it
     *  from one thread at a time. Processing allocates nothing: the output
     *  is sized to ``STANDARD_BUFFERSIZE`` in the constructor.
     */
    class API overdrive {
    public:
      /** @brief The drive until it is set: linear. */
      static constexpr Flt DEFAULT_DRIVE = 1.f;

      overdrive();

      /** @brief The drive factor. Not finite: ignored. */
      overdrive& drive(Flt value);

      /** @brief The drive as last set or read. */
      Flt drive() const {
        return factor;
      }

      /** @brief One sample. */
      Flt process(Flt in) const;

      /** @brief One block at the current drive. */
      buffer& operator()(buffer& in);

      /** @brief One block with the drive read per sample from
       *         @p driveSignal; as long as the shorter. It also sets the
       *         getter. */
      buffer& operator()(buffer& in, buffer& driveSignal);

    private:
      buffer output;

      Flt factor = DEFAULT_DRIVE;
    };

  } // namespace DSP
} // namespace YSE

#endif // YSE_DSP_OPERATORS_OVERDRIVE_HPP
