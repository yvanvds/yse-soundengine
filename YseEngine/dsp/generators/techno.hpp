/*
  ==============================================================================

    techno.hpp
    A step sequencer driven by a phase signal (issue #1068).

  ==============================================================================
*/

#ifndef YSE_DSP_GENERATORS_TECHNO_HPP
#define YSE_DSP_GENERATORS_TECHNO_HPP

#include <vector>
#include "../../headers/defines.hpp"
#include "../../headers/types.hpp"
#include "../../headers/constants.hpp"
#include "../buffer.hpp"

namespace YSE {
  namespace DSP {

    /**
     *  @brief A step sequencer read by a 0..1 phase signal (a ``phasor``):
     *         per step a pitch, an amplitude, a pitch slide and an attack
     *         and decay shape, for drum-machine patterns and acid lines at
     *         sample accuracy.
     *
     *  Ported from Max's ``techno~``.
     *
     *  **Steps.** The sequence has ``length()`` steps (1 to start with), out
     *  of a ``capacity()`` fixed in the constructor. Each step starts at a
     *  ``position()``, a fraction of the whole sequence: by default the
     *  steps are evenly spaced, step ``k`` of ``n`` starting at ``k / n``,
     *  and changing the length spaces them evenly again. A step lasts until
     *  the next one starts; the last one lasts until the first one starts
     *  again (the phase wraps). Per-step values can be set for any step
     *  below the capacity, also beyond the current length, so a longer
     *  sequence can be prepared and then switched to.
     *
     *  **Phase.** The input is read as a phase in the whole sequence,
     *  clamped to 0..1 (a sample that is not a number counts as 0). At each
     *  sample the current step is the last one starting at or before the
     *  phase, and ``f``, 0..1, is how far the phase has gone through it.
     *
     *  **Outputs**, at each sample, with ``k`` the current step, ``p`` the
     *  step before it and ``n`` the one after it (both wrapping around the
     *  sequence):
     *
     *  - the pitch, sliding from the previous step's:
     *    ``pitch[p] + (pitch[k] - pitch[p]) * f^curve[k]``. A curve of 1 is
     *    a linear slide across the whole step, 0.5 a square-root one, 2 a
     *    parabolic one; 0 jumps straight to the step's pitch (no slide).
     *  - ``envelope()``: the step's note decaying, plus the next step's note
     *    rising: ``amplitude[k] * (1 - f)^decay[k] +
     *    amplitude[n] * f^attack[n]``. A note is at its amplitude where its
     *    step starts; it rises over the step before it (``attack``) and dies
     *    away over its own (``decay``), reaching 0 where the next step
     *    starts. The exponents read as the curve's do: 1 is linear, larger
     *    is a quicker decay or a later, sharper attack; 0 holds the note
     *    over the whole step (a decay of 0 never decays; an attack of 0 is
     *    there from the start of the step before). Where both shapes are
     *    gentle the two notes overlap and their sum can exceed either
     *    amplitude.
     *  - ``stepPosition()``: where the sequence is, counted in steps: the
     *    current step plus ``f``, 0 up to the length.
     *
     *  A new step has pitch 440 Hz, amplitude 1, curve 0 (no slide), attack
     *  1 and decay 1. Exponents below 0 count as 0; values that are not
     *  finite numbers are ignored.
     *
     *  Nothing depends on the sample rate: steps are fractions of the phase,
     *  and pitches are passed through in Hz.
     *
     *  Apart from the constructor, call it from one thread at a time, the
     *  audio thread once it renders. Nothing but the constructor allocates:
     *  the step arrays are sized to the capacity and the outputs to
     *  ``STANDARD_BUFFERSIZE``; a block is at most that long (a longer one
     *  is processed up to ``STANDARD_BUFFERSIZE`` samples).
     */
    class API techno {
    public:
      /** @brief The capacity of a default-constructed sequencer. */
      static constexpr UInt DEFAULT_STEPS = 16;
      /** @brief The largest capacity; a larger request is clamped. */
      static constexpr UInt MAX_STEPS = 1024;

      /** @brief A sequencer with room for @p capacity steps (1 to
       *         ``MAX_STEPS``). Allocates. */
      explicit techno(UInt capacity = DEFAULT_STEPS);

      /** @brief How many steps the sequencer has room for. */
      UInt capacity() const {
        return static_cast<UInt>(pitches.size());
      }

      /** @brief Use the first @p steps steps (clamped to 1..capacity) and
       *         space them evenly. */
      techno& length(UInt steps);
      UInt length() const {
        return used;
      }

      /** @brief Step @p step's pitch, in Hz. */
      techno& pitch(UInt step, Flt hz);
      Flt pitch(UInt step) const;

      /** @brief Step @p step's amplitude: the envelope's value where it
       *         starts. */
      techno& amplitude(UInt step, Flt value);
      Flt amplitude(UInt step) const;

      /** @brief The shape of the pitch slide into step @p step. */
      techno& curve(UInt step, Flt exponent);
      Flt curve(UInt step) const;

      /** @brief The shape of step @p step's rise, over the step before it. */
      techno& attack(UInt step, Flt exponent);
      Flt attack(UInt step) const;

      /** @brief The shape of step @p step's decay, over its own length. */
      techno& decay(UInt step, Flt exponent);
      Flt decay(UInt step) const;

      /**
       *  @brief Where step @p step starts, as a fraction of the sequence.
       *
       *  Clamped between where the steps either side of it start (0 for the
       *  first step's lower bound, 1 for the last step's upper bound), so
       *  steps never change order. Ignored for a step at or beyond the
       *  length.
       */
      techno& position(UInt step, Flt start);
      Flt position(UInt step) const;

      /**
       *  @brief Repeat a pattern of step positions over the sequence.
       *
       *  @p starts holds @p count increasing values strictly between 0 and
       *  1: where steps 1 to @p count of a segment of ``count + 1`` steps
       *  start, as fractions of the segment (step 0 of each segment starts
       *  at its beginning). The sequence is cut into ``length / (count +
       *  1)`` equal segments, each spaced this way: with 8 steps,
       *  ``{0.66}`` makes every pair of steps swing two thirds to one
       *  third. Changes nothing and returns false when the length is not a
       *  multiple of ``count + 1`` or the values are out of order or out of
       *  range; ``count`` 0 spaces the steps evenly.
       */
      bool repeatPositions(const Flt* starts, UInt count);

      /** @brief One block: @p phase, 0..1 over the sequence, in; the pitch
       *         out (Hz). */
      buffer& operator()(buffer& phase);

      /** @brief The amplitude envelope of the last block. */
      buffer& envelope() {
        return envelopeOutput;
      }

      /** @brief The position in steps (0 up to the length) of the last
       *         block. */
      buffer& stepPosition() {
        return positionOutput;
      }

    private:
      void Space();

      std::vector<Flt> pitches;
      std::vector<Flt> amplitudes;
      std::vector<Flt> curves;
      std::vector<Flt> attacks;
      std::vector<Flt> decays;
      std::vector<Flt> starts;
      UInt used = 1;
      buffer output;
      buffer envelopeOutput;
      buffer positionOutput;
    };

  } // namespace DSP
} // namespace YSE

#endif // YSE_DSP_GENERATORS_TECHNO_HPP
