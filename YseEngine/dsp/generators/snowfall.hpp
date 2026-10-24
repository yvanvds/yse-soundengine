/*
  ==============================================================================

    snowfall.hpp
    A particle of one to four values that moves while a 0..1 phase ramp
    runs, starting at the ramp's start and ending at its reset
    (issue #1085).

  ==============================================================================
*/

#ifndef YSE_DSP_GENERATORS_SNOWFALL_HPP
#define YSE_DSP_GENERATORS_SNOWFALL_HPP

#include <cstdint>
#include "../../headers/defines.hpp"
#include "../../headers/types.hpp"
#include "../../headers/constants.hpp"
#include "../buffer.hpp"

namespace YSE {
  namespace DSP {

    /**
     *  @brief A particle of one to four values (its **dimensions**), each
     *         moving at its own speed while an input ramp runs: one voice of
     *         a cloud of overlapping events, each started by its own phasor.
     *
     *  Ported from Max's ``snowfall~``. The input is a 0..1 ramp, usually a
     *  ``phasor``. Each dimension has an output.
     *
     *  **Life.** A particle **starts** when the input rises above 0 from 0
     *  or below, or wraps (falls by more than half, as a phasor's reset from
     *  0.99 to 0.01 does). It **ends** when the input goes to 0 or below, at
     *  a wrap (where the next one starts in the same sample), or when it
     *  reaches a boundary in ``END`` mode. A sample that is not a finite
     *  number counts as the one before it.
     *
     *  **Start.** Each dimension starts at ``initial()`` plus a random
     *  deviation of up to ``initialDeviation()`` either way, kept within the
     *  boundaries (with ``startMode(CURRENT)``, at the value the last
     *  particle left instead, once there was one). Its **velocity**, in units
     *  per second, is ``direction()`` plus a random deviation of up to
     *  ``directionDeviation()`` either way.
     *
     *  **Motion.** Each sample, a dimension moves by its velocity times
     *  ``1 + energyRamp() * input``, over the sample rate: with energy ramp 0
     *  (the default) it moves at a constant speed, with 1 twice as fast at
     *  the end of the ramp as at its start.
     *
     *  **Updates.** Every ``interval()`` ms (plus a random deviation of up
     *  to ``intervalDeviation()`` either way, drawn for each update) the
     *  trajectory is calculated again: each velocity is drawn again around
     *  ``direction()``, keeping the reversals so far, and then reverses with
     *  the probability ``wanderProbability()`` (0..1). Interval 0 or less
     *  makes no updates. The first update comes one interval after the
     *  start.
     *
     *  **Boundaries.** ``minimum()`` and ``maximum()`` (either order) bound
     *  each dimension. On reaching one, ``boundaryMode()`` says:
     *
     *  - ``BOUNCE`` (the default): the value reflects back inside and the
     *    velocity reverses, times ``squish()`` (1 by default; 0.5 halves the
     *    speed at each bounce). The reversal is kept by later updates.
     *  - ``END``: the particle ends at the boundary.
     *  - ``WRAP``: the value continues from the other boundary.
     *  - ``CLIP``: the value stays at the boundary while it pushes on.
     *
     *  **Output.** While a particle lives, each dimension's output maps its
     *  value from ``minimum() .. maximum()`` onto ``scaleMinimum() ..
     *  scaleMaximum()`` (0..1 and 0..1 by default, so unchanged); equal
     *  boundaries give the scale minimum. When it ends, and before the
     *  first one, ``endMode()`` says: ``END_VALUE`` (the default) outputs
     *  ``endValue()`` (0 by default) as it is, ``CURRENT`` holds the last
     *  output (0 before the first particle).
     *
     *  Max leaves the defaults unstated; here they are: direction 1 (the
     *  range 0..1 crossed in a second), every deviation 0, energy ramp 0,
     *  initial 0, minimum 0, maximum 1, scale 0..1, squish 1, wander 0,
     *  end value 0, interval 100 ms, bounce, end value and initial value.
     *
     *  Speeds and intervals follow the live sample rate. Every random draw
     *  comes from one seeded source (``seed()``), in a fixed order, so a
     *  seed repeats a run exactly. Each call processes at most
     *  ``STANDARD_BUFFERSIZE`` samples. Call it from one thread at a time.
     *  Processing allocates nothing: the outputs are sized to
     *  ``STANDARD_BUFFERSIZE`` in the constructor.
     */
    class API snowfall {
    public:
      /** @brief The most dimensions, and outputs. */
      static constexpr UInt MAX_DIMENSIONS = 4;

      /** @brief What a dimension does at a boundary, numbered as Max's
       *         ``boundarymode``. */
      enum BOUNDARY_MODE {
        BOUNCE = 0,
        END = 1,
        WRAP = 2,
        CLIP = 3,
      };

      /** @brief What the output is when a particle ends, numbered as Max's
       *         ``endmode``. */
      enum END_MODE {
        END_VALUE = 0,
        CURRENT_VALUE = 1,
      };

