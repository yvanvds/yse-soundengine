/*
  ==============================================================================

    pitchShifter.hpp
    A real-time pitch shifter: a granular dual delay line for low latency,
    or a phase vocoder for quality (issue #1153).

  ==============================================================================
*/

#ifndef YSE_DSP_DELAY_PITCHSHIFTER_HPP
#define YSE_DSP_DELAY_PITCHSHIFTER_HPP

#include <vector>
#include "../../headers/defines.hpp"
#include "../../headers/types.hpp"
#include "../../headers/constants.hpp"
#include "../buffer.hpp"
#include "../spectral/frameClock.hpp"
#include "../spectral/spectralPitchShift.hpp"
#include "../spectral/stft.hpp"

namespace YSE {
  namespace DSP {

    /**
     *  @brief Shifts the pitch of a signal by a ratio or in cents, in real
     *         time, keeping its duration: what ``~pitchshift`` wraps.
     *
     *  Ported from MSP's ``pitchshift~`` (issue #1153), on algorithms of
     *  YSE's own. ``quality()`` picks one of two:
     *
     *  **Granular (``BASIC``, ``GOOD``).** A dual delay line. The input is
     *  written into a delay line that two grains read, half a grain apart,
     *  each at ``ratio()`` samples per sample, with a Hann window over the
     *  grain (``G`` = 1024 samples for ``BASIC``, 2048 for ``GOOD``) so the
     *  two always sum to 1. A grain starting at sample ``n`` reads at the
     *  delay ``d(a) = L + o - (1 - r) G / 2 + (1 - r) a`` samples, ``a``
     *  samples into the grain, at the ratio ``r`` it took when it started:
     *  centred, halfway through the grain, on ``L + o``. The delay line is
     *  read with 4-point Hermite interpolation.
     *
     *  - **Alignment.** Two copies of a tone at different delays are out of
     *    phase, and crossfading between them would pull its pitch off (by
     *    up to half the grain rate). So a grain that starts picks the offset
     *    ``o``, from ``-G / 8`` to ``G / 8`` samples, at which the last
     *    ``G / 4`` samples it would have read correlate best with those the
     *    playing grain has just read (normalised; silence: 0), or, of the
     *    offsets within 0.002 of the best, the one nearest 0, so that a
     *    periodic sound, which matches as well a period further on, keeps
     *    the grains near their nominal delay. A periodic sound whose period
     *    is at most ``G / 4`` (187 Hz and up at ``BASIC`` and 48 kHz, 94 Hz
     *    at ``GOOD``) is crossfaded in phase and comes out at ``r`` times
     *    its pitch; a lower one is aligned less well. This costs ``G / 4 + 1`` correlations of
     *    ``G / 4`` samples each time a grain starts. At ratio 1 there is
     *    nothing to align and ``o`` is 0.
     *  - **Latency.** ``L`` is ``ceil(|1 - r| G / 2) + 2 + G / 8``: the
     *    least that keeps every grain's delay at 2 or more whatever its
     *    offset. Sound is heard on average ``L`` samples late, each grain
     *    up to ``G / 8`` earlier or later. At ratio 1, ``L`` is 2. With
     *    ``constantLatency(true)``, ``L`` is that of the farthest ratio,
     *    ``ceil(3 G / 2) + 2 + G / 8``, whatever the ratio. A semitone either
     *    way costs about 160 samples at ``BASIC`` (3.4 ms at 48 kHz), an
     *    octave up 642; constant, ``BASIC`` is 1666 samples late and
     *    ``GOOD`` 3330.
     *  - A new ratio is taken by the next grain to start, every ``G / 2``
     *    samples, and the crossfade into it is the grain window: a ratio
     *    changed every block glides without a click. A caller that moves
     *    the ratio all the time (a pitch corrector) and wants a fixed
     *    delay turns ``constantLatency()`` on; off, ``latency()`` follows
     *    ``|1 - r|`` and drops to 2 at exactly 1.
     *  - At ratio 1 every grain reads at the delay ``L``, and the output is
     *    the input ``L`` samples later, bit for bit.
     *  - **Quality.** Where the grains overlap, a sound that is not
     *    periodic (noise, a chord, a sweep) is not in phase in both, and
     *    partly cancels: its level wavers at the grain rate (``2 SAMPLERATE
     *    / G`` Hz, about 94 Hz at ``BASIC`` and 48 kHz), more for large
     *    shifts, and a sharp attack can be heard twice. Longer grains
     *    (``GOOD``) waver more slowly and align lower pitches; shorter ones
     *    smear less in time. Shifting up reads the input faster without
     *    filtering it first, so what lies above ``SAMPLERATE / (2 r)``
     *    aliases.
     *
     *  **Phase vocoder (``BETTER``, ``BEST``).** A ``DSP::stft`` frame loop
     *  (a ``frameClock`` with an ``stftAnalysis`` and an ``stftSynthesis``,
     *  Hann windows) with a ``DSP::spectralPitchShift`` on every frame,
     *  which moves the spectrum's peaks to their transposed bins with their
     *  phases locked (see that class). ``BETTER`` uses frames of 2048
     *  samples at an overlap of 4, ``BEST`` 4096 at an overlap of 8. The
     *  latency is the frame size, whatever the ratio: 2048 and 4096
     *  samples. A steady tone keeps its level and has no grain rate, at the
     *  price of more latency, more work per sample, and transients smeared
     *  over a frame. At ratio 1 the output is the input ``latency()``
     *  samples later. The ratio is read once per frame.
     *
     *  Formants move with the pitch in every mode: a voice shifted up sounds
     *  smaller. Formant preservation is not part of this class.
     *
     *  **The ratio** is held to ``MINIMUM_RATIO`` (two octaves down) to
     *  ``MAXIMUM_RATIO`` (two octaves up); ``cents()`` sets it as
     *  ``2^(cents / 1200)``. A value that is not a finite number is
     *  ignored. The setters are cheap and allocate nothing, so a caller on
     *  the audio thread (a pitch corrector) may set a new ratio every block
     *  and read ``latency()`` back. The two-buffer call reads the shift per
     *  sample from a second buffer, as a ratio or, with ``usesCents()``, in
     *  cents: a grain or frame takes the value at the sample it starts
     *  (granular) or completes (phase vocoder), and ``ratio()`` is the
     *  block's last finite value afterwards.
     *
     *  **Switching.** A new ``quality()``, or ``enabled()`` turned back on,
     *  starts the shifter afresh: silence for ``latency()`` samples, then the
     *  shifted input. ``constantLatency()`` only changes what grains that
     *  start later use. Disabled, the output is the input with no delay and
     *  ``latency()`` is 0.
     *
     *  Nothing depends on the sample rate: grains, frames and latency are in
     *  samples. All memory (the delay line, both phase vocoders and their
     *  spectral shifters) is allocated in the constructor, so every setter
     *  and every block call allocate nothing and take no lock. A block call
     *  processes at most ``STANDARD_BUFFERSIZE`` samples; with two buffers
     *  the block is as long as the shorter one. One thread at a time.
     */
    class API pitchShifter {
    public:
      /** @brief The algorithm and its size; the names are MSP's. */
      enum QUALITY {
        BASIC, ///< granular, 1024-sample grains: the least latency (default)
        GOOD, ///< granular, 2048-sample grains
        BETTER, ///< phase vocoder, 2048-sample frames at an overlap of 4
        BEST, ///< phase vocoder, 4096-sample frames at an overlap of 8
      };

