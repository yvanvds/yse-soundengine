/*
  ==============================================================================

    frameAverage.hpp
    Per-bin running average over the last frames, and a smoothing along each
    frame that never reaches across into another (issue #1162).

  ==============================================================================
*/

#ifndef YSE_DSP_SPECTRAL_FRAMEAVERAGE_HPP
#define YSE_DSP_SPECTRAL_FRAMEAVERAGE_HPP

#include <vector>
#include "../../headers/defines.hpp"
#include "../../headers/types.hpp"
#include "../../headers/constants.hpp"
#include "../buffer.hpp"
#include "stft.hpp"

namespace YSE {
  namespace DSP {

    /**
     *  @brief The running average of every bin over the last
     *         ``frameCount()`` frames: a smoothing in time, bin by bin.
     *
     *  Ported from MSP's ``frameaverage~``. The input is a stream of
     *  frames, handed over as runs of bins (``stftFrame``: ``firstBin``,
     *  ``count``, the frame's ``bins`` and its ``index``), with the values in
     *  a separate array, as ``frameDelta`` takes them. For each bin
     *  ``k = firstBin + i`` the output is the mean of bin ``k`` over the
     *  current frame and the ``frameCount() - 1`` frames before it:
     *  ``out[i] = (in[i] + x[m-1][k] + ... + x[m-n+1][k]) / n``. Every bin
     *  starts at 0, so the first frames come out scaled down, the first one
     *  by ``1 / n``, and reach the average once ``n`` frames are in.
     *
     *  The same object serves a whole frame (a ``DSP::stft`` callback) or a
     *  frame cut into runs in bin order (the passes of a ``~pfft``). A run
     *  begins a new frame when its ``index`` differs from the run before or
     *  its ``firstBin`` is not past the one before; every frame should hand
     *  over the same bins.
     *
     *  The block overload cuts a stream of samples into frames: of
     *  ``frameSize()`` samples, sample ``i`` of a frame being bin ``i``, the
     *  frames running on across blocks (a ``streamingFft`` output, MSP's
     *  ``framesize`` with ``fft~``), or, with ``frameSize()`` 0 (the
     *  default), of one block each, as MSP treats the signal vector outside
     *  a ``pfft~``.
     *
     *  - Bins at or past ``capacity()`` put out 0 and store nothing.
     *  - A run whose frame has a different number of ``bins`` than the
     *    frame before clears every bin first.
     *  - An input that is not a finite number counts as 0.
     *  - ``in`` and ``out`` may be the same array.
     *  - The sums are kept in double precision and updated with each frame
     *    (in, oldest out), so a frame costs the same at any ``frameCount()``.
     *
     *  The history, ``maxFrames() x capacity()`` values, is allocated in the
     *  constructor and never again: processing, ``frameCount()`` and
     *  ``clear()`` allocate nothing, take no lock and nothing depends on the
     *  sample rate. A block call processes at most ``STANDARD_BUFFERSIZE``
     *  samples. One thread at a time.
     */
    class API frameAverage {
    public:
      /** @brief The frames averaged by a default-constructed object. */
      static constexpr UInt DEFAULT_FRAMES = 4;
      /** @brief The most frames an object can average. */
      static constexpr UInt MAXIMUM_FRAMES = 256;
      /** @brief The capacity of a default-constructed object: frames of up
       *         to 4096 bins (a half spectrum of an 8192-point FFT). */
      static constexpr UInt DEFAULT_CAPACITY = 4096;
      /** @brief The largest capacity: a full spectrum of ``~pfft``'s
       *         largest FFT. */
      static constexpr UInt MAXIMUM_CAPACITY = 1u << 16;
      /** @brief The longest frame the block overload cuts a stream into. */
      static constexpr UInt MAXIMUM_FRAME_SIZE = 1u << 16;

      /** @brief Room to average up to @p maxFrames frames (clamped to 1 to
       *         ``MAXIMUM_FRAMES``) of @p capacity bins (clamped to 1 to
       *         ``MAXIMUM_CAPACITY``); ``frameCount()`` starts at
       *         @p maxFrames. Allocates. */
      explicit frameAverage(UInt maxFrames = DEFAULT_FRAMES, UInt capacity = DEFAULT_CAPACITY);

      /** @brief How many bins the object keeps. */
      UInt capacity() const {
        return static_cast<UInt>(sums.size());
      }

      /** @brief The most frames ``frameCount()`` can be set to. */
      UInt maxFrames() const {
        return slots;
      }

      /** @brief Averages over @p frames frames (clamped to 1 to
       *         ``maxFrames()``), MSP's ``framecount``. A change clears. */
      frameAverage& frameCount(UInt frames);

      /** @brief The frames averaged. */
      UInt frameCount() const {
        return frames;
      }

      /** @brief For the block overload: frames of @p samples samples
       *         (clamped to ``MAXIMUM_FRAME_SIZE``), across blocks; 0 makes
       *         every block a frame. MSP's ``framesize``. A change starts the
       *         next block on a new frame, at bin 0. */
      frameAverage& frameSize(UInt samples);

      /** @brief The frame size of the block overload; 0: a block. */
      UInt frameSize() const {
        return stream.size;
      }

      /** @brief Sets every stored frame to 0: the next frames come out
       *         scaled down again, as at the start. The block overload's
       *         place in its frame is kept. */
      frameAverage& clear();

      /** @brief The average of bin @p bin after the last frame; 0 past the
       *         capacity. */
      Flt value(UInt bin) const;

