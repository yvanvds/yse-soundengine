/*
  ==============================================================================

    zPlane.hpp
    A cascade of biquad sections seen as poles and zeroes on the z plane,
    which a host draws and drags (issue #1103).

  ==============================================================================
*/

#ifndef YSE_DSP_FILTERS_ZPLANE_HPP
#define YSE_DSP_FILTERS_ZPLANE_HPP

#include <array>
#include <complex>
#include "../../headers/defines.hpp"
#include "../../headers/types.hpp"
#include "../filters.hpp"

namespace YSE {
  namespace DSP {

    /**
     *  @brief Up to ``MAX_SECTIONS`` biquad sections held as coefficients
     *         and read and edited as poles and zeroes.
     *
     *  Ported from the model behind Max's ``zplane~``. A host draws the
     *  poles and zeroes on the z plane and lets them be dragged; this class
     *  keeps the coefficients they stand for. The conversions are
     *  ``filterDesign::sectionRoots`` and ``filterDesign::sectionFromRoots``.
     *
     *  **Sections.** ``sections()`` of the ``MAX_SECTIONS`` are in use (1 at
     *  first), each ``a0 a1 a2 b1 b2`` in the ``biquadCascade`` form; a new
     *  section is ``1 0 0 0 0``, the input unchanged. ``order()`` is the
     *  number of poles (and of zeroes), two per section.
     *
     *  **Roots.** Section ``s`` owns zeroes and poles ``2 s`` and ``2 s + 1``
     *  (see ``filterDesign::sectionRoots`` for their order) and its gain is
     *  its ``a0``. A section whose ``a0`` is 0 has no pair of finite zeroes;
     *  ``finite()`` is false for it and its zeroes read as 0.
     *
     *  **Editing.** ``moveZero`` and ``movePole`` move one root and keep the
     *  coefficients real: a root moved off the real axis takes its partner
     *  along as its complex conjugate; a root moved onto the real axis
     *  leaves a real partner where it is and takes a conjugate partner
     *  along to the same point. Moving a zero of a section with ``a0`` = 0
     *  gives that section gain 1. With ``constrainPoles`` on, a pole moved
     *  outside a radius of ``MAX_POLE_RADIUS`` is pulled back onto it, so
     *  edits keep the filter stable; coefficients set directly are kept as
     *  they are.
     *
     *  Nothing here depends on the sample rate. Setters ignore values that
     *  are not finite and indices out of range. Nothing allocates; the class
     *  is plain data and may be copied. Call it from one thread at a time.
     */
    class API zPlane {
    public:
      /** @brief The five coefficients of a section, as ``biQuad`` takes them. */
      using coefficientSet = biQuad::coefficientSet;

      /** @brief The most sections. */
      static constexpr UInt MAX_SECTIONS = 24;

      /** @brief The radius ``constrainPoles`` keeps edited poles within. */
      static constexpr Dbl MAX_POLE_RADIUS = 0.9999;

      /** @brief One section in use, ``1 0 0 0 0``. */
      zPlane() = default;

      /** @brief How many sections are in use, clamped to 1 to
       *         ``MAX_SECTIONS``. Sections past the count keep their
       *         coefficients for when it grows again. */
      zPlane& sections(UInt count);

      /** @brief How many sections are in use. */
      UInt sections() const {
        return count;
      }

      /** @brief The number of poles: an odd number is rounded up to whole
       *         sections. Clamped to 2 .. ``2 * MAX_SECTIONS``. */
      zPlane& order(UInt poles);

      /** @brief The number of poles, ``2 * sections()``. */
      UInt order() const {
        return 2 * count;
      }

      /** @brief The coefficients of @p section; ignored when one is not
       *         finite. */
      zPlane& coefficients(UInt section, const coefficientSet& set);

      /** @brief Coefficient @p index (0 .. 4: a0 a1 a2 b1 b2) of
       *         @p section. */
      zPlane& coefficient(UInt section, UInt index, Flt value);

      /** @brief A cascade given as coefficients: @p values in sets of five
       *         set as many sections (at most ``MAX_SECTIONS``) and make them
       *         the sections in use. An incomplete trailing set is ignored.
       *         Without one complete set, or with a value that is not
       *         finite, nothing changes.
       *  @return The sections set, or 0. */
      UInt cascade(const Flt* values, UInt valueCount);

      /** @brief The coefficients of @p section (``1 0 0 0 0`` past
       *         ``MAX_SECTIONS``). */
      coefficientSet coefficients(UInt section) const;

      /** @brief The coefficients of the sections in use, five per section,
       *         as many whole sections as @p capacity holds.
       *  @return The number of values written. */
      UInt coefficients(Flt* out, UInt capacity) const;

      /** @brief Whether @p section has two finite zeroes (its ``a0`` is not
       *         0). */
      Bool finite(UInt section) const;

      /** @brief Zero @p index (0 .. ``order() - 1``); 0 past it. */
      std::complex<Dbl> zero(UInt index) const;

      /** @brief Pole @p index (0 .. ``order() - 1``); 0 past it. */
      std::complex<Dbl> pole(UInt index) const;

      /** @brief The gain of @p section, its ``a0``. */
      Flt gain(UInt section) const;

      /** @brief The zeroes of the sections in use as ``real imaginary``
       *         pairs, as many whole sections (four values each) as
       *         @p capacity holds.
       *  @return The number of values written. */
      UInt zeroes(Flt* out, UInt capacity) const;

      /** @brief The poles, as ``zeroes`` writes the zeroes. */
      UInt poles(Flt* out, UInt capacity) const;

      /** @brief The gain of each section in use.
       *  @return The number of values written. */
      UInt gains(Flt* out, UInt capacity) const;

      /** @brief Moves zero @p index to @p real + i @p imaginary (see the
       *         class description). False, changing nothing, for an index
       *         past ``order()`` or a value that is not finite. */
      Bool moveZero(UInt index, Dbl real, Dbl imaginary);

      /** @brief Moves pole @p index, as ``moveZero`` moves a zero, within
       *         ``MAX_POLE_RADIUS`` when ``constrainPoles`` is on. */
      Bool movePole(UInt index, Dbl real, Dbl imaginary);

      /** @brief Whether ``movePole`` keeps poles inside the unit circle
       *         (Max's ``pconstrain``; off at first). */
      zPlane& constrainPoles(Bool on) {
        constrain = on;
        return *this;
      }

      /** @brief Whether edited poles are kept inside the unit circle. */
      Bool constrainPoles() const {
        return constrain;
      }

    private:
      // Moves root `which` (0 or 1) of `roots` to `to`, keeping the pair real.
      static void Move(std::complex<Dbl> roots[2], UInt which, std::complex<Dbl> to);

      std::array<coefficientSet, MAX_SECTIONS> slots = Identity();
      UInt count = 1;
      Bool constrain = false;

      static std::array<coefficientSet, MAX_SECTIONS> Identity() {
        std::array<coefficientSet, MAX_SECTIONS> s{};
        for (coefficientSet& c : s)
          c.a0 = 1.f;
        return s;
      }
    };

  } // namespace DSP
} // namespace YSE

#endif // YSE_DSP_FILTERS_ZPLANE_HPP