      /** @brief The lowest ratio: two octaves down. */
      static constexpr Flt MINIMUM_RATIO = 0.25f;
      /** @brief The highest ratio: two octaves up. */
      static constexpr Flt MAXIMUM_RATIO = 4.f;
      /** @brief The grain of ``BASIC``, in samples. */
      static constexpr UInt BASIC_GRAIN = 1024;
      /** @brief The grain of ``GOOD``, in samples. */
      static constexpr UInt GOOD_GRAIN = 2048;

      /** @brief The ratio @p cents make: ``2^(cents / 1200)``, held to the
       *         ratio's range. Not a finite number: 1. */
      static Flt centsToRatio(Flt cents);

      /** @brief A shifter at ratio 1 in @p quality. Allocates. */
      explicit pitchShifter(QUALITY quality = BASIC);

      /** @brief The pitch ratio: 2 an octave up, 0.5 an octave down, 1
       *         unchanged. Held to ``MINIMUM_RATIO`` to ``MAXIMUM_RATIO``;
       *         not a finite number: ignored. MSP's ``pitchshift``. */
      pitchShifter& ratio(Flt value);
      Flt ratio() const {
        return scale;
      }

      /** @brief The shift in cents (100 a semitone up, -1200 an octave
       *         down): the ratio ``centsToRatio(value)``. Not a finite
       *         number: ignored. MSP's ``pitchshiftcent``. */
      pitchShifter& cents(Flt value);
      /** @brief The ratio in cents. */
      Flt cents() const;

