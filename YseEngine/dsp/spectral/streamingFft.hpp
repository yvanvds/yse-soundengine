/*
  ==============================================================================

    streamingFft.hpp
    Fourier transforms of a sample stream, frame by frame: the input taken
    in as signals, the transform put out as signals one bin per sample
    (issue #1160).

  ==============================================================================
*/

#ifndef YSE_DSP_SPECTRAL_STREAMINGFFT_HPP
#define YSE_DSP_SPECTRAL_STREAMINGFFT_HPP

#include <vector>
#include "../../headers/defines.hpp"
#include "../../headers/types.hpp"
#include "../../headers/constants.hpp"
#include "../buffer.hpp"
#include "frameClock.hpp"

namespace YSE {
  namespace DSP {

    /**
     *  @brief The shared implementation of ``streamingFft`` and
     *         ``streamingInverseFft``: a complex Fourier transform of
     *         successive frames of a stream, put out as a stream.
     *
     *  Use one of the two classes below; this one only holds what they
     *  share.
     *
     *  **Framing.** The frames follow a ``frameClock``: ``size()`` samples
     *  (a power of two, 16 to ``MAXIMUM_SIZE``), one frame every
     *  ``interval()`` samples (a power of two, at least the size), the
     *  first ``offset()`` samples into the stream. The real and imaginary
     *  input are taken in at positions ``0 .. size - 1`` of each period;
     *  with an interval above the size, the samples after them are not
     *  used.
     *
     *  **Output.** The sample after a frame's last one, the frame is
     *  transformed and put out over the next ``size()`` samples: sample
     *  ``k`` of the output period is bin ``k`` (or, inverse, time sample
     *  ``k``), real part on ``real()``, imaginary part on ``imaginary()``,
     *  and ``sync()`` is ``k``. Outside an output period (before the first
     *  frame completes, and after the ``size()`` output samples when the
     *  interval is larger) all three are 0. So the output lags the input by
     *  ``latency()``, one frame: with an interval equal to the size the
     *  frames follow each other without a gap, each put out while the next
     *  is taken in.
     *
     *  The transform runs at the exact sample its frame completes, inside
     *  whatever block that is, so the output does not depend on the block
     *  size. The stream reads as 0 before its first sample (see
     *  ``frameClock``).
     *
     *  **Scaling.** Neither direction scales: a frame put through
     *  ``streamingFft`` and then ``streamingInverseFft`` comes back
     *  ``size()`` times larger (and two frames later), as with ``fft`` and
     *  ``inverseFft``.
     *
     *  The frame storage is allocated in the constructor and never again.
     *  Each block call processes at most ``STANDARD_BUFFERSIZE`` samples,
     *  as many as its shortest input. Nothing depends on the sample rate.
     *  Call it from one thread at a time. Processing, ``offset()`` and
     *  ``reset()`` allocate nothing.
     */
    class API streamingTransform {
    public:
      /** @brief The frame size of a default-constructed object. */
      static constexpr UInt DEFAULT_SIZE = 512;
      /** @brief The smallest frame size. */
      static constexpr UInt MINIMUM_SIZE = 16;
      /** @brief The largest frame size. */
      static constexpr UInt MAXIMUM_SIZE = 1u << 16;
      /** @brief The largest interval. */
      static constexpr UInt MAXIMUM_INTERVAL = 1u << 24;

      /** @brief A frame size as the class uses it: the power of two at or
       *         above @p size, from ``MINIMUM_SIZE`` to ``MAXIMUM_SIZE``. */
      static UInt fitSize(UInt size);

      /** @brief An interval as the class uses it with frames of @p size
       *         (already fitted): the power of two at or above
       *         @p interval, from @p size to ``MAXIMUM_INTERVAL``; 0 means
       *         the size. */
      static UInt fitInterval(UInt interval, UInt size);

      /** @brief The frame size. */
      UInt size() const {
        return clock.size();
      }

      /** @brief Samples from the start of one frame to the next. */
      UInt interval() const {
        return clock.interval();
      }

      /** @brief Samples into the stream that the first frame starts, ``0``
       *         to ``interval() - 1``. */
      UInt offset() const {
        return clock.offset();
      }

      /** @brief A new offset, taken modulo the interval (MSP's ``phase``).
       *         It moves the frames along the stream from now on: the
       *         samples taken in so far stay where they are in the frame,
       *         the next one goes to its place under the new offset, and an
       *         output period that has started runs on unless a frame
       *         completes first. */
      void offset(UInt samples) {
        clock.offset(samples);
      }

