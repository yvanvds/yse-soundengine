/*
  ==============================================================================

    spectrumAnalyzer.hpp
    A spectrum analyser's frames: the amplitude (or phase) spectrum of a
    signal, or of a ~pfft's spectral frames, every update interval, handed to
    a reader on another thread through a triple buffer (issue #1170).

  ==============================================================================
*/

#ifndef YSE_DSP_ANALYSIS_SPECTRUMANALYZER_HPP
#define YSE_DSP_ANALYSIS_SPECTRUMANALYZER_HPP

#include <atomic>
#include <vector>
#include "../../headers/defines.hpp"
#include "../../headers/types.hpp"
#include "../../headers/constants.hpp"
#include "../buffer.hpp"
#include "../spectral/stft.hpp"

namespace YSE {
  namespace DSP {

    /**
     *  @brief Measures the spectrum of a signal every update interval, for
     *         whoever draws it: a spectrogram from the newest frame, a
     *         sonogram from successive ones. The analyser owns the numbers;
     *         the reader does the drawing.
     *
     *  **Frames.** Every ``interval()`` milliseconds (20 by default), turned
     *  into samples with the live ``SAMPLERATE`` when each wait begins and
     *  keeping its fraction, the analyser takes the latest ``size()``
     *  samples (4096 by default, the stream reading as 0 before its first
     *  sample), weights them with a periodic Hann window and transforms
     *  them with the engine's FFT. Frame ``k`` is taken after sample
     *  ``ceil(k * interval)`` of the stream, sample-accurate across blocks.
     *  A frame holds ``size() / 2 + 1`` bins, DC to Nyquist; bin ``j`` is
     *  the frequency ``j * rate / size`` Hz, ``rate`` being the sample rate
     *  the frame was taken at.
     *
     *  - **Amplitude** (the default): the magnitude of each bin, scaled by
     *    2 over the window's sum (1 over it at DC and Nyquist), so a sine of
     *    amplitude ``a`` whose frequency is a bin's reads ``a`` there (and
     *    ``a / 2`` in the bins beside it, the Hann window's main lobe), and
     *    a constant ``c`` reads ``c`` at DC (and in bin 1, the lobe).
     *  - **Phase**: the phase of each bin in radians, ``-pi`` to ``pi``,
     *    against the frame's first (oldest) sample: a cosine of phase
     *    ``p`` there, of a bin's frequency, reads ``p`` in that bin.
     *
     *  Samples that are not finite count as 0. Changing ``size()`` applies
     *  from the next frame, over the samples already taken in.
     *
     *  **Spectral frames.** The ``stftFrame`` call takes bins already
     *  transformed, run by run, as a ``~pfft`` hands its subpatch the bins
     *  of a frame: real and imaginary parts, ``firstBin`` 0 starting a
     *  frame. A spectral frame is captured once ``interval()`` has gone by
     *  in hops since the last one, and published at its last bin. Its
     *  amplitudes are scaled by 2 over its size (1 over it at DC and
     *  Nyquist): a sine reads its amplitude times the mean of whatever
     *  window the frames were weighted with (a half for a Hann window).
     *  It holds the frame's ``bins``, or ``capacity()`` when that is fewer.
     *
     *  **Reading.** Every finished frame is published to a triple buffer:
     *  three frames live in memory allocated by the constructor, the
     *  analyser writes its bins straight into one, the reader holds one,
     *  and the third is the newest finished frame. Publishing and taking a
     *  frame are each one atomic exchange, so neither side copies, waits or
     *  allocates, and a frame the reader holds is never written until the
     *  reader lets go of it by acquiring another. ``acquire()`` takes the
     *  newest finished frame (or keeps the one it holds when none is
     *  newer); ``held()`` looks at the one it holds. Frames are numbered, so
     *  a reader drawing a sonogram sees how many it skipped when it polls
     *  slower than frames finish.
     *
     *  The capacity is the largest ``size()``, fixed by the constructor; the
     *  analyser takes ``7 * capacity`` samples of memory. Nothing allocates
     *  afterwards.
     *
     *  **Threads.** One thread analyses — the block calls, the setters,
     *  ``reset()`` — and one other thread at a time reads with
     *  ``acquire()`` and ``held()``. They may be the same thread.
     */
    class API spectrumAnalyzer {
    public:
      /** @brief What a frame's bins hold. */
      enum MODE {
        AMPLITUDE, ///< each bin's magnitude, scaled to a sine's amplitude
        PHASE, ///< each bin's phase in radians
      };

      /** @brief One finished frame, as the reader sees it.
       *
       *  ``values`` points at ``bins`` values, bin 0 (DC) first. ``size`` is
       *  the FFT size and ``rate`` the sample rate the frame was taken at,
       *  so bin ``j`` is ``j * rate / size`` Hz. ``serial`` counts the
       *  frames the analyser has finished, from 1; a frame with ``serial``
       *  0 is none (``bins`` 0, a null pointer). The pointer stays valid,
       *  and the values do not change, until the reader's next
       *  ``acquire()``.
       */
      struct frame {
        MODE mode = AMPLITUDE;
        UInt size = 0;
        UInt bins = 0;
        UInt serial = 0;
        UInt rate = 0;
        const Flt* values = nullptr;
      };