      /** @brief One run of bins: ``frame.firstBin`` .. ``+ frame.count - 1``
       *         from @p in to @p out (``frame.count`` values each).
       *         ``frame.real`` and ``frame.imaginary`` are not used. */
      void operator()(const stftFrame& frame, const Flt* in, Flt* out);

      /** @brief One run of bins from a block, into the class's own output
       *         (as long as @p in, at most ``STANDARD_BUFFERSIZE``; samples
       *         past ``frame.count`` are 0). */
      buffer& operator()(const stftFrame& frame, buffer& in);

      /** @brief One block of a stream of frames of ``frameSize()`` samples,
       *         or a whole frame of its length with ``frameSize()`` 0. */
      buffer& operator()(buffer& in);

      /** @brief Where the block overload is in a stream of frames. */
      struct streamPlace {
        UInt size = 0; ///< frame size; 0: a frame per block
        UInt position = 0; ///< the bin the next sample is
        U64 frame = 0; ///< frames begun so far
      };

    private:
      buffer output;
      std::vector<Flt> history; // slots x capacity, slot by slot
      std::vector<double> sums;
      UInt slots;
      UInt frames;
      UInt slot = 0; // the history slot of the current frame
      UInt frameBins = 0;
      UInt lastFirst = 0;
      U64 lastIndex = 0;
      bool started = false;
      streamPlace stream;
    };

    /**
     *  @brief A running average along each frame, over the last
     *         ``smoothness()`` values of the same frame: a smoothing in
     *         frequency that never blurs one frame into the next.
     *
     *  Ported from MSP's ``framesmooth~``. Frames come as runs of bins as
     *  for ``frameAverage``; for the value at bin ``k`` of a frame the
     *  output is the mean of it and the ``smoothness() - 1`` values before
     *  it in the same frame: ``out[k] = (x[k] + x[k-1] + ... + x[k-s+1]) /
     *  s``. At the start of a frame there are fewer values before it, and
     *  the mean is over those there are, so bin 0 comes out as it went in,
     *  bin 1 as the mean of bins 0 and 1, and so on. Nothing of one frame
     *  reaches into the next.
     *
     *  The window trails the bin, so that a frame can come in runs, a pass
     *  of a ``~pfft`` after the other: it needs no bin of a later run. A
     *  feature of the spectrum moves up by ``(smoothness() - 1) / 2`` bins.
     *
     *  A run continues the frame when it has the same ``index`` and
     *  ``bins`` as the run before and starts at the bin after its last;
     *  any other run starts a new window at its first bin. No per-bin
     *  state, so no capacity: frames of any size. The block overload, its
     *  ``frameSize()``, non-finite input (counts as 0) and in-place
     *  processing as ``frameAverage``.
     *
     *  The last ``MAXIMUM_SMOOTHNESS`` values are kept in storage allocated
     *  in the constructor: processing, ``smoothness()`` and ``clear()``
     *  allocate nothing, take no lock, and nothing depends on the sample
     *  rate. One thread at a time.
     */
    class API frameSmooth {
    public:
      /** @brief The values averaged by a default-constructed object. */
      static constexpr UInt DEFAULT_SMOOTHNESS = 3;
      /** @brief The most values the window can hold. */
      static constexpr UInt MAXIMUM_SMOOTHNESS = 1024;
      /** @brief The longest frame the block overload cuts a stream into. */
      static constexpr UInt MAXIMUM_FRAME_SIZE = frameAverage::MAXIMUM_FRAME_SIZE;

      /** @brief Averages over @p smoothness values (clamped to 1 to
       *         ``MAXIMUM_SMOOTHNESS``). Allocates. */
      explicit frameSmooth(UInt smoothness = DEFAULT_SMOOTHNESS);

      /** @brief Averages over @p values values (clamped to 1 to
       *         ``MAXIMUM_SMOOTHNESS``) from the next bin on, MSP's
       *         ``smoothness``; the window takes in the values before it in
       *         the frame at once. */
      frameSmooth& smoothness(UInt values);

      /** @brief The values averaged. */
      UInt smoothness() const {
        return size;
      }

      /** @brief For the block overload: frames of @p samples samples
       *         (clamped to ``MAXIMUM_FRAME_SIZE``), across blocks; 0 makes
       *         every block a frame. A change starts the next block on a new
       *         frame, at bin 0. */
      frameSmooth& frameSize(UInt samples);

      /** @brief The frame size of the block overload; 0: a block. */
      UInt frameSize() const {
        return stream.size;
      }

      /** @brief Forgets the values before: the next bin starts a new
       *         window. The block overload's place in its frame is kept. */
      frameSmooth& clear();

      /** @brief One run of bins, as ``frameAverage``. */
      void operator()(const stftFrame& frame, const Flt* in, Flt* out);

      /** @brief One run of bins from a block, into the class's own output. */
      buffer& operator()(const stftFrame& frame, buffer& in);

      /** @brief One block of a stream of frames, as ``frameAverage``. */
      buffer& operator()(buffer& in);

    private:
      void resum();

      buffer output;
      std::vector<Flt> recent; // ring of the last MAXIMUM_SMOOTHNESS values
      double sum = 0.0; // of the last `held` values
      UInt size;
      UInt head = 0; // where the next value goes
      UInt held = 0; // values in the window
      UInt taken = 0; // values of this frame so far, up to MAXIMUM_SMOOTHNESS
      UInt nextBin = 0;
      UInt frameBins = 0;
      U64 lastIndex = 0;
      bool started = false;
      frameAverage::streamPlace stream;
    };

  } // namespace DSP
} // namespace YSE

#endif // YSE_DSP_SPECTRAL_FRAMEAVERAGE_HPP
