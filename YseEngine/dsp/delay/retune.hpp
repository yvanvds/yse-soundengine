/*
  ==============================================================================

    retune.hpp
    Pitch detection and correction toward a scale or a set of notes, built
    from DSP::pitchTracker and DSP::pitchShifter (issue #1154).

  ==============================================================================
*/

#ifndef YSE_DSP_DELAY_RETUNE_HPP
#define YSE_DSP_DELAY_RETUNE_HPP

#include <array>
#include "../../headers/defines.hpp"
#include "../../headers/types.hpp"
#include "../buffer.hpp"
#include "../analysis/pitchTracker.hpp"
#include "pitchShifter.hpp"

namespace YSE {
  class scale;

  namespace DSP {

    /**
     *  @brief The notes a ``DSP::retune`` corrects toward: a scale of
     *         degrees in cents above a base note, repeated every octave,
     *         of which any subset is enabled.
     *
     *  **Degrees.** Degree 0 is the base note itself; ``notes()`` sets the
     *  others, in cents above it, as MSP's ``notelist`` does in the Scala
     *  format (``100 200 ... 1100`` the chromatic scale, ``200 400 500 700
     *  900 1100`` the major one). Every degree repeats at every octave, so
     *  a value at or above 1200 cents (or at or below 0) is no degree and
     *  is skipped: in particular the ``1200`` (``2/1``) a Scala file ends
     *  with. ``ratioToCents()`` turns a ratio such as ``9/8`` into cents. At
     *  most ``MAX_NOTES`` degrees, degree 0 included.
     *
     *  **Pitch.** The base note is ``base()`` semitones above C (0 C, 9 A,
     *  11 B), in equal temperament from the A at ``standardPitch()`` Hz
     *  (440 by default): degree ``i`` of a scale on base ``b`` lies
     *  ``100 (b - 9) + note(i)`` cents from that A, at every octave.
     *
     *  **Enabled notes.** Each degree is enabled or not, bit ``i`` of
     *  ``enabled()`` for degree ``i``; only an enabled degree is a target.
     *  ``midiNote()`` enables or disables the degree a MIDI note falls on,
     *  ``(note - base()) mod size()``: with the chromatic scale on C, note 60
     *  is degree 0 (C) and note 69 degree 9 (A).
     *
     *  The default is MSP's: the chromatic scale on C, every note enabled,
     *  A at 440 Hz. The class is a plain value (no allocation, no
     *  pointers), so it may be copied between threads through any
     *  lock-free hand-over and set on a ``retune`` from the audio thread.
     */
    class API tuning {
    public:
      /** @brief The most degrees a scale has, degree 0 included. */
      static constexpr UInt MAX_NOTES = 64;

      /** @brief @p ratio in cents: ``1200 log2(ratio)``. A ratio that is
       *         not a finite number above 0: 0. */
      static Flt ratioToCents(Flt ratio);

      /** @brief The chromatic scale on C, every note enabled, A at 440 Hz. */
      tuning();

      /** @brief The A the base note is reckoned from, in Hz. A value at or
       *         below 0, or one that is not finite, is ignored. MSP's
       *         ``standard pitch`` argument. */
      tuning& standardPitch(Flt hz);
      Flt standardPitch() const {
        return standard;
      }

      /** @brief The chromatic scale, with every note enabled; the base and
       *         the standard pitch stay. MSP's ``defaulttuning``. */
      tuning& chromatic();

      /** @brief The degrees above the base, in cents: @p count values, of
       *         which those not finite or not between 0 and 1200 (both
       *         excluded) are skipped. The enabled notes stay. MSP's
       *         ``notelist``. */
      tuning& notes(const Flt* cents, UInt count);

      /** @brief How many degrees the scale has, degree 0 included. */
      UInt size() const {
        return count;
      }

      /** @brief Degree @p index in cents above the base; 0 for degree 0 and
       *         past the last. */
      Flt note(UInt index) const {
        return index < count ? degrees[index] : 0.f;
      }