      /** @brief Where a particle starts, numbered as Max's ``startmode``. */
      enum START_MODE {
        INITIAL = 0,
        CURRENT = 1,
      };

      /** @brief A particle of ``dimensions`` values, 1 to 4 (clamped). */
      explicit snowfall(UInt dimensions = 1);

      /** @brief The number of dimensions, and outputs. */
      UInt dimensions() const {
        return count;
      }

      /** @brief Sets the number of dimensions, 1 to 4 (clamped). Allocates
       *         nothing; a dimension added while a particle moves goes on
       *         from the value it last had. */
      snowfall& dimensions(UInt n);

      /** @brief Starting velocity of dimension ``d``, in units per second. */
      snowfall& direction(UInt d, Flt unitsPerSecond);
      /** @brief The random deviation around the velocity, either way. */
      snowfall& directionDeviation(UInt d, Flt unitsPerSecond);
      /** @brief How much the input ramp speeds the motion up. */
      snowfall& energyRamp(UInt d, Flt amount);
      /** @brief The starting value. */
      snowfall& initial(UInt d, Flt value);
      /** @brief The random deviation around the starting value, either way. */
      snowfall& initialDeviation(UInt d, Flt value);
      /** @brief The lower boundary. */
      snowfall& minimum(UInt d, Flt value);
      /** @brief The upper boundary. */
      snowfall& maximum(UInt d, Flt value);
      /** @brief The output at the lower boundary. */
      snowfall& scaleMinimum(UInt d, Flt value);
      /** @brief The output at the upper boundary. */
      snowfall& scaleMaximum(UInt d, Flt value);
      /** @brief The factor a bounce multiplies the velocity by. */
      snowfall& squish(UInt d, Flt factor);
      /** @brief The probability, 0..1 (clamped), of a reversal at an update. */
      snowfall& wanderProbability(UInt d, Flt probability);
      /** @brief The output once a particle ends, in ``END_VALUE`` mode. */
      snowfall& endValue(UInt d, Flt value);

      /** @brief The time between trajectory updates, in ms; 0 or less makes
       *         none. */
      snowfall& interval(Flt ms);
      /** @brief The random deviation around the interval, either way, in ms
       *         (0 or more). */
      snowfall& intervalDeviation(Flt ms);
      /** @brief What a dimension does at a boundary. */
      snowfall& boundaryMode(BOUNDARY_MODE m);
      /** @brief What the output is when a particle ends. */
      snowfall& endMode(END_MODE m);
      /** @brief Where a particle starts. */
      snowfall& startMode(START_MODE m);
      /** @brief Seeds the random source. */
      snowfall& seed(std::uint32_t value);

      /** @brief Whether a particle is moving. */
      bool alive() const {
        return living;
      }

      /** @brief The value of dimension ``d``, before scaling. */
      Flt value(UInt d) const {
        return d < count ? static_cast<Flt>(dims[d].x) : 0.f;
      }

      /** @brief The velocity of dimension ``d``, in units per second. */
      Flt velocity(UInt d) const {
        return d < count ? static_cast<Flt>(dims[d].v) : 0.f;
      }

      /** @brief One sample: writes ``dimensions()`` outputs to ``out``. */
      void process(Flt ramp, Flt* out);

      /** @brief One block; returns the first output, ``output()`` has them
       *         all. */
      buffer& operator()(buffer& ramp);

      /** @brief Output ``d`` of the last block (the last one past the
       *         dimensions). */
      buffer& output(UInt d) {
        return outputs[d < count ? d : count - 1];
      }

    private:
      struct dimension {
        Flt direction = 1.f;
        Flt directionDeviation = 0.f;
        Flt energy = 0.f;
        Flt initial = 0.f;
        Flt initialDeviation = 0.f;
        Flt minimum = 0.f;
        Flt maximum = 1.f;
        Flt scaleMinimum = 0.f;
        Flt scaleMaximum = 1.f;
        Flt squish = 1.f;
        Flt wander = 0.f;
        Flt endValue = 0.f;
        Dbl x = 0.0;
        Dbl v = 0.0;
        bool reversed = false;
        Flt held = 0.f;
      };

      Dbl Random(); // [-1, 1)
      Dbl Probability(); // [0, 1)
      Dbl NextInterval();
      void Start();
      void Update();
      // Moves dimension `d`; false when it reached a boundary in END mode.
      bool Move(dimension& d, Dbl ramp, Dbl rate);
      Flt Scaled(const dimension& d) const;

      dimension dims[MAX_DIMENSIONS];
      buffer outputs[MAX_DIMENSIONS];
      UInt count = 1;
      Flt intervalMs = 100.f;
      Flt intervalDev = 0.f;
      BOUNDARY_MODE boundary = BOUNCE;
      END_MODE ending = END_VALUE;
      START_MODE starting = INITIAL;
      bool living = false;
      bool started = false;
      Flt last = 0.f;
      Dbl untilUpdate = 0.0; // ms
      std::uint32_t state;
    };

  } // namespace DSP
} // namespace YSE

#endif // YSE_DSP_GENERATORS_SNOWFALL_HPP