      /** @brief Whether the two-buffer call reads its second buffer in
       *         cents (on) or as a ratio (off, the default). MSP's
       *         ``usecents``. */
      pitchShifter& usesCents(bool on);
      bool usesCents() const {
        return centsIn;
      }

      /** @brief The algorithm; a change starts afresh. MSP's ``quality``. */
      pitchShifter& quality(QUALITY value);
      QUALITY quality() const {
        return mode;
      }

      /** @brief Granular modes: the latency of the farthest ratio for every
       *         ratio (on), or the least the ratio needs (off, the default).
       *         The phase vocoder's is constant either way. MSP's
       *         ``constantlatency``. */
      pitchShifter& constantLatency(bool on);
      bool constantLatency() const {
        return constant;
      }

      /** @brief Shifting (on, the default) or the input passed straight
       *         through (off); turning it on starts afresh. MSP's
       *         ``enabled``. */
      pitchShifter& enabled(bool on);
      bool enabled() const {
        return active;
      }

      /** @brief Samples from the input to the output: in the granular modes
       *         ``L``, the delay at the centre of a grain that starts now
       *         before its alignment offset (see the class notes), in the
       *         phase vocoder the frame size, disabled 0. Cheap: one call
       *         per block is fine. */
      UInt latency() const;

      /** @brief Silences the delay line and the phase vocoder, and starts
       *         the grains and frames afresh. */
      pitchShifter& reset();

      /** @brief One block at ``ratio()``. */
      buffer& operator()(buffer& in);

      /** @brief One block with the shift read per sample from @p shift, a
       *         ratio or, with ``usesCents()``, cents. */
      buffer& operator()(buffer& in, buffer& shift);

      /** @brief The output of the last block call. */
      buffer& output() {
        return result;
      }

    private:
      // One frame-by-frame phase vocoder: an stft frame loop with a peak
      // shifter on every frame.
      struct vocoder {
        vocoder(UInt size, UInt overlap);
        void reset();
        frameClock clock;
        stftAnalysis analysis;
        stftSynthesis synthesis;
        spectralPitchShift shifter;
        U64 frames = 0;
      };

      // One grain of the dual delay line.
      struct grain {
        Dbl delay = 0.0; // samples behind the write position
        Dbl step = 0.0; // 1 - ratio: the delay's change per sample
      };

      UInt GrainSize() const;
      UInt GrainLatency(Flt ratio) const;
      void StartGrain(grain& g, Flt ratio, UInt into, const grain* other);
      Int Align(Dbl start, Dbl playing);
      Flt Read(Dbl delay) const;
      Flt Shift(Flt sample) const;
      void Granular(const Flt* in, const Flt* shift, Flt* out, UInt length);
      void Vocode(vocoder& pv, const Flt* in, const Flt* shift, Flt* out, UInt length);
      buffer& Process(buffer& in, const Flt* shift, UInt length);

      Flt scale = 1.f;
      bool centsIn = false;
      QUALITY mode;
      bool constant = false;
      bool active = true;

      std::vector<Flt> line; // the delay line, a power of two long
      UInt mask;
      UInt write = 0; // where the next sample goes
      std::vector<Flt> window; // a GOOD_GRAIN Hann window
      std::vector<Flt> scores; // the alignment search's, one per offset
      grain grains[2]; // the second is half a grain behind the first
      UInt age = 0; // samples into the first grain
      bool fresh = true; // nothing processed since reset()

      vocoder better;
      vocoder best;
      buffer result;
    };

  } // namespace DSP
} // namespace YSE

#endif // YSE_DSP_DELAY_PITCHSHIFTER_HPP