      /** @brief The base note in semitones above C, taken mod 12 (0 C ... 11
       *         B). MSP's ``notebase``. */
      tuning& base(Int semitone);
      Int base() const {
        return baseNote;
      }

      /** @brief The enabled degrees, bit ``i`` for degree ``i``; bits past
       *         ``size()`` are kept but have no degree. MSP's
       *         ``enablednotes``. */
      tuning& enabled(U64 mask);
      U64 enabled() const {
        return mask;
      }

      /** @brief Degree @p index on or off; past ``MAX_NOTES``: ignored. */
      tuning& enable(UInt index, bool on);
      /** @brief Whether degree @p index is a degree and enabled. */
      bool isEnabled(UInt index) const;

      /** @brief Every degree enabled. MSP's ``flush`` and ``enablednotes
       *         all``. */
      tuning& enableAll();

      /** @brief The degree MIDI note @p note falls on, ``(note - base())
       *         mod size()``, enabled for a @p velocity above 0 and disabled
       *         for 0 or below. MSP's ``list``. */
      tuning& midiNote(Int note, Int velocity);

      /** @brief The pitch classes of a ``YSE::scale`` (its MIDI pitches mod
       *         12): base C, every class but C a degree above it, and
       *         exactly the scale's classes enabled (C only when the scale
       *         has it). The standard pitch stays. An empty scale leaves C
       *         alone, disabled: nothing to correct toward. A class past
       *         ``MAX_NOTES`` is dropped. Reads the scale's
       *         pitches without allocating; not while another thread
       *         changes that scale. */
      tuning& scale(const YSE::scale& source);

      /** @brief The enabled pitch nearest @p cents, both in cents from the
       *         standard pitch, at any octave, into @p target (the lower on
       *         a tie). False, with @p target untouched, when no degree is
       *         enabled or @p cents is not finite. */
      bool nearest(Flt cents, Flt& target) const;

    private:
      std::array<Flt, MAX_NOTES> degrees{};
      UInt count = 0;
      Int baseNote = 0;
      U64 mask = ~U64(0);
      Flt standard = 440.f;
    };

