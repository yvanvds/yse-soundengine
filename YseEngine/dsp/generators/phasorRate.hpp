/*
  ==============================================================================

    phasorRate.hpp
    Time-scales a 0..1 phase ramp by a multiplier (issue #1073).

  ==============================================================================
*/

#ifndef YSE_DSP_GENERATORS_PHASORRATE_HPP
#define YSE_DSP_GENERATORS_PHASORRATE_HPP

#include "../../headers/defines.hpp"
#include "../../headers/types.hpp"
#include "../../headers/constants.hpp"
#include "../buffer.hpp"

namespace YSE {
  namespace DSP {

    /**
     *  @brief Time-scales a 0..1 ramp (a ``phasor`` output): a faster or
     *         slower ramp that stays in phase with it.
     *
     *  Ported from Max's ``rate~``. The **multiplier** scales the length of
     *  a cycle: the output runs at the input's speed divided by it. 0.25
     *  gives four output ramps per input ramp, 2 one output ramp per two
     *  input ramps. Per sample the input step is read the short way round
     *  (a fall of more than half a cycle is a wrap), and the output phase
     *  moves by that step divided by the multiplier, wrapped into 0..1. A
     *  negative multiplier runs the output backwards.
     *
     *  **In phase** means the output is ``frac((n + x) / m)``: ``x`` the
     *  input, ``n`` the input wraps counted since the class was made or last
     *  ``reset()``, ``m`` the multiplier. Then input and output start a new
     *  cycle together at the least common multiple of their periods. A
     *  steady multiplier keeps an in-phase output in phase. What happens
     *  when that breaks (a new multiplier, a ``jump()``) is the sync mode:
     *
     *  - ``syncMode::off`` (the default): never resynchronises. A new
     *    multiplier changes the speed and the ramp continues without a jump.
     *  - ``syncMode::lock``: a new multiplier jumps the output to the in-phase
     *    position at once.
     *  - ``syncMode::cycle``: a new multiplier changes the speed only; the
     *    output jumps to the in-phase position when the input next wraps,
     *    and at every input wrap after that.
     *
     *  ``jump(value)`` (Max's ``goto``) makes the next sample ``value`` and
     *  continues from there; ``jump(value, at)`` does so on the sample where
     *  the input passes ``at``. ``reset()`` restarts the wrap count on the
     *  next input wrap, the output jumping to ``x / m`` there: the in-phase
     *  position of a fresh start.
     *
     *  With ``oneshot()`` on, the output stops when it would wrap and holds
     *  the end it reached (1 running forward, 0 backward) until a
     *  ``jump()`` or ``reset()`` restarts it, or oneshot is turned off. A
     *  backward ramp at 0 crosses its end on the first step, so jump it to 1
     *  to play one backward ramp.
     *
     *  A multiplier of 0 or one that is not a finite number is ignored: the
     *  last valid one stays in use (1 at the start). An input sample that is
     *  not a finite number counts as the previous one, so the ramp does not
     *  move. Nothing depends on the sample rate. The phase is kept in double
     *  precision.
     *
     *  Each call processes at most ``STANDARD_BUFFERSIZE`` samples; with a
     *  multiplier buffer the block is as long as the shorter input. Call it
     *  from one thread at a time. Processing allocates nothing: the output is
     *  sized to ``STANDARD_BUFFERSIZE`` in the constructor.
     */
    class API phasorRate {
    public:
      /** @brief When the output is brought back in phase: Max's ``sync``
       *         values 0, 1 and 2. */
      enum class syncMode {
        cycle = 0, ///< at every input wrap
        lock = 1, ///< whenever the multiplier changes
        off = 2, ///< never (the default)
      };

      phasorRate();

      /** @brief The cycle-length multiplier; 0 or a non-finite value is
       *         ignored. */
      phasorRate& multiplier(Flt value);
      Flt multiplier() const {
        return rate;
      }

      /** @brief How the output is kept in phase; see the class description. */
      phasorRate& sync(syncMode value);
      syncMode sync() const {
        return mode;
      }

      /** @brief On: stop at the end of the current output ramp. Turning it
       *         off restarts a stopped ramp. */
      phasorRate& oneshot(bool on);
      bool oneshot() const {
        return single;
      }

      /** @brief True while a oneshot ramp has ended and holds. */
      bool stopped() const {
        return ended;
      }

      /** @brief The next sample is ``value``; the ramp continues from it.
       *         Max's ``goto <value>``. Replaces a jump still waiting. */
      phasorRate& jump(Flt value);

      /** @brief On the sample where the input passes ``at`` (0..1, clamped)
       *         the output is ``value``. Max's ``goto <value> <time>``.
       *         Replaces a jump still waiting. */
      phasorRate& jump(Flt value, Flt at);

      /** @brief On the next input wrap, restart the wrap count and jump in
       *         phase. Max's ``reset``. */
      phasorRate& reset();

      /** @brief One sample for input ``phase`` with the setter's multiplier. */
      Flt process(Flt phase);

      /** @brief One sample for input ``phase`` with ``multiplier`` (0 or
       *         non-finite: the last valid one). */
      Flt process(Flt phase, Flt multiplier);

      /** @brief One block with the setter's multiplier. */
      buffer& operator()(buffer& phase);

      /** @brief One block with the multiplier read per sample. */
      buffer& operator()(buffer& phase, buffer& multiplier);

    private:
      enum jumpKind { NONE, NOW, AT };

      Flt Step(Flt in, Flt m);

      buffer output;
      Flt rate = 1.f;
      syncMode mode = syncMode::off;
      bool single = false;

      // The multiplier the last sample ran at, the input it read, its wrap
      // count, and the output phase (unwrapped only on a jump sample).
      Flt current = 1.f;
      Flt previous = 0.f;
      Dbl wraps = 0.0;
      Dbl position = 0.0;
      bool ended = false;
      Flt endValue = 1.f;

      jumpKind jumpPending = NONE;
      Flt jumpValue = 0.f;
      Flt jumpAt = 0.f;
      bool resetPending = false;
    };

  } // namespace DSP
} // namespace YSE

#endif // YSE_DSP_GENERATORS_PHASORRATE_HPP
