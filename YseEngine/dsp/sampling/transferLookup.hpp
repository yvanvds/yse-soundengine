/*
  ==============================================================================

    transferLookup.hpp
    A sample buffer used as a transfer function: a signal from -1 to 1 looks
    up a value in it (issue #1147).

  ==============================================================================
*/

#ifndef YSE_DSP_SAMPLING_TRANSFERLOOKUP_HPP
#define YSE_DSP_SAMPLING_TRANSFERLOOKUP_HPP

#include "../../headers/constants.hpp"
#include "../../headers/defines.hpp"
#include "../../headers/types.hpp"
#include "../buffer.hpp"

namespace YSE {
  namespace DSP {

    class sampleView;

    /**
     *  @brief Uses part of a ``sampleBuffer`` as a transfer function: each
     *         input sample from -1 to 1 is a position in the table, and the
     *         output is the value found there. What the patcher's
     *         ``~lookup`` does (Max's ``lookup~``).
     *
     *  Draw a curve into a buffer and a signal fed through it is shaped by
     *  that curve: waveshaping distortion. A straight line from -1 to 1 lets
     *  the signal through unchanged.
     *
     *  **The table.** One channel of the buffer (``channel()``, from 0). The
     *  table runs from frame ``offset()`` up to, not including, frame
     *  ``size()``, so it holds ``size() - offset()`` entries: as in Max, the
     *  size counts from the start of the buffer, and a larger offset makes
     *  the table shorter. A size past the end of the buffer stops at its
     *  last frame, so a short buffer is used whole at the default size of
     *  512. An offset at or past the end of the table leaves no entries.
     *
     *  **The lookup.** -1 reads the first entry, 1 the last, 0 the middle of
     *  the table, and the positions between are spread evenly across it: an
     *  input ``x`` reads position ``offset + (x + 1) / 2 * (entries - 1)``.
     *  A position between two entries reads the straight line between them
     *  (Max's reference does not say; a drawn curve of a few hundred points
     *  would otherwise be heard as steps). A table of one entry reads it for
     *  every input.
     *
     *  **Silence.** An input beyond -1 or 1, or not a number, outputs 0 (Max:
     *  "Input amplitude exceeding the range from -1 to 1 results in an
     *  output of 0"). So do a table without entries, a channel the buffer
     *  lacks, and an empty or unbound buffer.
     *
     *  It works on the ``sampleView`` a ``sampleBufferReader`` pins for the
     *  block. Every read is a relaxed atomic load, so a writer on another
     *  render thread may change the curve meanwhile. Offset and size count
     *  frames of the buffer, whatever its rate or the engine's, so there is
     *  no state derived from the sample rate.
     *
     *  .. code-block:: cpp
     *
     *     // control thread
     *     reader.bind(DSP::sampleBufferRegistry::acquire("curve"));
     *     shaper.size(1024);
     *
     *     // audio thread, once per block
     *     DSP::sampleView view = reader.pin();
     *     DSP::buffer& out = shaper(view, in);
     *
     *  The output is sized to ``STANDARD_BUFFERSIZE`` when the object is
     *  made; a shorter block resizes it down without allocating, and a longer
     *  one is cut to ``STANDARD_BUFFERSIZE``. Processing allocates nothing
     *  and takes no lock. Not thread-safe: one thread at a time.
     */
    class API transferLookup {
    public:
      /** @brief The size a new object starts with, as in Max. */
      static constexpr Int DEFAULT_SIZE = 512;

      transferLookup();

      /** @brief The channel read, from 0. */
      transferLookup& channel(UInt c) {
        targetChannel = c;
        return *this;
      }
      UInt channel() const {
        return targetChannel;
      }

      /** @brief The first frame of the table, the one -1 reads. Below 0 is
       *         0. Default 0. */
      transferLookup& offset(Int frames) {
        firstFrame = frames < 0 ? 0 : frames;
        return *this;
      }
      Int offset() const {
        return firstFrame;
      }

      /** @brief The frame the table ends before, counted from the start of
       *         the buffer: the table holds ``size - offset`` entries. Below
       *         0 is 0. Default 512. */
      transferLookup& size(Int frames) {
        endFrame = frames < 0 ? 0 : frames;
        return *this;
      }
      Int size() const {
        return endFrame;
      }

      /** @brief One sample: the table's value at input @p x, or 0 when @p x
       *         is outside -1 to 1 or there is no table. */
      Flt read(const sampleView& view, Flt x) const;

      /** @brief One block: the value at each input sample. As long as
       *         @p in, up to ``STANDARD_BUFFERSIZE``. */
      buffer& operator()(const sampleView& view, buffer& in);

      /** @brief One block of @p length samples, all read at input @p x. */
      buffer& operator()(const sampleView& view, Flt x, UInt length = STANDARD_BUFFERSIZE);

      /** @brief The output of the last call. */
      buffer& out() {
        return output;
      }

    private:
      UInt targetChannel = 0;
      Int firstFrame = 0;
      Int endFrame = DEFAULT_SIZE;
      buffer output;
    };

  } // namespace DSP
} // namespace YSE

#endif // YSE_DSP_SAMPLING_TRANSFERLOOKUP_HPP