    /**
     *  @brief Detects the pitch of a monophonic signal and shifts it toward
     *         the nearest enabled note of a ``DSP::tuning``, or toward a
     *         pitch given per sample: autotune. What ``~retune`` wraps.
     *
     *  Ported from MSP's ``retune~`` (issue #1154), built from YSE's own
     *  ``DSP::pitchTracker`` and ``DSP::pitchShifter`` instead of Ztx.
     *
     *  **Detection.** The input runs through a ``pitchTracker`` (YIN, a
     *  2048-sample analysis buffer, 47 Hz to 4 kHz at 48 kHz) that analyses
     *  every ``period()`` samples (MSP's ``windowsize``, 256 by default).
     *  ``frequency()`` is the pitch of the last analysis in Hz, or 0 when it
     *  found none: below the tracker's gate, or not periodic enough. How
     *  periodic a sound has to be is ``ambience()``, the tracker's YIN
     *  tolerance (MSP's ``correction_ambience_threshold``, 0.3 by default):
     *  higher passes noisier or more reverberant sound as pitched, lower
     *  leaves it uncorrected. ``tracker()`` gives the rest of the tracker's
     *  settings (its range, size and gate).
     *
     *  **Sibilance.** Between two analyses the input's zero crossings are
     *  counted; ``sibilance()`` is them in percent of the samples (a 440 Hz
     *  tone at 48 kHz about 1.8, an "s" 20 to 40, alternating samples 100).
     *  Above ``bypass()`` (MSP's ``correction_bypass``, 100 by default: never)
     *  the input passes uncorrected, so sibilants are not processed.
     *
     *  **Correction.** The input lies ``x = 1200 log2(frequency() /
     *  standardPitch())`` cents from the tuning's A. Its target ``t`` is the
     *  nearest enabled note (``tuning::nearest``), or, where the lock buffer
     *  holds a frequency above 0, that frequency. The deviation ``t - x``
     *  within ``threshold()`` cents either way (MSP's
     *  ``correction_threshold``, 0 by default) is left alone; beyond it,
     *  ``amount()`` percent of it is corrected (MSP's ``correction_amount``,
     *  100 by default: onto the note). No pitch, sibilance, no enabled note
     *  or ``amount()`` 0: no correction. ``correction()`` is the shift in
     *  cents. The detune buffer, in cents, is added to it per sample.
     *
     *  The correction is worked out anew every ``CHUNK`` samples from the
     *  last analysis and the lock value at the chunk's first sample, and the
     *  shifter takes it per sample, in cents: a grain takes the shift at
     *  the sample it starts (granular) or a frame at the sample it completes
     *  (phase vocoder), so a correction that changes glides without a click.
     *  A pitch only changes once an analysis has heard it, about half the
     *  tracker's 2048 samples later (``tracker().latency()``), so the start
     *  of a new note is corrected toward the old one's target for a moment.
     *
     *  **Shifting** is a ``pitchShifter`` in ``quality()`` (MSP's
     *  ``quality``). ``latency()`` is its latency: in the granular modes
     *  ``ceil(|1 - r| G / 2) + 2 + G / 8`` samples at the latest ratio ``r``
     *  (``G`` 1024 for ``BASIC``, 2048 for ``GOOD``), so it moves with the
     *  correction (130 to 142 samples for a correction within 40 cents at
     *  ``BASIC``) and is 2 at no correction; in the phase vocoder the frame
     *  size, 2048 or 4096 samples. See ``pitchShifter`` for what each costs.
     *
     *  **Enabled.** ``enabled(false)`` (MSP's ``retune 0``) passes the input
     *  straight through with no delay and ``latency()`` 0; turning it back
     *  on starts the shifter afresh. ``pitchDetection()`` (MSP's
     *  ``pitchdetection``) fills ``pitch()`` with ``frequency()`` per sample
     *  (zeros when off), also while not retuning; with both off the tracker
     *  rests.
     *
     *  Nothing depends on the sample rate: the tracker reads the live rate
     *  at every analysis, and periods, crossings and latency are in samples.
     *  Every engine is allocated in the constructor; every setter and block
     *  call allocates nothing and takes no lock, so the setters (a new
     *  ``tuning`` included) may run between blocks on the audio thread. A
     *  block call processes at most ``STANDARD_BUFFERSIZE`` samples. One
     *  thread at a time.
     */
    class API retune {
    public:
      /** @brief The default time between analyses, in samples. */
      static constexpr UInt DEFAULT_PERIOD = 256;
      /** @brief How many samples one correction holds for. */
      static constexpr UInt CHUNK = 16;
      static constexpr Flt DEFAULT_AMBIENCE = 0.3f;

      /** @brief A corrector with the defaults above, shifting in
       *         @p quality. Allocates. */
      explicit retune(pitchShifter::QUALITY quality = pitchShifter::BASIC);

      /** @brief The notes corrected toward; changing them in place takes
       *         effect at the next chunk. */
      tuning& notes() {
        return scaleNotes;
      }
      const tuning& notes() const {
        return scaleNotes;
      }
      /** @brief A whole new tuning, copied. */
      retune& notes(const tuning& value);

      /** @brief How much of the deviation is corrected, in percent: 0 none,
       *         100 onto the note. Held to 0 .. 100; not finite: ignored.
       *         MSP's ``correction_amount``. */
      retune& amount(Flt percent);
      Flt amount() const {
        return percentage;
      }

      /** @brief The deviation in cents, either way, left uncorrected. Below
       *         0: 0; not finite: ignored. MSP's ``correction_threshold``. */
      retune& threshold(Flt cents);
      Flt threshold() const {
        return tolerance;
      }

