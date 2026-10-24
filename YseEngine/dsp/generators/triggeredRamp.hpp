/*
  ==============================================================================

    triggeredRamp.hpp
    A single ramp from a start to an end value, started by a trigger signal
    (issue #1072).

  ==============================================================================
*/

#ifndef YSE_DSP_GENERATORS_TRIGGEREDRAMP_HPP
#define YSE_DSP_GENERATORS_TRIGGEREDRAMP_HPP

#include "../../headers/defines.hpp"
#include "../../headers/types.hpp"
#include "../../headers/constants.hpp"
#include "../buffer.hpp"

namespace YSE {
  namespace DSP {

    /**
     *  @brief A one-shot ramp from ``start()`` to ``end()`` over
     *         ``duration()`` milliseconds, started sample-accurately when a
     *         trigger signal goes from 0 to non-zero.
     *
     *  Ported from Max's ``ramp~`` (``DSP::ramp`` is the unrelated linear
     *  control ramp). Per sample:
     *
     *  - A **trigger** is a trigger-input sample that is not 0 after one that
     *    was 0. It starts the ramp on that sample: the phase is 0 there and
     *    the output is ``start``. While a ramp runs a trigger is ignored
     *    unless ``retrigger()`` is on, in which case the ramp starts again
     *    from ``start`` (a jump).
     *  - The **phase** ``p`` runs from 0 to 1 in ``T`` samples, ``T`` being
     *    the duration converted with the live ``SAMPLERATE``: ``p = n / T``
     *    at sample ``n`` after the trigger, reaching 1 (and ``end``) on the
     *    first sample with ``n >= T``. A duration of 0 or less ends the ramp
     *    on the trigger sample itself.
     *  - The **output** is ``start + (end - start) * p^e``, with
     *    ``e = (1 + curve) / (1 - curve)``: the curvature ``~shape`` and
     *    ``.function`` use (Max documents no formula). A curve of 0 is
     *    linear; below 0 a rising ramp runs above the straight line, above 0
     *    below it; -1 jumps to ``end`` at once, +1 holds ``start`` until the
     *    end. Max approximates the curve with straight segments of
     *    ``interval`` samples; this class computes it exactly at every
     *    sample, so it has no ``interval``.
     *  - When the ramp has ended the output holds ``end``, or is 0 with
     *    ``resetToZero()`` on. Before the first trigger it is ``start``, or
     *    0 with ``resetToZero()`` on.
     *
     *  ``start`` and ``end`` are read at every sample, so a ramp follows
     *  them while it runs. The duration is read once, at the trigger, in
     *  ``durationMode::atStart``. In ``durationMode::continuous`` it is read
     *  at every sample and a change rescales what is left: the phase
     *  reached so far is kept and the rest of the ramp is stretched or
     *  shrunk to end when the new duration (counted from the trigger) is
     *  up; a duration already past ends the ramp. A ramp of 1000 ms changed
     *  to 1500 ms after 400 ms runs 1100 ms more.
     *
     *  The four-buffer call reads the duration, start and end per sample
     *  from buffers (the patcher's signal inlets); a sample past a buffer's
     *  length reads the setter's value. A trigger sample that is not a
     *  finite number counts as 0; a non-finite duration, start or end
     *  sample reads the setter's value; a setter given one is ignored.
     *
     *  Defaults: duration 100 ms (Max documents none), start 0, end 1,
     *  curve 0, ``durationMode::atStart``, no reset, no retrigger (Max's
     *  default). Each call processes at most ``STANDARD_BUFFERSIZE``
     *  samples. Call it from one thread at a time. Processing allocates
     *  nothing: the output is sized to ``STANDARD_BUFFERSIZE`` in the
     *  constructor.
     */
    class API triggeredRamp {
    public:
      static constexpr Flt DEFAULT_DURATION = 100.f;
      static constexpr Flt DEFAULT_START = 0.f;
      static constexpr Flt DEFAULT_END = 1.f;

      /** @brief When the duration is read: Max's ``mode`` 0 and 1. */
      enum class durationMode {
        atStart = 0, ///< at the trigger, fixed for the whole ramp
        continuous = 1, ///< at every sample, rescaling what is left
      };

      triggeredRamp();

      /** @brief The ramp's length in milliseconds; 0 or less ends it on the
       *         trigger sample. */
      triggeredRamp& duration(Flt ms);
      Flt duration() const {
        return durationMs;
      }

      /** @brief The value the ramp starts from. */
      triggeredRamp& start(Flt value);
      Flt start() const {
        return startValue;
      }

      /** @brief The value the ramp ends on. */
      triggeredRamp& end(Flt value);
      Flt end() const {
        return endValue;
      }

      /** @brief Curvature, -1 to 1 (clamped); 0 is linear. */
      triggeredRamp& curve(Flt value);
      Flt curve() const {
        return curvature;
      }

      /** @brief When the duration is read; see the class description. */
      triggeredRamp& mode(durationMode value);
      durationMode mode() const {
        return durationRead;
      }

      /** @brief On: the output is 0 once the ramp has ended (and before the
       *         first trigger); off: it holds ``end`` (``start`` before the
       *         first trigger). Max's ``reset``. */
      triggeredRamp& resetToZero(bool on);
      bool resetToZero() const {
        return zeroWhenIdle;
      }

      /** @brief On: a trigger restarts a running ramp; off: it is
       *         ignored. */
      triggeredRamp& retrigger(bool on);
      bool retrigger() const {
        return restartWhileRunning;
      }

      /** @brief True while a ramp runs (from its trigger until it reaches
       *         ``end``). */
      bool running() const {
        return state == RUNNING;
      }

      /** @brief The phase of the current or last ramp, 0..1 (0 before the
       *         first trigger). */
      Flt phase() const {
        return static_cast<Flt>(position);
      }

      /** @brief One block of the ramp for ``trigger``, with the setters'
       *         duration, start and end. */
      buffer& operator()(buffer& trigger);

      /** @brief One block of the ramp for ``trigger``, reading the duration
       *         (ms), start and end per sample from the buffers. */
      buffer& operator()(buffer& trigger, buffer& duration, buffer& start, buffer& end);

    private:
      enum stage { IDLE, RUNNING, DONE };

      buffer& Process(buffer& trigger, buffer* durations, buffer* starts, buffer* ends);

      buffer output;
      Flt durationMs = DEFAULT_DURATION;
      Flt startValue = DEFAULT_START;
      Flt endValue = DEFAULT_END;
      Flt curvature = 0.f;
      durationMode durationRead = durationMode::atStart;
      bool zeroWhenIdle = false;
      bool restartWhileRunning = false;

      stage state = IDLE;
      Flt previous = 0.f;
      // Samples since the trigger, and the anchor the phase is measured from:
      // the phase reached at sample `anchorSample` when the length in samples
      // last changed to `anchorLength`.
      double elapsed = 0.0;
      double position = 0.0;
      double anchorSample = 0.0;
      double anchorPhase = 0.0;
      double anchorLength = 0.0;
    };

  } // namespace DSP
} // namespace YSE

#endif // YSE_DSP_GENERATORS_TRIGGEREDRAMP_HPP
