/*
  ==============================================================================

    filterDesign.hpp
    Butterworth and Chebyshev filters of a given order, designed into
    second-order sections, zeroes-poles-gain and a transfer function
    (issue #1101).

  ==============================================================================
*/

#ifndef YSE_DSP_FILTERS_FILTERDESIGN_HPP
#define YSE_DSP_FILTERS_FILTERDESIGN_HPP

#include <array>
#include <complex>
#include "../../headers/defines.hpp"
#include "../../headers/types.hpp"
#include "../../headers/constants.hpp"
#include "../filters.hpp"

namespace YSE {
  namespace DSP {

    /**
     *  @brief Designs a classic IIR filter of a given order and hands it out
     *         as second-order sections, the coefficient lists
     *         ``biquadCascade`` runs.
     *
     *  Ported from Max's ``filterdesign``. Set the specification with the
     *  setters, call ``design()``, then read the result in any of three
     *  forms:
     *
     *  - **Second-order sections**: ``sections()`` biquads in the
     *    ``biquadCascade`` / ``biQuad`` form, ``a0 a1 a2 b1 b2`` with the
     *    feedback coefficients subtracted. ``coefficients(out, capacity)``
     *    writes them as one flat list, ready for
     *    ``biquadCascade::coefficients``.
     *  - **Zeroes, poles and gain** of the digital filter: ``zero(i)`` and
     *    ``pole(i)`` for ``i < poles()``, and ``gain()``.
     *  - **Transfer function**: ``numerator(i)`` and ``denominator(i)`` for
     *    ``i <= poles()``, the coefficients of ``z^-i``, with
     *    ``denominator(0)`` = 1. Rounding error grows quickly with the order
     *    in this form; the sections are the form to filter with.
     *
     *  **Design.** The analog prototype (Butterworth, Chebyshev type 1 or
     *  type 2) is transformed to the response (lowpass, highpass, bandpass,
     *  bandstop) at frequencies prewarped for the bilinear transform, then
     *  mapped to the z plane by the bilinear transform: the standard method,
     *  and the one SciPy's ``iirfilter`` uses, so its results serve as a
     *  reference. The frequencies mean:
     *
     *  - Butterworth: the point where the gain is down 3 dB.
     *  - Chebyshev type 1: the edge of the passband, where the gain first
     *    falls below the passband ripple.
     *  - Chebyshev type 2: the edge of the stopband, where the gain first
     *    reaches the stopband attenuation.
     *
     *  **Order** is the number of poles. A lowpass or highpass of order
     *  ``n`` has ``n`` poles. A bandpass or bandstop is built from a
     *  prototype of half the order, rounded up, so an odd order gets one
     *  more pole than asked (``poles()`` says how many). The order is
     *  clamped to 1 .. ``MAX_ORDER``.
     *
     *  **Sections.** Poles are paired with their complex conjugates, real
     *  poles with each other, and each pair takes the nearest zeroes. A
     *  single real pole left over gives one first-order section
     *  (``a2 = b2 = 0``), the first in the list. The other sections follow
     *  in order of their poles' distance to the unit circle, the sharpest
     *  resonance last, and the first section carries the overall gain.
     *
     *  **Frequencies** are read in ``units()``: hertz against
     *  ``sampleRate()``, normalized (1 is the Nyquist frequency) or radians
     *  per sample (pi is the Nyquist frequency). A lowpass or highpass uses
     *  the first frequency, a bandpass or bandstop both (the lower and upper
     *  edge, in either order). A sample rate of 0, the default, is the
     *  engine's ``SAMPLERATE`` at the moment ``design()`` runs, so the
     *  design follows the rate the engine runs at.
     *
     *  ``design()`` returns false, and leaves no sections, when a frequency
     *  it uses is not strictly between 0 and the Nyquist frequency, or when
     *  the two band edges are equal.
     *
     *  Setters ignore values that are not finite numbers, ripple and
     *  attenuation that are not above 0, and a negative sample rate.
     *
     *  **Memory.** Everything is held in fixed arrays inside the object:
     *  nothing allocates, not even ``design()``, which may therefore run on
     *  any thread. The class is not thread-safe; call it from one thread at
     *  a time.
     */
    class API filterDesign {
    public:
      /** @brief The shape of the response. */
      enum class RESPONSE {
        LOWPASS, ///< Low frequencies pass, high frequencies are attenuated.
        HIGHPASS, ///< High frequencies pass, low frequencies are attenuated.
        BANDPASS, ///< A band passes, frequencies below and above it are attenuated.
        BANDSTOP, ///< A band is attenuated, frequencies below and above it pass.
      };