      /** @brief The FFT size by default. */
      static constexpr UInt DEFAULT_SIZE = 4096;
      /** @brief The smallest FFT size. */
      static constexpr UInt MIN_SIZE = 16;
      /** @brief The largest capacity, and so the largest FFT size. */
      static constexpr UInt MAX_SIZE = 16384;
      /** @brief The update interval by default, in ms (Max's). */
      static constexpr Flt DEFAULT_INTERVAL = 20.f;
      /** @brief The shortest update interval, in ms. */
      static constexpr Flt MIN_INTERVAL = 10.f;
      /** @brief The longest update interval, in ms. */
      static constexpr Flt MAX_INTERVAL = 2000.f;

      /** @brief An analyser of FFT sizes up to @p capacity, made a power of
       *         two from ``MIN_SIZE`` to ``MAX_SIZE``. ``size()`` starts at
       *         ``DEFAULT_SIZE``, or the capacity when that is smaller.
       *         Allocates. */
      explicit spectrumAnalyzer(UInt capacity = MAX_SIZE);

      spectrumAnalyzer(const spectrumAnalyzer&) = delete;
      spectrumAnalyzer& operator=(const spectrumAnalyzer&) = delete;

      /** @brief Sets the FFT size: the power of two at or above
       *         @p samples, ``MIN_SIZE`` to ``capacity()``. Applies from the
       *         next frame. */
      spectrumAnalyzer& size(UInt samples);
      /** @brief The FFT size. */
      UInt size() const;

      /** @brief Sets the update interval in milliseconds, held to
       *         ``MIN_INTERVAL`` to ``MAX_INTERVAL``; NaN is ignored.
       *         Applies from the next wait. */
      spectrumAnalyzer& interval(Flt ms);
      /** @brief The update interval in milliseconds. */
      Flt interval() const;

      /** @brief Sets what the bins hold. Applies from the next frame. */
      spectrumAnalyzer& mode(MODE what);
      /** @brief What the bins hold. */
      MODE mode() const;

      /** @brief The largest ``size()``, fixed by the constructor. */
      UInt capacity() const;

      /** @brief Takes in a block of the signal, finishing a frame at every
       *         interval. */
      void operator()(buffer& in);

      /** @brief Takes in a run of a spectral frame's bins (see the class
       *         notes). */
      void operator()(const stftFrame& bins);

      /** @brief Forgets the samples taken in and starts the interval
       *         again: the next frame comes an interval after the next
       *         sample. Published frames stay. */
      spectrumAnalyzer& reset();

      /** @brief How many frames have finished. Any thread. */
      UInt frames() const;

      /** @brief Reader: takes the newest finished frame, or keeps the one
       *         held when none is newer, and returns it. A frame with
       *         ``serial`` 0 until the first one finishes. */
      frame acquire();

      /** @brief Reader: the frame the last ``acquire()`` returned. */
      frame held() const;

      // --- Helpers for callers that hold settings as floats (the patcher
      // wrapper); any thread.

      /** @brief @p value as an FFT size: the power of two at or above its
       *         whole part, ``MIN_SIZE`` to ``MAX_SIZE``; NaN is
       *         ``DEFAULT_SIZE``. */
      static UInt Size(Flt value);
      /** @brief @p value as a mode, Max's ``phasespect``: a whole part of
       *         1 is ``PHASE``, anything else ``AMPLITUDE``. */
      static MODE Mode(Flt value);

    private:
      struct slot {
        MODE mode = AMPLITUDE;
        UInt size = 0;
        UInt bins = 0;
        UInt serial = 0;
        UInt rate = 0;
      };

      static constexpr unsigned int FRESH = 4;
      static constexpr unsigned int INDEX = 3;

      Dbl wait() const;
      void analyse();
      void publish(UInt fftSize, UInt bins);
      Flt* values(unsigned int s);
      const Flt* values(unsigned int s) const;

      UInt cap;
      std::vector<Flt> history; // the last cap samples, a ring
      std::vector<Flt> shape; // the window, size() values
      std::vector<Flt> re, im; // the transform's work
      std::vector<Flt> store; // three slots of cap values
      slot slots[3];

      // Settings, the analysing thread's.
      UInt n;
      Flt intervalMs;
      MODE what;
      Dbl windowSum;

      // Analysis state, the analysing thread's.
      UInt pos; // where the next sample goes
      Dbl left; // samples to the next frame; below 0 before the first sample
      Dbl elapsed; // spectral frames: samples since the last captured one
      bool capturing; // spectral frames: this one is being captured
      UInt spectralSize;
      UInt spectralBins;
      UInt serial;
      unsigned int writeSlot;

      // The newest finished slot, | FRESH while no reader has taken it.
      std::atomic<unsigned int> published;
      std::atomic<UInt> finished;
      unsigned int readSlot; // the reader's
    };

  } // namespace DSP
} // namespace YSE

#endif // YSE_DSP_ANALYSIS_SPECTRUMANALYZER_HPP