      /** @brief The tracker's YIN tolerance, 0 .. 1 (see the class notes).
       *         MSP's ``correction_ambience_threshold``. */
      retune& ambience(Flt value);
      Flt ambience() const;

      /** @brief The sibilance in percent above which the input passes
       *         uncorrected. Not finite: ignored. MSP's
       *         ``correction_bypass``. */
      retune& bypass(Flt percent);
      Flt bypass() const {
        return sibilanceLimit;
      }

      /** @brief The time between analyses in samples, held to the tracker's
       *         16 .. 4096; 0 is ``DEFAULT_PERIOD``. MSP's ``windowsize``. */
      retune& period(UInt samples);
      UInt period() const;

      /** @brief The shifter's algorithm; a change starts it afresh. MSP's
       *         ``quality``. */
      retune& quality(pitchShifter::QUALITY value);
      pitchShifter::QUALITY quality() const {
        return shifter.quality();
      }

      /** @brief Retuning (on, the default) or the input passed straight
       *         through (off). MSP's ``retune``. */
      retune& enabled(bool on);
      bool enabled() const {
        return active;
      }

      /** @brief Whether ``pitch()`` carries the detected pitch (on) or zeros
       *         (off, the default). MSP's ``pitchdetection``. */
      retune& pitchDetection(bool on);
      bool pitchDetection() const {
        return detecting;
      }

      /** @brief Samples from the input to the output: the shifter's
       *         ``latency()``. Cheap. */
      UInt latency() const {
        return shifter.latency();
      }

      /** @brief Starts over: the tracker's history and the shifter silent,
       *         no pitch, no correction. Keeps the settings. */
      retune& reset();

      /** @brief One block, toward the tuning. */
      buffer& operator()(buffer& in);

      /** @brief One block with a lock frequency in Hz (above 0: the
       *         target, else the tuning) and a detune in cents per sample.
       *         The block is as long as the shortest of the three; a value
       *         that is not finite counts as 0. */
      buffer& operator()(buffer& in, buffer& lock, buffer& detune);

      /** @brief The corrected output of the last block call. */
      buffer& output() {
        return shifter.output();
      }

      /** @brief The detected pitch per sample of the last block call (see
       *         ``pitchDetection()``). */
      buffer& pitch() {
        return detected;
      }

      /** @brief The pitch of the last analysis in Hz; 0 when it found none. */
      Flt frequency() const {
        return heard;
      }

      /** @brief The zero crossings since the analysis before the last, in
       *         percent of the samples. */
      Flt sibilance() const {
        return sibilant;
      }

      /** @brief The correction of the last chunk in cents, before the
       *         detune. */
      Flt correction() const {
        return shift;
      }

      /** @brief The tracker, for its range, size and gate. ``ambience()``
       *         and ``period()`` set two of its settings. */
      pitchTracker& tracker() {
        return detector;
      }
      const pitchTracker& tracker() const {
        return detector;
      }

    private:
      buffer& Process(buffer& in, const Flt* lock, const Flt* detune, UInt length);
      void Analysed();
      Flt Correct(Flt lockHz) const;

      tuning scaleNotes;
      Flt percentage = 100.f;
      Flt tolerance = 0.f;
      Flt sibilanceLimit = 100.f;
      bool active = true;
      bool detecting = false;

      pitchTracker detector;
      pitchShifter shifter;
      buffer cents; // the shift per sample, in cents
      buffer detected; // the pitch per sample

      Flt heard = 0.f; // the last analysis's pitch, 0: none
      Flt sibilant = 0.f;
      Flt shift = 0.f;
      UInt crossings = 0;
      UInt counted = 0; // samples since the last analysis
      bool negative = false; // the sign of the last sample counted
    };

  } // namespace DSP
} // namespace YSE

#endif // YSE_DSP_DELAY_RETUNE_HPP
