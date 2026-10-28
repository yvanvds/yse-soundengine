/*
  ==============================================================================

    filterBands.hpp
    The state of a filter editor: up to 24 biquad bands, each set by a
    filter type, a frequency, a gain and a Q, or by its five coefficients
    (issue #1102).

  ==============================================================================
*/

#ifndef YSE_DSP_FILTERS_FILTERBANDS_HPP
#define YSE_DSP_FILTERS_FILTERBANDS_HPP

#include <array>
#include "../../headers/defines.hpp"
#include "../../headers/types.hpp"
#include "../../headers/constants.hpp"
#include "../filters.hpp"
#include "filterCoefficients.hpp"
#include "filterResponse.hpp"

namespace YSE {
  namespace DSP {

    /**
     *  @brief A cascade of up to ``MAX_BANDS`` biquad bands as a filter
     *         editor holds it: each band a filter type with a frequency, a
     *         gain and a Q, or five coefficients as they are, with the
     *         coefficients of the whole cascade for ``biquadCascade``.
     *
     *  Ported from the model behind Max's ``filtergraph~``. A host draws the
     *  bands and the response curve; this class keeps what they mean.
     *
     *  **Bands.** ``bands()`` of the ``MAX_BANDS`` are in use (1 at first);
     *  the others keep their settings for when the count grows again. Each
     *  band has a ``MODE``:
     *
     *  - ``LOWPASS`` .. ``ALLPASS`` (1 .. 9): the coefficients are those of
     *    ``filterCoefficients::Compute`` for the type of the same number,
     *    from the band's frequency in Hz, linear gain and Q (the slope of a
     *    shelf), at the sample rate asked for. With ``gainEnabled`` on, a
     *    lowpass, highpass, bandpass, bandstop or resonant band is the
     *    ``GAIN`` type of the same name, its feedforward scaled by the gain;
     *    off (the default) they ignore the gain. Peak/notch and the shelves
     *    always read it, and so does the allpass, whose gain is its only
     *    effect on the amplitude. With ``analog`` on (off by default), a
     *    bandpass or peak/notch band is ``filterCoefficients::ComputeMatched``
     *    instead, the "imitation analog" form that follows the analog
     *    prototype up to half the sample rate; other modes ignore the flag.
     *  - ``DISPLAY`` (0): the band's coefficients are the five it was given
     *    (``coefficients(band, set)``, ``coefficient``, ``cascade``) and its
     *    frequency, gain and Q change nothing. Switching a band to
     *    ``DISPLAY`` from another mode keeps the coefficients it had then,
     *    at the live sample rate.
     *
     *  **Constraints.** Each band can bound its frequency, gain and Q
     *  (``constraints``); a bound of 0 is none. Every later setting is
     *  clamped into them, and setting them clamps the current values. A
     *  minimum above its maximum is swapped with it.
     *
     *  **Output.** ``coefficients(band)`` gives one band, ``sections()`` the
     *  whole cascade as a flat list of ``a0 a1 a2 b1 b2`` per band in use
     *  (the ``biquadCascade`` form, feedback subtracted). ``bandwidth`` is
     *  the frequency over the Q, in Hz. ``responseAt`` evaluates the
     *  cascade's gain and phase at one frequency; ``response`` fills a
     *  ``filterResponse`` with its curves, which is what a host draws.
     *
     *  A sample rate of 0 (the default everywhere), or any not above 0, means
     *  the live ``SAMPLERATE``, read at the call: nothing is derived from it ahead of
     *  time. A band index at or past ``MAX_BANDS`` is ignored by the setters
     *  and reads as a default band. A setting that is not a finite number is
     *  ignored.
     *
     *  Nothing here allocates; the class is plain data and may be copied.
     *  Call it from one thread at a time.
     */
    class API filterBands {
    public:
      /** @brief What a band is; the numbers are Max's ``mode`` numbers and
       *         those of ``filterCoefficients::TYPE``. */
      enum MODE : int {
        DISPLAY = 0,
        LOWPASS = 1,
        HIGHPASS = 2,
        BANDPASS = 3,
        BANDSTOP = 4,
        PEAKNOTCH = 5,
        LOWSHELF = 6,
        HIGHSHELF = 7,
        RESONANT = 8,
        ALLPASS = 9,
      };

      /** @brief The number of modes. */
      static constexpr int MODE_COUNT = 10;

      /** @brief The most bands. */
      static constexpr UInt MAX_BANDS = 24;

      /** @brief The five coefficients of a band, as ``biQuad`` takes them. */
      using coefficientSet = biQuad::coefficientSet;

      /** @brief The Q of a flat lowpass or highpass (Butterworth). */
      static constexpr Flt FLAT_Q = 0.70710678f;

      /** @brief One band in use. Every band starts as a lowpass at 1000 Hz,
       *         gain 1, Q ``FLAT_Q``, gain off, analog off, no constraints, and as
       *         coefficients ``1 0 0 0 0`` (the input unchanged). */
      filterBands();

      /** @brief How many bands are in use, clamped to 1 .. ``MAX_BANDS``. */
      filterBands& bands(UInt count);

      /** @brief The mode of @p band. A mode out of range is ignored. */
      filterBands& mode(UInt band, MODE value);