      /** @brief The family of the analog prototype. */
      enum class TOPOLOGY {
        BUTTERWORTH, ///< Flat passband, gentle transition, monotonic stopband.
        CHEBYSHEV_1, ///< Ripple in the passband, the steepest transition.
        CHEBYSHEV_2, ///< Flat passband, ripple in the stopband.
      };

      /** @brief How frequencies are read. */
      enum class UNITS {
        HERTZ, ///< Cycles per second, against ``sampleRate()``.
        NORMALIZED, ///< 0 .. 1, where 1 is the Nyquist frequency.
        RADIANS, ///< 0 .. pi, where pi is the Nyquist frequency.
      };

      /** @brief The highest order, in poles. */
      static constexpr UInt MAX_ORDER = 32;

      /** @brief The most sections a design has. */
      static constexpr UInt MAX_SECTIONS = MAX_ORDER / 2;

      /** @brief A Butterworth lowpass of order 2 at 1000 Hz (band edges
       *         1000 and 2000 Hz), 1 dB passband ripple and 40 dB stopband
       *         attenuation, at the engine's sample rate. Not designed yet:
       *         ``sections()`` is 0 until ``design()`` runs. */
      filterDesign() = default;

      /** @brief Sets the response shape. */
      filterDesign& response(RESPONSE value) {
        shape = value;
        return *this;
      }

      /** @brief The response shape. */
      RESPONSE response() const {
        return shape;
      }

      /** @brief Sets the prototype family. */
      filterDesign& topology(TOPOLOGY value) {
        family = value;
        return *this;
      }

      /** @brief The prototype family. */
      TOPOLOGY topology() const {
        return family;
      }

      /** @brief Sets the order, clamped to 1 .. ``MAX_ORDER``. */
      filterDesign& order(UInt value);

      /** @brief The order asked for. */
      UInt order() const {
        return requestedOrder;
      }

      /** @brief Sets the first frequency (the cutoff, or the lower band
       *         edge). */
      filterDesign& frequency(Flt value);

      /** @brief Sets both frequencies (the band edges). A value that is not
       *         finite leaves that frequency as it was. */
      filterDesign& frequency(Flt first, Flt second);

      /** @brief Frequency @p index (0 or 1), in ``units()``. */
      Flt frequencyAt(UInt index) const {
        return index == 0 ? frequencies[0] : frequencies[1];
      }

      /** @brief Sets how frequencies are read. */
      filterDesign& units(UNITS value) {
        unit = value;
        return *this;
      }

      /** @brief How frequencies are read. */
      UNITS units() const {
        return unit;
      }

      /** @brief Sets the sample rate hertz are read against; 0 is the
       *         engine's ``SAMPLERATE`` when ``design()`` runs. */
      filterDesign& sampleRate(Flt value);

      /** @brief The sample rate set (0: the engine's). */
      Flt sampleRate() const {
        return rate;
      }

      /** @brief Sets the passband ripple of a Chebyshev type 1 filter, in
       *         dB (above 0). */
      filterDesign& passbandRipple(Flt decibels);

      /** @brief The passband ripple, in dB. */
      Flt passbandRipple() const {
        return ripple;
      }

      /** @brief Sets the stopband attenuation of a Chebyshev type 2
       *         filter, in dB (above 0). */
      filterDesign& stopbandAttenuation(Flt decibels);

      /** @brief The stopband attenuation, in dB. */
      Flt stopbandAttenuation() const {
        return attenuation;
      }

      /**
       *  @brief Designs the filter from the current settings.
       *
       *  False, with no sections, when the frequencies cannot be designed
       *  (see the class description). Allocates nothing.
       */
      Bool design();

      /** @brief The sections of the last design (0 before the first or
       *         after a failed one). */
      UInt sections() const {
        return sectionCount;
      }

      /** @brief The coefficients of section @p index; all 0 past
       *         ``sections()``. */
      biQuad::coefficientSet section(UInt index) const;

      /**
       *  @brief Writes the sections as one flat list, five values per
       *         section, into @p out.
       *
       *  At most @p capacity values, in whole sections. Returns how many
       *  were written.
       */
      UInt coefficients(Flt* out, UInt capacity) const;

      /** @brief As the ``Flt`` overload, in double precision. */
      UInt coefficients(Dbl* out, UInt capacity) const;

