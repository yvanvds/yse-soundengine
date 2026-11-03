/*
  ==============================================================================

    sampleAccess.hpp
    Reading and writing single samples of a sample buffer by index
    (issues #1144, #1145).

  ==============================================================================
*/

#ifndef YSE_DSP_SAMPLING_SAMPLEACCESS_HPP
#define YSE_DSP_SAMPLING_SAMPLEACCESS_HPP

#include "../../headers/constants.hpp"
#include "../../headers/defines.hpp"
#include "../../headers/types.hpp"
#include "../buffer.hpp"

namespace YSE {
  namespace DSP {

    class sampleView;
    class sampleWriteView;

    /**
     *  @brief Reads and writes one sample of a ``sampleBuffer`` at a time, by
     *         its index: what the patcher's ``~peek`` does.
     *
     *  It works on the ``sampleWriteView`` a ``sampleBufferWriter`` pins, so
     *  it allocates nothing and takes no lock, and it may run on any thread
     *  that holds the view: the audio thread, or a control thread with the
     *  audio stopped. Indexes count samples from 0, channels count from 0.
     *  An index or channel outside the buffer reads and writes nothing.
     *
     *  With clipping on (the default), a value written is limited to -1 to
     *  1 first. Not thread-safe: one thread at a time.
     */
    class API sampleAccess {
    public:
      /** @brief The channel read and written, from 0. */
      void channel(UInt c) {
        targetChannel = c;
      }
      UInt channel() const {
        return targetChannel;
      }

      /** @brief Whether a written value is limited to -1 to 1 first. */
      void clip(bool on) {
        clipping = on;
      }
      bool clip() const {
        return clipping;
      }

      /**
       *  @brief The sample at @p index of the channel.
       *
       *  @return false, and @p out untouched, when the index or the channel
       *          is outside the buffer.
       */
      bool read(const sampleWriteView& view, Int index, Flt& out) const;

      /**
       *  @brief Write @p value at @p index of the channel, limited to -1 to
       *         1 when clipping is on.
       *
       *  @return false, and nothing written, when the index or the channel
       *          is outside the buffer.
       */
      bool write(sampleWriteView& view, Int index, Flt value) const;

    private:
      UInt targetChannel = 0;
      bool clipping = true;
    };

    /**
     *  @brief Writes a signal into a ``sampleBuffer`` at indexes given by a
     *         second signal: what the patcher's ``~poke`` does.
     *
     *  Sample ``i`` of the values goes to the frame given by sample ``i`` of
     *  the indexes, truncated to a whole frame. An index below 0 (``-1`` by
     *  convention), not a number, or past the end of the buffer writes
     *  nothing for that sample, so an index signal of -1 pauses writing.
     *  Values are written as they are, without clipping. Writes go into the
     *  buffer in place, through the ``sampleWriteView`` a
     *  ``sampleBufferWriter`` pinned for the block, so every reader of the
     *  buffer sees them at once.
     *
     *  Either input may be a constant instead of a signal. With two signals
     *  the block is as long as the shorter one. A constant value at a
     *  constant index is one write. Every call returns how many samples it
     *  wrote. Processing allocates nothing and takes no lock; there is no
     *  state derived from the sample rate. Not thread-safe: one thread at a
     *  time.
     */
    class API sampleIndexWriter {
    public:
      /** @brief The channel written, from 0. */
      void channel(UInt c) {
        targetChannel = c;
      }
      UInt channel() const {
        return targetChannel;
      }

      /** @brief One block: per-sample values at per-sample indexes. */
      UInt operator()(sampleWriteView& view, buffer& values, buffer& indexes) const;

      /** @brief One block: a constant value at per-sample indexes. */
      UInt operator()(sampleWriteView& view, Flt value, buffer& indexes) const;

      /** @brief One block: per-sample values, all at one index. Each
       *         would overwrite the one before, so only the block's last
       *         value is written: one write. */
      UInt operator()(sampleWriteView& view, buffer& values, Flt index) const;

      /** @brief A constant value at a constant index: one write. */
      UInt operator()(sampleWriteView& view, Flt value, Flt index) const;

    private:
      UInt targetChannel = 0;
    };

    /**
     *  @brief Reads a ``sampleBuffer`` at sample indexes given by a signal,
     *         without interpolation: what the patcher's ``~index`` does.
     *
     *  Output sample ``i`` is the buffer's sample at the frame given by
     *  sample ``i`` of the indexes, truncated toward 0 to a whole frame
     *  (the same frame ``sampleIndexWriter`` writes for that index, so a
     *  ``~poke`` and an ``~index`` driven by one index signal meet). An index
     *  below 0, or not a number, reads the first frame; one past the end
     *  reads the last. A channel the buffer lacks, or an empty or unbound
     *  buffer, reads silence.
     *
     *  It works on the ``sampleView`` a ``sampleBufferReader`` pins for the
     *  block. Every read is a relaxed atomic load, so a writer on another
     *  render thread may write the same samples meanwhile. Indexes count
     *  samples of the buffer, whatever its rate or the engine's, so there is
     *  no state derived from the sample rate.
     *
     *  .. code-block:: cpp
     *
     *     // control thread
     *     reader.bind(DSP::sampleBufferRegistry::acquire("table"));
     *
     *     // audio thread, once per block
     *     DSP::sampleView view = reader.pin();
     *     DSP::buffer& out = index(view, indexes);
     *
     *  The output is sized to ``STANDARD_BUFFERSIZE`` when the object is
     *  made; a shorter block resizes it down without allocating, and a longer
     *  one is cut to ``STANDARD_BUFFERSIZE``. Processing allocates nothing
     *  and takes no lock. Not thread-safe: one thread at a time.
     */
    class API sampleIndexReader {
    public:
      sampleIndexReader();

      /** @brief The channel read, from 0. */
      void channel(UInt c) {
        targetChannel = c;
      }
      UInt channel() const {
        return targetChannel;
      }

      /** @brief One sample: the channel's sample at @p index, truncated and
       *         clamped to the buffer; 0 for a channel the buffer lacks or
       *         an empty buffer. */
      Flt read(const sampleView& view, Flt index) const;

      /** @brief One block: the sample at each index. As long as
       *         @p indexes, up to ``STANDARD_BUFFERSIZE``. */
      buffer& operator()(const sampleView& view, buffer& indexes);

      /** @brief One block of @p length samples, all read at @p index. */
      buffer& operator()(const sampleView& view, Flt index, UInt length = STANDARD_BUFFERSIZE);

      /** @brief The output of the last call. */
      buffer& out() {
        return output;
      }

    private:
      UInt targetChannel = 0;
      buffer output;
    };

  } // namespace DSP
} // namespace YSE

#endif // YSE_DSP_SAMPLING_SAMPLEACCESS_HPP
