/*
  ==============================================================================

    firFilter.hpp
    A FIR filter whose taps are read from a sample buffer (issue #1149).

  ==============================================================================
*/

#ifndef YSE_DSP_SAMPLING_FIRFILTER_HPP
#define YSE_DSP_SAMPLING_FIRFILTER_HPP

#include <vector>
#include "../../headers/constants.hpp"
#include "../../headers/defines.hpp"
#include "../../headers/types.hpp"
#include "../buffer.hpp"

namespace YSE {
  namespace DSP {

    class sampleView;

    /**
     *  @brief A finite impulse response filter whose taps are part of a
     *         ``sampleBuffer``: the input convolved with up to
     *         ``MAX_TAPS`` samples of the buffer. What the patcher's
     *         ``~buffir`` does (Max's ``buffir~``).
     *
     *  Put an impulse response or a filter kernel into a buffer and a signal
     *  through this object is filtered by it: custom filters, short
     *  impulse responses (a cabinet, a body, a room's first reflections).
     *
     *  **The taps.** One channel of the buffer (``channel()``, from 0),
     *  ``size()`` frames from frame ``offset()``. Tap ``k`` is frame
     *  ``offset + k``, so the output is
     *  ``y[n] = sum(k = 0 .. taps - 1) tap[k] * x[n - k]``: an impulse in
     *  gives the taps back, in the order they are in the buffer. The size is
     *  held to ``MAX_TAPS`` (Max's limit) and stops at the end of the
     *  buffer. No taps (a size of 0, the default as in Max; an offset at or
     *  past the end; a channel the buffer lacks; an empty or unbound
     *  buffer) outputs silence.
     *
     *  **When the taps are read.** At the start of every block, through the
     *  ``sampleView`` a ``sampleBufferReader`` pins for it: an edit of the
     *  buffer, a new offset or size, is heard from the next block. Each tap
     *  is a relaxed atomic load, so a writer on another render thread may
     *  change them meanwhile.
     *
     *  **History.** The object keeps the last ``MAX_TAPS`` input samples,
     *  whatever the size, so a longer kernel picks up the past at once.
     *  ``clear()`` sets them to 0 (Max's ``clear``), as when a signal stops
     *  and a new one should not ring with the old one. An input that is not
     *  a number stays in the history until it has passed every tap.
     *
     *  Offset and size count frames of the buffer, whatever its rate or the
     *  engine's, so there is no state derived from the sample rate.
     *
     *  .. code-block:: cpp
     *
     *     // control thread
     *     reader.bind(DSP::sampleBufferRegistry::acquire("impulse"));
     *     fir.size(256);
     *
     *     // audio thread, once per block
     *     DSP::sampleView view = reader.pin();
     *     DSP::buffer& out = fir(view, in);
     *
     *  The history and the taps are allocated when the object is made, and
     *  the output is sized to ``STANDARD_BUFFERSIZE``; a shorter block
     *  resizes it down without allocating, and a longer one is cut to
     *  ``STANDARD_BUFFERSIZE``. Processing allocates nothing and takes no
     *  lock. Direct form: a block costs ``taps`` multiply-adds per sample.
     *  Not thread-safe: one thread at a time.
     */
    class API firFilter {
    public:
      /** @brief The most taps a kernel has, as in Max. */
      static constexpr Int MAX_TAPS = 4096;

      /** @brief Allocates the history and taps. Control thread. */
      firFilter();

      /** @brief The channel read, from 0. */
      firFilter& channel(UInt c) {
        targetChannel = c;
        return *this;
      }
      UInt channel() const {
        return targetChannel;
      }

      /** @brief The frame of the first tap. Below 0 is 0. Default 0. */
      firFilter& offset(Int frames) {
        firstFrame = frames < 0 ? 0 : frames;
        return *this;
      }
      Int offset() const {
        return firstFrame;
      }

      /** @brief The number of taps, from 0 to ``MAX_TAPS`` (held there).
       *         Default 0: silence until a size is given, as in Max. */
      firFilter& size(Int taps) {
        tapCount = taps < 0 ? 0 : (taps > MAX_TAPS ? MAX_TAPS : taps);
        return *this;
      }
      Int size() const {
        return tapCount;
      }

      /** @brief Set the input history to 0. */
      firFilter& clear();

      /** @brief The taps the last block used: ``size()`` cut at the end of
       *         the buffer, 0 without a table. */
      UInt taps() const {
        return usedTaps;
      }

      /** @brief One block: @p in filtered. As long as @p in, up to
       *         ``STANDARD_BUFFERSIZE``. */
      buffer& operator()(const sampleView& view, buffer& in);

      /** @brief One block of @p length samples of a constant input @p x. */
      buffer& operator()(const sampleView& view, Flt x, UInt length = STANDARD_BUFFERSIZE);

      /** @brief The output of the last call. */
      buffer& out() {
        return output;
      }

    private:
      // Copy this block's taps out of the view into `kernel`.
      void loadTaps(const sampleView& view);
      // One input sample in, one output sample out.
      Flt step(Flt x);

      UInt targetChannel = 0;
      Int firstFrame = 0;
      Int tapCount = 0;
      UInt usedTaps = 0;

      // The input history twice over: sample x[n - k] is at
      // history[write + k] for every k < MAX_TAPS, so the convolution reads
      // one contiguous run.
      std::vector<Flt> history;
      UInt write = 0;
      std::vector<Flt> kernel;
      buffer output;
    };

  } // namespace DSP
} // namespace YSE

#endif // YSE_DSP_SAMPLING_FIRFILTER_HPP
