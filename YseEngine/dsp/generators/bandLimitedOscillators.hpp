/*
  ==============================================================================

    bandLimitedOscillators.hpp
    Antialiased sawtooth, rectangle and triangle oscillators (issue #1055).

  ==============================================================================
*/

#ifndef YSE_DSP_GENERATORS_BANDLIMITEDOSCILLATORS_HPP
#define YSE_DSP_GENERATORS_BANDLIMITEDOSCILLATORS_HPP

#include "../../headers/defines.hpp"
#include "../../headers/types.hpp"
#include "../../headers/constants.hpp"
#include "../buffer.hpp"

namespace YSE {
  namespace DSP {

    /**
     *  @brief Antialiased sawtooth oscillator, rising from -1 to 1 once per
     *         cycle.
     *
     *  The classic bright waveform of subtractive synthesis: every harmonic,
     *  falling off as 1/n. A naive sawtooth (a phase ramp rescaled to -1..1)
     *  jumps from 1 to -1 in a single sample, and that jump carries harmonics
     *  far above the Nyquist frequency, which fold back as inharmonic tones,
     *  loudly so at high pitches. This one smooths the jump with a polyBLEP
     *  correction: a two-sample polynomial band-limited step added around each
     *  discontinuity. Away from the jump the output is the exact ramp
     *  ``2 * phase - 1``. Ported from Max's ``saw~``.
     *
     *  polyBLEP rather than a band-limited wavetable: it costs a few
     *  multiplications per sample at any pitch, needs no tables per octave,
     *  and works the same for the variable pulse width of ``blRect`` and the
     *  variable duty of ``blTri``, which a fixed table cannot follow. It is
     *  not perfectly alias-free (it suppresses the folded energy by tens of
     *  decibels, most strongly in the low and middle of the band), which is
     *  the usual trade for analog-style oscillators.
     *
     *  A negative frequency runs the ramp backwards (falling). The sign
     *  carries through the antialiasing. Frequencies beyond half the sample
     *  rate are held at half the sample rate; a frequency that is not a finite
     *  number holds the phase. The phase is kept in double precision and the
     *  sample rate is read from ``SAMPLERATE`` on every block, so the pitch
     *  stays right across an engine restart at another rate.
     *
     *  **Sync.** The overloads taking a ``sync`` signal reset the phase to 0
     *  whenever that signal rises across ``syncThreshold()`` (0.5 by default)
     *  from one sample to the next, for hard-sync sounds. The reset itself is
     *  not band-limited.
     *
     *  Apart from the constructor and the setters, call it only from the
     *  audio thread. Processing allocates nothing: the output is sized to
     *  ``STANDARD_BUFFERSIZE`` in the constructor, and a block is at most that
     *  long (a longer one is processed up to ``STANDARD_BUFFERSIZE`` samples).
     */
    class API blSaw {
    public:
      blSaw();

      /** @brief One block at a constant ``frequency`` in Hz. */
      buffer& operator()(Flt frequency, UInt length = STANDARD_BUFFERSIZE);

      /** @brief One block driven by a per-sample frequency in Hz. */
      buffer& operator()(buffer& frequency);

      /** @brief One block driven by a per-sample frequency, hard-synced to
       *         @p sync (see the class description). */
      buffer& operator()(buffer& frequency, buffer& sync);

      /** @brief Jump to a phase now, wrapped into 0..1. Phase 0 is the jump
       *         from 1 to -1. */
      blSaw& phase(Flt value);

      /** @brief The phase the next sample will have, in 0..1. */
      Flt phase() const;

      /** @brief The level a sync signal must rise across to reset the
       *         phase. Default 0.5. */
      blSaw& syncThreshold(Flt value);
      Flt syncThreshold() const;

    private:
      Dbl current;
      Flt threshold;
      Flt lastSync;
      bool syncPrimed;
      buffer output;
    };

