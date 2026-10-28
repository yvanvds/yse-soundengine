/*
  ==============================================================================

    filterResponse.hpp
    The characteristics of a cascade of biquads: magnitude, phase, phase
    delay, group delay, impulse and step response (issue #1101).

  ==============================================================================
*/

#ifndef YSE_DSP_FILTERS_FILTERRESPONSE_HPP
#define YSE_DSP_FILTERS_FILTERRESPONSE_HPP

#include <vector>
#include "../../headers/defines.hpp"
#include "../../headers/types.hpp"
#include "filterDesign.hpp"

namespace YSE {
  namespace DSP {

    /**
     *  @brief Computes what a filter does: its frequency response and its
     *         response in time, from the second-order sections that
     *         ``biquadCascade`` runs.
     *
     *  Ported from Max's ``filterdetail``. ``analyse()`` takes a flat list of
     *  coefficients in sets of five, ``a0 a1 a2 b1 b2`` per section (the
     *  ``biquadCascade`` form, feedback coefficients subtracted), or a
     *  ``filterDesign``, and fills six curves of ``points()`` values each:
     *
     *  - ``magnitude()``: the gain ``|H|``, linear (1 is unity), at the
     *    frequencies ``frequency(i) = pi * i / points()`` radians per
     *    sample, from 0 up to just below the Nyquist frequency.
     *  - ``phase()``: the phase of ``H`` in radians at those frequencies,
     *    unwrapped (no jumps of 2 pi between neighbouring points), starting
     *    within -pi .. pi.
     *  - ``phaseDelay()``: ``-phase / frequency``, in samples. At frequency
     *    0, where that is 0 / 0, it is the group delay there.
     *  - ``groupDelay()``: ``-d phase / d frequency``, in samples, computed
     *    exactly per section rather than by differences. Where a section's
     *    numerator is exactly zero (a zero on the unit circle) it counts as
     *    half its degree, its group delay everywhere else when its zeroes
     *    lie on the unit circle, as a filter design's do.
     *  - ``impulse()``: the first ``points()`` samples of the output for an
     *    input of 1 followed by zeroes.
     *  - ``step()``: the first ``points()`` samples of the output for an
     *    input of 1 held.
     *
     *  Everything is computed in double precision and stored as ``Flt``.
     *
     *  At most ``MAX_SECTIONS`` sections are read; an incomplete trailing
     *  set is ignored. ``analyse()`` returns false and changes nothing when
     *  there is not one complete set or a coefficient is not a finite
     *  number. ``analyse(const filterDesign&)`` reads the design's sections
     *  in double precision.
     *
     *  **Memory.** The constructor allocates the curves for ``capacity``
     *  points; ``points()`` chooses how many of them are used, and nothing
     *  allocates after construction. The class is not thread-safe; call it
     *  from one thread at a time.
     */
    class API filterResponse {
    public:
      /** @brief The capacity, and the number of points, when none is
       *         given. */
      static constexpr UInt DEFAULT_POINTS = 512;

      /** @brief The largest capacity; larger ones are clamped to it. */
      static constexpr UInt MAX_POINTS = 16384;

      /** @brief The most sections read. */
      static constexpr UInt MAX_SECTIONS = 64;

      /**
       *  @brief Room for @p capacity points per curve, all of them used.
       *         Nothing analysed yet: every curve is 0.
       *
       *  Allocates.
       *
       *  @param capacity  Points, clamped to 1 .. ``MAX_POINTS``.
       */
      explicit filterResponse(UInt capacity = DEFAULT_POINTS);

      /** @brief Sets the number of points the next ``analyse()`` computes,
       *         clamped to 1 .. ``capacity()``. */
      filterResponse& points(UInt count);

      /** @brief The number of points per curve. */
      UInt points() const {
        return used;
      }

      /** @brief The most points per curve. */
      UInt capacity() const {
        return static_cast<UInt>(magnitudes.size());
      }

      /** @brief Analyses the sections in @p values, @p count values in sets
       *         of five. False, changing nothing, without one complete set
       *         or with a value that is not finite. */
      Bool analyse(const Flt* values, UInt count);

      /** @brief As the ``Flt`` overload, in double precision. */
      Bool analyse(const Dbl* values, UInt count);

      /** @brief Analyses the sections of @p design. False, changing
       *         nothing, when it has none. */
      Bool analyse(const filterDesign& design);

      /** @brief The sections of the last analysis. */
      UInt sections() const {
        return sectionCount;
      }

      /** @brief The frequency of point @p index, in radians per sample. */
      Dbl frequency(UInt index) const;

      /** @brief The magnitude curve, ``points()`` values. */
      const Flt* magnitude() const {
        return magnitudes.data();
      }

      /** @brief The unwrapped phase curve, in radians. */
      const Flt* phase() const {
        return phases.data();
      }

      /** @brief The phase delay curve, in samples. */
      const Flt* phaseDelay() const {
        return phaseDelays.data();
      }

      /** @brief The group delay curve, in samples. */
      const Flt* groupDelay() const {
        return groupDelays.data();
      }

      /** @brief The impulse response. */
      const Flt* impulse() const {
        return impulses.data();
      }

      /** @brief The step response. */
      const Flt* step() const {
        return steps.data();
      }

    private:
      Bool Analyse(UInt count);

      // The sections being analysed, double precision.
      std::vector<Dbl> coefficientsIn;
      UInt sectionCount = 0;
      UInt used;

      std::vector<Flt> magnitudes;
      std::vector<Flt> phases;
      std::vector<Flt> phaseDelays;
      std::vector<Flt> groupDelays;
      std::vector<Flt> impulses;
      std::vector<Flt> steps;
    };

  } // namespace DSP
} // namespace YSE

#endif // YSE_DSP_FILTERS_FILTERRESPONSE_HPP