      /** @brief Frequency in Hz, gain and Q of @p band, each clamped into
       *         its constraints; a value that is not finite is left as it
       *         was. */
      filterBands& parameters(UInt band, Flt frequency, Flt gain, Flt q);

      /** @brief The frequency of @p band in Hz. */
      filterBands& frequency(UInt band, Flt hz);

      /** @brief The linear gain of @p band. */
      filterBands& gain(UInt band, Flt value);

      /** @brief The Q, or the shelf slope, of @p band. */
      filterBands& q(UInt band, Flt value);

      /** @brief Whether a lowpass, highpass, bandpass, bandstop or resonant
       *         @p band reads its gain. */
      filterBands& gainEnabled(UInt band, Bool on);

      /** @brief Whether a bandpass or peak/notch @p band uses the matched
       *         "imitation analog" design (Max's ``analog``; issue #1209). */
      filterBands& analog(UInt band, Bool on);

      /** @brief Whether a host shows a draggable handle on a ``DISPLAY``
       *         band (Max's ``displaydot``). Stored only. */
      filterBands& handle(UInt band, Bool on);

      /** @brief Bounds on the frequency, the gain and the Q of @p band; 0
       *         for none. Clamps the current values into them. */
      filterBands& constraints(UInt band, Flt minFrequency, Flt maxFrequency, Flt minGain,
                               Flt maxGain, Flt minQ, Flt maxQ);

      /** @brief The five coefficients a ``DISPLAY`` @p band uses. Ignored
       *         when one is not finite. */
      filterBands& coefficients(UInt band, const coefficientSet& set);

      /** @brief Coefficient @p index (0 .. 4: a0 a1 a2 b1 b2) of a
       *         ``DISPLAY`` @p band. */
      filterBands& coefficient(UInt band, UInt index, Flt value);

      /** @brief A cascade given as coefficients: @p count values in sets of
       *         five set as many bands (at most ``MAX_BANDS``) to ``DISPLAY``
       *         with those coefficients and make them the bands in use. An
       *         incomplete trailing set is ignored. Without one complete set,
       *         or with a value that is not finite, nothing changes.
       *  @return The bands set, or 0. */
      UInt cascade(const Flt* values, UInt count);

      /** @brief Max's ``flat``: a lowpass or highpass @p band gets Q
       *         ``FLAT_Q`` and gain 1, a bandpass or bandstop is left alone,
       *         any other gets gain 1 (all within its constraints). */
      filterBands& flat(UInt band);

      /** @brief How many bands are in use. */
      UInt bands() const {
        return count;
      }

      /** @brief The mode of @p band. */
      MODE mode(UInt band) const;
      /** @brief The frequency of @p band in Hz. */
      Flt frequency(UInt band) const;
      /** @brief The gain of @p band. */
      Flt gain(UInt band) const;
      /** @brief The Q of @p band. */
      Flt q(UInt band) const;
      /** @brief Whether @p band reads its gain (see the setter). */
      Bool gainEnabled(UInt band) const;
      /** @brief Whether @p band uses the analog design (see the setter). */
      Bool analog(UInt band) const;
      /** @brief Whether @p band shows a handle in ``DISPLAY`` mode. */
      Bool handle(UInt band) const;
      /** @brief Constraint @p which of @p band: 0 .. 5 for the minimum and
       *         maximum frequency, gain and Q; 0 is none. */
      Flt constraint(UInt band, UInt which) const;

      /** @brief The frequency of @p band over its Q (clamped as
       *         ``filterCoefficients`` clamps it), in Hz. */
      Flt bandwidth(UInt band) const;

      /** @brief The coefficients of @p band at @p sampleRate (0: the live
       *         rate). */
      coefficientSet coefficients(UInt band, Flt sampleRate = 0.f) const;

      /** @brief The coefficients of the bands in use, five per band, into
       *         @p out, as many whole bands as @p capacity holds.
       *  @return The number of values written. */
      UInt sections(Flt* out, UInt capacity, Flt sampleRate = 0.f) const;

      /** @brief The gain (linear) and the phase (radians, -pi .. pi) of the
       *         cascade at @p hz. False, changing nothing, for a frequency
       *         that is not finite, or when the live sample rate is 0. */
      Bool responseAt(Flt hz, Flt& amplitude, Flt& phase, Flt sampleRate = 0.f) const;

      /** @brief Analyses the cascade into @p curves (its magnitude, phase,
       *         delays, impulse and step response). False when the
       *         analysis fails. */
      Bool response(filterResponse& curves, Flt sampleRate = 0.f) const;

    private:
      struct band {
        MODE mode = LOWPASS;
        Flt frequency = 1000.f;
        Flt gain = 1.f;
        Flt q = FLAT_Q;
        Bool gainEnabled = false;
        Bool analog = false;
        Bool handle = false;
        // min/max frequency, gain, Q; 0 is none.
        std::array<Flt, 6> limits{};
        coefficientSet raw{1.f, 0.f, 0.f, 0.f, 0.f};
      };

      // Clamps `value` into limits[2 * which] .. limits[2 * which + 1].
      static Flt Bound(const band& b, UInt which, Flt value);

      std::array<band, MAX_BANDS> slots{};
      UInt count = 1;
    };

  } // namespace DSP
} // namespace YSE

#endif // YSE_DSP_FILTERS_FILTERBANDS_HPP