    /**
     *  @brief Antialiased rectangular (pulse) oscillator, switching between
     *         1 and -1.
     *
     *  The output is 1 for the first ``width`` of each cycle and -1 for the
     *  rest. A width of 0.5 is a square wave (odd harmonics only, hollow and
     *  woody); narrower pulses thin the sound towards a nasal buzz, and
     *  sweeping the width (pulse-width modulation) gives the classic chorused
     *  analog string pad. The width is wrapped into 0..1, so 1.25 means 0.25;
     *  at 0 the output is a constant -1. Both edges are smoothed with polyBLEP,
     *  as in ``blSaw``. Ported from Max's ``rect~``.
     *
     *  A rectangle's mean is ``2 * width - 1``, so any width but 0.5 carries
     *  a DC offset, as in the analog original.
     *
     *  Frequency, sample rate, sync and real-time rules are those of
     *  ``blSaw``. A width that is not a finite number is taken as 0.5.
     */
    class API blRect {
    public:
      blRect();

      /** @brief One block at a constant ``frequency`` in Hz and the width
       *         set with ``width(Flt)``. */
      buffer& operator()(Flt frequency, UInt length = STANDARD_BUFFERSIZE);

      /** @brief One block driven by a per-sample frequency, at the width set
       *         with ``width(Flt)``. */
      buffer& operator()(buffer& frequency);

      /** @brief One block driven by a per-sample frequency and width. */
      buffer& operator()(buffer& frequency, buffer& width);

      /** @brief As above, hard-synced to @p sync. */
      buffer& operator()(buffer& frequency, buffer& width, buffer& sync);

      /** @brief The pulse width used while none is passed per sample.
       *         Default 0.5. */
      blRect& width(Flt value);
      Flt width() const;

      /** @brief Jump to a phase now, wrapped into 0..1. Phase 0 is the
       *         rising edge. */
      blRect& phase(Flt value);
      Flt phase() const;

      /** @brief The level a sync signal must rise across to reset the
       *         phase. Default 0.5. */
      blRect& syncThreshold(Flt value);
      Flt syncThreshold() const;

    private:
      Dbl current;
      Flt pulseWidth;
      Flt threshold;
      Flt lastSync;
      bool syncPrimed;
      buffer output;
    };

    /**
     *  @brief Antialiased triangle oscillator with a variable duty cycle.
     *
     *  The output rises from -1 to 1 over the first ``duty`` of each cycle
     *  and falls back over the rest. A duty of 0.5 is a symmetric triangle
     *  (odd harmonics falling off as 1/n², soft and flute-like); moving the
     *  duty towards 0.05 or 0.95 skews it towards a falling or a rising
     *  sawtooth and brightens it. The duty is wrapped into 0..1 and then held
     *  within 0.05..0.95, as in Max's ``tri~``, from which it is ported.
     *
     *  A triangle has no jumps, only corners, so the corners are smoothed with
     *  polyBLAMP (the integral of polyBLEP), scaled by how sharply the slope
     *  turns.
     *
     *  Frequency, sample rate, sync and real-time rules are those of
     *  ``blSaw``. A duty that is not a finite number is taken as 0.5.
     */
    class API blTri {
    public:
      blTri();

      /** @brief One block at a constant ``frequency`` in Hz and the duty set
       *         with ``duty(Flt)``. */
      buffer& operator()(Flt frequency, UInt length = STANDARD_BUFFERSIZE);

      /** @brief One block driven by a per-sample frequency, at the duty set
       *         with ``duty(Flt)``. */
      buffer& operator()(buffer& frequency);

      /** @brief One block driven by a per-sample frequency and duty. */
      buffer& operator()(buffer& frequency, buffer& duty);

      /** @brief As above, hard-synced to @p sync. */
      buffer& operator()(buffer& frequency, buffer& duty, buffer& sync);

      /** @brief The duty cycle used while none is passed per sample.
       *         Default 0.5. */
      blTri& duty(Flt value);
      Flt duty() const;

      /** @brief Jump to a phase now, wrapped into 0..1. Phase 0 is the low
       *         corner (-1). */
      blTri& phase(Flt value);
      Flt phase() const;

      /** @brief The level a sync signal must rise across to reset the
       *         phase. Default 0.5. */
      blTri& syncThreshold(Flt value);
      Flt syncThreshold() const;

    private:
      Dbl current;
      Flt dutyCycle;
      Flt threshold;
      Flt lastSync;
      bool syncPrimed;
      buffer output;
    };

  } // namespace DSP
} // namespace YSE

#endif // YSE_DSP_GENERATORS_BANDLIMITEDOSCILLATORS_HPP
