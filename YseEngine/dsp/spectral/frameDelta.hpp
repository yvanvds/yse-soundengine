/*
  ==============================================================================

    frameDelta.hpp
    Per-bin difference and running sum across successive frames: the phase
    deviation and running phase of a phase vocoder (issue #1161).

  ==============================================================================
*/

#ifndef YSE_DSP_SPECTRAL_FRAMEDELTA_HPP
#define YSE_DSP_SPECTRAL_FRAMEDELTA_HPP

#include <vector>
#include "../../headers/defines.hpp"
#include "../../headers/types.hpp"
#include "../../headers/constants.hpp"
#include "../buffer.hpp"
#include "stft.hpp"

namespace YSE {
  namespace DSP {

    /**
     *  @brief The change of every bin since the previous frame:
     *         ``out = in - previous``, bin by bin. The phase deviation of a
     *         phase vocoder, given the phases of successive frames.
     *
     *  Ported from MSP's ``framedelta~``. The input is a stream of frames,
     *  handed over as runs of bins (``stftFrame``: ``firstBin``, ``count``
     *  and the frame's ``bins``), with the values in a separate array, since
     *  what is differenced is usually not a bin's real part but its phase.
     *  For each bin ``k = firstBin + i``, ``out[i] = in[i] - previous[k]``,
     *  and ``in[i]`` becomes ``previous[k]`` for the next frame. Every bin
     *  starts at 0, so the first frame comes out as it went in.
     *
     *  The same object serves a whole frame (a ``DSP::stft`` callback,
     *  ``firstBin`` 0, ``count`` = ``bins``) or a frame cut into runs in
     *  bin order (the passes of a ``~pfft``). The block overload treats one
     *  block as a whole frame, as MSP does outside a ``pfft~``: sample ``i``
     *  of a block against sample ``i`` of the block before.
     *
     *  - Bins at or past ``capacity()`` put out 0 and store nothing.
     *  - A run whose frame has a different number of ``bins`` than the
     *    frame before clears every bin first (a new FFT size, or a new block
     *    length with the block overload).
     *  - An input that is not a finite number counts as 0.
     *  - ``in`` and ``out`` may be the same array.
     *
     *  The bins are allocated in the constructor, ``capacity()`` of them,
     *  and never again: processing and ``clear()`` allocate nothing, take no
     *  lock and nothing depends on the sample rate. A block call processes
     *  at most ``STANDARD_BUFFERSIZE`` samples. One thread at a time.
     */
    class API frameDelta {
    public:
      /** @brief The capacity of a default-constructed object: frames of up
       *         to 4096 bins (a half spectrum of an 8192-point FFT). */
      static constexpr UInt DEFAULT_CAPACITY = 4096;
      /** @brief The largest capacity: a full spectrum of ``~pfft``'s
       *         largest FFT. */
      static constexpr UInt MAXIMUM_CAPACITY = 1u << 16;

      /** @brief Room for frames of @p capacity bins (clamped to 1 to
       *         ``MAXIMUM_CAPACITY``). Allocates. */
      explicit frameDelta(UInt capacity = DEFAULT_CAPACITY);

      /** @brief How many bins the object keeps. */
      UInt capacity() const {
        return static_cast<UInt>(last.size());
      }

      /** @brief Sets every stored bin to 0: the next frame comes out as it
       *         goes in. */
      frameDelta& clear();

      /** @brief The value bin @p bin had in the last frame; 0 past the
       *         capacity. */
      Flt previous(UInt bin) const {
        return bin < capacity() ? last[bin] : 0.f;
      }

      /** @brief One run of bins: ``frame.firstBin`` .. ``+ frame.count - 1``
       *         from @p in to @p out (``frame.count`` values each).
       *         ``frame.real`` and ``frame.imaginary`` are not used. */
      void operator()(const stftFrame& frame, const Flt* in, Flt* out);

      /** @brief One run of bins from a block, into the class's own output
       *         (as long as @p in, at most ``STANDARD_BUFFERSIZE``; samples
       *         past ``frame.count`` are 0). */
      buffer& operator()(const stftFrame& frame, buffer& in);

      /** @brief One block as a whole frame of its length: sample ``i``
       *         against sample ``i`` of the block before. */
      buffer& operator()(buffer& in);

    private:
      buffer output;
      std::vector<Flt> last;
      UInt frameBins = 0;
    };

    /**
     *  @brief A running sum of every bin over successive frames:
     *         ``out = previous + in``, bin by bin. The running phase of a
     *         phase vocoder, given successive phase deviations.
     *
     *  Ported from MSP's ``frameaccum~``, the counterpart of ``frameDelta``:
     *  for each bin ``k = firstBin + i``, ``out[i] = sum[k] + in[i]``, which
     *  becomes ``sum[k]``. Every bin starts at 0, so the first frame comes
     *  out as it went in, and ``frameAccumulate`` after ``frameDelta`` gives
     *  the frames back.
     *
     *  With ``wrap()`` on, every sum is wrapped into -pi .. pi as
     *  ``phaseWrap`` does (``pi`` itself gives ``-pi``), and kept wrapped,
     *  so a phase that keeps advancing loses no precision: the output is
     *  that of a ``phaseWrap`` after the unwrapped sum, without the sum
     *  growing. Off (the default), the sums are kept as they are.
     *
     *  Runs of bins, the block overload, the capacity, a change of frame
     *  size, non-finite input (counts as 0), in-place processing, threading
     *  and allocation as ``frameDelta``.
     */
    class API frameAccumulate {
    public:
      static constexpr UInt DEFAULT_CAPACITY = frameDelta::DEFAULT_CAPACITY;
      static constexpr UInt MAXIMUM_CAPACITY = frameDelta::MAXIMUM_CAPACITY;

      /** @brief Room for frames of @p capacity bins (clamped to 1 to
       *         ``MAXIMUM_CAPACITY``), the sums wrapped into -pi .. pi when
       *         @p wrapPhase is set. Allocates. */
      explicit frameAccumulate(UInt capacity = DEFAULT_CAPACITY, bool wrapPhase = false);

      /** @brief How many bins the object keeps. */
      UInt capacity() const {
        return static_cast<UInt>(sum.size());
      }

      /** @brief Wraps the sums into -pi .. pi from the next run on (the
       *         stored sums are wrapped as they are next added to). */
      frameAccumulate& wrap(bool on) {
        wrapping = on;
        return *this;
      }

      /** @brief Whether the sums are wrapped. */
      bool wrap() const {
        return wrapping;
      }

      /** @brief Sets every sum to 0. */
      frameAccumulate& clear();

      /** @brief The sum of bin @p bin after the last frame; 0 past the
       *         capacity. */
      Flt value(UInt bin) const {
        return bin < capacity() ? sum[bin] : 0.f;
      }

      /** @brief One run of bins, as ``frameDelta``. */
      void operator()(const stftFrame& frame, const Flt* in, Flt* out);

      /** @brief One run of bins from a block, into the class's own output. */
      buffer& operator()(const stftFrame& frame, buffer& in);

      /** @brief One block as a whole frame of its length. */
      buffer& operator()(buffer& in);

    private:
      buffer output;
      std::vector<Flt> sum;
      UInt frameBins = 0;
      bool wrapping;
    };

  } // namespace DSP
} // namespace YSE

#endif // YSE_DSP_SPECTRAL_FRAMEDELTA_HPP
