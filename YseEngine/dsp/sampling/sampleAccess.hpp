/*
  ==============================================================================

    sampleAccess.hpp
    Reading and writing single samples of a sample buffer by index
    (issue #1144).

  ==============================================================================
*/

#ifndef YSE_DSP_SAMPLING_SAMPLEACCESS_HPP
#define YSE_DSP_SAMPLING_SAMPLEACCESS_HPP

#include "../../headers/defines.hpp"
#include "../../headers/types.hpp"
#include "../buffer.hpp"

namespace YSE {
  namespace DSP {

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

  } // namespace DSP
} // namespace YSE

#endif // YSE_DSP_SAMPLING_SAMPLEACCESS_HPP