      /** @brief The poles of the last design, which has as many zeroes. */
      UInt poles() const {
        return rootCount;
      }

      /** @brief Zero @p index of the last design; 0 past ``poles()``. */
      std::complex<Dbl> zero(UInt index) const {
        return index < rootCount ? zeroes[index] : std::complex<Dbl>();
      }

      /** @brief Pole @p index of the last design; 0 past ``poles()``. */
      std::complex<Dbl> pole(UInt index) const {
        return index < rootCount ? polesZ[index] : std::complex<Dbl>();
      }

      /** @brief The overall gain of the last design. */
      Dbl gain() const {
        return gainZ;
      }

      /** @brief Coefficient of ``z^-index`` in the transfer function's
       *         numerator; 0 past ``poles()``. */
      Dbl numerator(UInt index) const {
        return index <= rootCount ? numeratorTF[index] : 0.0;
      }

      /** @brief Coefficient of ``z^-index`` in the transfer function's
       *         denominator (``denominator(0)`` is 1); 0 past ``poles()``. */
      Dbl denominator(UInt index) const {
        return index <= rootCount ? denominatorTF[index] : 0.0;
      }

      /**
       *  @brief The zeroes and poles of one section (issue #1103).
       *
       *  A section ``a0 a1 a2 b1 b2`` (the ``biquadCascade`` form) is
       *  ``a0 (1 - z0 z^-1)(1 - z1 z^-1) / ((1 - p0 z^-1)(1 - p1 z^-1))``:
       *  the zeroes are the roots of ``a0 z^2 + a1 z + a2``, the poles those
       *  of ``z^2 + b1 z + b2``, and ``a0`` is the section's gain. A
       *  first-order section (``a2 = b2 = 0``) has a zero and a pole at the
       *  origin. A complex pair comes upper half first (``zero0.imag() >
       *  0``); a real pair the larger first. Computed in double precision.
       *
       *  @return False when ``a0`` is 0 or a coefficient is not finite: the
       *          numerator then has fewer than two finite zeroes and both
       *          zeroes read as 0. The poles are always written (0 when a
       *          coefficient is not finite).
       */
      static Bool sectionRoots(const biQuad::coefficientSet& set, std::complex<Dbl>& zero0,
                               std::complex<Dbl>& zero1, std::complex<Dbl>& pole0,
                               std::complex<Dbl>& pole1);

      /**
       *  @brief The section with gain @p gain, zeroes @p zero0, @p zero1
       *         and poles @p pole0, @p pole1: the inverse of
       *         ``sectionRoots`` (issue #1103).
       *
       *  ``a0 = gain``, ``a1 = -gain (z0 + z1)``, ``a2 = gain z0 z1``,
       *  ``b1 = -(p0 + p1)``, ``b2 = p0 p1``. Each pair must be two real
       *  roots or a complex conjugate pair for the coefficients to be real;
       *  of any other pair the real parts of the sum and product are taken.
       */
      static biQuad::coefficientSet sectionFromRoots(Dbl gain, std::complex<Dbl> zero0,
                                                     std::complex<Dbl> zero1,
                                                     std::complex<Dbl> pole0,
                                                     std::complex<Dbl> pole1);

    private:
      // Pairs the digital poles and zeroes into sections.
      Bool BuildSections();
      void BuildTransferFunction();
      void Fail();

      RESPONSE shape = RESPONSE::LOWPASS;
      TOPOLOGY family = TOPOLOGY::BUTTERWORTH;
      UNITS unit = UNITS::HERTZ;
      UInt requestedOrder = 2;
      Flt frequencies[2] = {1000.f, 2000.f};
      Flt rate = 0.f;
      Flt ripple = 1.f;
      Flt attenuation = 40.f;

      // The result.
      UInt rootCount = 0;
      std::array<std::complex<Dbl>, MAX_ORDER> zeroes{};
      std::array<std::complex<Dbl>, MAX_ORDER> polesZ{};
      Dbl gainZ = 0.0;
      UInt sectionCount = 0;
      std::array<Dbl, MAX_SECTIONS * 5> sos{};
      std::array<Dbl, MAX_ORDER + 1> numeratorTF{};
      std::array<Dbl, MAX_ORDER + 1> denominatorTF{};
    };

  } // namespace DSP
} // namespace YSE

#endif // YSE_DSP_FILTERS_FILTERDESIGN_HPP