      /** @brief Back to the start: the frame and the output cleared, the
       *         next sample is sample 0 of the stream. */
      void reset();

      /** @brief Samples from a frame's first input sample to its first
       *         output sample: ``size()``. */
      UInt latency() const {
        return clock.size();
      }

      /** @brief One block: @p real and @p imaginary taken in, the result's
       *         real part returned. */
      buffer& operator()(buffer& real, buffer& imaginary);

      /** @brief One block of a real input: the imaginary input is 0. */
      buffer& operator()(buffer& real);

      /** @brief The real part put out by the last call. */
      buffer& real() {
        return realOut;
      }

      /** @brief The imaginary part put out by the last call. */
      buffer& imaginary() {
        return imaginaryOut;
      }

      /** @brief The index of the bin (or sample) put out, ``0`` to
       *         ``size() - 1``; 0 outside an output period. */
      buffer& sync() {
        return syncOut;
      }

    protected:
      streamingTransform(bool inverse, UInt size, UInt interval, UInt offset);

    private:
      buffer& process(const Flt* realIn, const Flt* imaginaryIn, UInt length);
      void transform();

      bool inverse;
      frameClock clock;
      std::vector<Flt> frameReal, frameImaginary; // the frame being taken in
      std::vector<Flt> outReal, outImaginary; // the last frame transformed
      UInt outIndex = 0; // the next output sample of the output period
      bool outputting = false; // inside an output period
      buffer realOut, imaginaryOut, syncOut;
    };

    /**
     *  @brief A Fourier transform of a stream, frame by frame: every
     *         ``interval()`` samples, a frame of ``size()`` input samples
     *         is transformed and put out as a stream of bins, one per
     *         sample, over the next ``size()`` samples.
     *
     *  Ported from MSP's ``fft~``; see ``streamingTransform`` for the
     *  framing, timing and scaling. Bin ``k`` of a frame ``x`` is
     *  ``X(k) = sum over n of x(n) * e^(-2 pi i k n / size)``, for ``k``
     *  from 0 to ``size() - 1``: the whole spectrum, the bins above
     *  ``size() / 2`` the mirror (complex conjugate) of those below for a
     *  real input. The bin at ``k`` is ``k * SAMPLERATE / size()`` Hz.
     *
     *  For a spectral analysis outside a frame-based patch: an impulse at
     *  the first sample of a frame gives 1 in every bin, a cosine of ``c``
     *  cycles per frame ``size() / 2`` in bins ``c`` and ``size() - c``.
     */
    class API streamingFft : public streamingTransform {
    public:
      /** @brief Frames of @p size samples (fitted with ``fitSize``) every
       *         @p interval samples (``fitInterval``; 0: the size), the
       *         first @p offset samples into the stream. Allocates. */
      explicit streamingFft(UInt size = DEFAULT_SIZE, UInt interval = 0, UInt offset = 0)
        : streamingTransform(false, size, interval, offset) {}
    };

    /**
     *  @brief An inverse Fourier transform of a stream of bins, frame by
     *         frame: every ``interval()`` samples, a frame of ``size()``
     *         input samples is read as a spectrum, bin ``k`` at sample
     *         ``k``, and put out as a stream of samples over the next
     *         ``size()`` samples.
     *
     *  Ported from MSP's ``ifft~``; see ``streamingTransform`` for the
     *  framing, timing and scaling. Sample ``n`` of the output is
     *  ``x(n) = sum over k of X(k) * e^(2 pi i k n / size)``, not divided
     *  by the size. Fed the bins of ``streamingFft`` with the same size and
     *  interval, and an offset ``size()`` later (the same offset when the
     *  interval equals the size), it gives back the input ``size()`` times
     *  larger, ``2 * size()`` samples later.
     */
    class API streamingInverseFft : public streamingTransform {
    public:
      /** @brief Frames of @p size bins (fitted with ``fitSize``) every
       *         @p interval samples (``fitInterval``; 0: the size), the
       *         first @p offset samples into the stream. Allocates. */
      explicit streamingInverseFft(UInt size = DEFAULT_SIZE, UInt interval = 0, UInt offset = 0)
        : streamingTransform(true, size, interval, offset) {}
    };

  } // namespace DSP
} // namespace YSE

#endif // YSE_DSP_SPECTRAL_STREAMINGFFT_HPP
