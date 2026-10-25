/*
  ==============================================================================

    biquadCascade.hpp
    A series of biquad filters run one after the other, each section driven
    by five coefficients (issue #1091).

  ==============================================================================
*/

#ifndef YSE_DSP_FILTERS_BIQUADCASCADE_HPP
#define YSE_DSP_FILTERS_BIQUADCASCADE_HPP

#include <vector>
#include "../../headers/defines.hpp"
#include "../../headers/types.hpp"
#include "../../headers/constants.hpp"
#include "../buffer.hpp"
#include "../filters.hpp"

namespace YSE {
  namespace DSP {

    /**
     *  @brief A cascade of biquad filters in series: the higher-order
     *         filters a filter design splits into second-order sections.
     *
     *  Ported from Max's ``cascade~``. Each section is the coefficient path
     *  of ``biQuad``,
     *
     *  ``y(n) = a0 x(n) + a1 x(n-1) + a2 x(n-2) - b1 y(n-1) - b2 y(n-2)``
     *
     *  in the same naming and signs (the feedback coefficients are
     *  subtracted), and the output of one section is the input of the next.
     *  Everything ``biQuad`` documents for that path holds per section:
     *  double precision state, no stability check, a blown-up section
     *  restarting from silence, non-finite coefficients ignored and
     *  non-finite input samples skipped.
     *
     *  **Coefficients.** ``coefficients(values, count)`` takes a flat list in
     *  sets of five, ``a0 a1 a2 b1 b2`` per section: the first five are the
     *  first section, the next five the second, and so on. The number of
     *  complete sets becomes the number of sections in use, at most
     *  ``capacity()``; an incomplete trailing set and sets beyond the
     *  capacity are ignored, and a list without one complete set changes
     *  nothing. A section that was in use moves to its new coefficients
     *  smoothly while ``smooth()`` is on (as ``biQuad`` smooths, over
     *  ``biQuad::SMOOTH_SAMPLES`` samples); a section that comes into use
     *  starts from silence at its new coefficients at once. A section that
     *  goes out of use is no longer run.
     *
     *  With no section in use the cascade passes its input unchanged (the
     *  empty product of transfer functions is 1).
     *
     *  **clear()** sets the coefficients and the state of every section to 0
     *  (as Max's ``clear``), at once, keeping the number of sections in use:
     *  the output is silent until new coefficients arrive, which then apply
     *  at once, as to sections coming into use.
     *
     *  **bypass()** outputs the input unchanged; **zero()** outputs silence.
     *  Bypass wins over zero. Under either the sections keep running, so
     *  turning them off continues from a state that has followed the input.
     *
     *  **Memory.** The constructor allocates ``capacity`` sections; nothing
     *  allocates after that. To change the capacity, assign a new
     *  ``biquadCascade``, off the audio thread.
     *
     *  Nothing depends on the sample rate. Each block call processes at most
     *  ``STANDARD_BUFFERSIZE`` samples. Call it from one thread at a time;
     *  the setters are for the thread that processes, or between blocks.
     */
    class API biquadCascade {
    public:
      /** @brief The capacity when none is given, in sections. */
      static constexpr UInt DEFAULT_CAPACITY = 16;

      /** @brief The largest capacity, in sections; larger ones are clamped
       *         to it. */
      static constexpr UInt MAX_CAPACITY = 64;

      /**
       *  @brief A cascade with room for @p capacity sections and none in
       *         use (it passes its input).
       *
       *  Allocates.
       *
       *  @param capacity  Sections, clamped to 1 .. ``MAX_CAPACITY``.
       */
      explicit biquadCascade(UInt capacity = DEFAULT_CAPACITY);

      biquadCascade(const biquadCascade&) = default;
      biquadCascade& operator=(const biquadCascade&) = delete;
      biquadCascade(biquadCascade&&) = default;
      biquadCascade& operator=(biquadCascade&&) = default;
      ~biquadCascade() = default;

      /** @brief Sets the coefficients of the sections from @p count values
       *         in sets of five (see the class description). */
      biquadCascade& coefficients(const Flt* values, UInt count);

      /** @brief The coefficients section @p section moves towards; all 0
       *         for a section not in use. */
      biQuad::coefficientSet coefficients(UInt section) const;

      /** @brief The sections in use. */
      UInt sections() const {
        return active;
      }

      /** @brief The sections the cascade has room for. */
      UInt capacity() const {
        return static_cast<UInt>(stages.size());
      }

      /** @brief Turns smoothing of coefficient changes on or off (on by
       *         default). */
      biquadCascade& smooth(Bool on);

      /** @brief Whether coefficient changes are smoothed. */
      Bool smooth() const {
        return smoothing;
      }

      /** @brief Sets the coefficients and the state of every section to 0. */
      biquadCascade& clear();

      /** @brief Outputs the input unchanged while on. */
      biquadCascade& bypass(Bool on) {
        bypassed = on;
        return *this;
      }

      /** @brief Whether the input is passed unchanged. */
      Bool bypass() const {
        return bypassed;
      }

      /** @brief Outputs silence while on (unless bypassed). */
      biquadCascade& zero(Bool on) {
        muted = on;
        return *this;
      }

      /** @brief Whether the output is silenced. */
      Bool zero() const {
        return muted;
      }

      /** @brief One sample through every section in use. */
      Flt process(Flt in);

      /** @brief One block through every section in use. */
      buffer& operator()(buffer& in);

    private:
      // Sets section `index` to five coefficients; `fresh` starts it from
      // silence at them at once.
      void Set(UInt index, const Flt* values, Bool fresh);

      std::vector<biQuad> stages;
      buffer output;
      // Sections run, and of those the ones a new list moves smoothly (all
      // of them, but none after clear()).
      UInt active = 0;
      UInt running = 0;
      Bool smoothing = true;
      Bool bypassed = false;
      Bool muted = false;
    };

  } // namespace DSP
} // namespace YSE

#endif // YSE_DSP_FILTERS_BIQUADCASCADE_HPP
