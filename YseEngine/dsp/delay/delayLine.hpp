/*
  ==============================================================================

    delayLine.hpp
    A delay line shared by any number of taps (issues #1038, #1105).

  ==============================================================================
*/

#ifndef YSE_DSP_DELAY_DELAYLINE_HPP
#define YSE_DSP_DELAY_DELAYLINE_HPP

#include <vector>
#include "../../headers/defines.hpp"
#include "../../headers/types.hpp"
#include "../../headers/constants.hpp"
#include "../buffer.hpp"

namespace YSE {
  namespace DSP {

    class delayTap;

    /**
     *  @brief A delay line that one writer fills and any number of
     *         ``delayTap`` readers read at their own delay times.
     *
     *  This is the memory behind Max's ``tapin~``. Write one block into it
     *  with ``write``, and read it back with as many ``delayTap`` objects as
     *  you like.
     *
     *  **Blocks.** Call ``beginBlock`` once at the start of every block,
     *  before ``write`` and before any tap reads. Taps read relative to the
     *  position the line had at that call, and never closer than one block.
     *  So a tap reads exactly the same samples whether it runs before or
     *  after this block's ``write``. That is what makes feedback through a
     *  delay line legal in the patcher (issue #1038): ``~tapout`` does not
     *  have to wait for ``~tapin``, and the result does not depend on which
     *  one runs first.
     *
     *  **Memory.** The constructor allocates the line for the longest delay
     *  it will ever hold, at the highest sample rate it supports, plus one
     *  block. Nothing allocates after that. ``maximumDelay`` can lower or
     *  raise the usable length within that capacity, but never beyond it.
     *  The line follows ``SAMPLERATE`` on every block: at a lower rate it
     *  simply uses less of its memory.
     *
     *  **Freeze.** A frozen line records nothing new. It loops the last
     *  ``maximumDelay`` milliseconds it holds, so taps keep replaying them.
     *
     *  Apart from the constructor, call it only from the audio thread. Blocks
     *  are at most ``STANDARD_BUFFERSIZE`` samples (a longer one is written
     *  up to that length).
     */
    class API delayLine {
    public:
      /** @brief The sample rate the default constructor sizes the line for. */
      static constexpr UInt HIGHEST_SAMPLERATE = 192000;

      /**
       *  @brief A silent line holding up to @p maximumDelayMs milliseconds.
       *
       *  @param maximumDelayMs     The longest delay, and the capacity. At
       *                            least 0.
       *  @param highestSampleRate  The highest rate the capacity is sized
       *                            for. At a higher rate the longest delay is
       *                            shorter than @p maximumDelayMs.
       */
      explicit delayLine(Flt maximumDelayMs = 100.f, UInt highestSampleRate = HIGHEST_SAMPLERATE);

      /**
       *  @brief Set the longest delay in milliseconds, and clear the line.
       *
       *  Clamped to 0 .. ``capacity()``. Does not allocate.
       */
      delayLine& maximumDelay(Flt ms);

      /** @brief The longest delay in milliseconds. */
      Flt maximumDelay() const;

      /** @brief The longest ``maximumDelay`` the memory allows, in ms. */
      Flt capacity() const;

      /** @brief Start a block: taps read relative to the line's position now. */
      delayLine& beginBlock();

      /** @brief Record one block, or loop the line when frozen. */
      delayLine& write(buffer& in);

      /** @brief Silence the whole line. */
      delayLine& clear();

      /** @brief Stop recording and loop what the line holds, or resume. */
      delayLine& freeze(bool on);

      /** @brief Whether the line is frozen. */
      bool frozen() const;

    private:
      friend class delayTap;

      // The usable delay in samples at the current SAMPLERATE, for a block of
      // `length` samples: maximumDelay, held inside the memory so neither the
      // reads nor this block's write can wrap onto each other.
      UInt longestDelay(UInt length) const;

      // The sample `offset` positions from the block origin (negative: older).
      inline Flt at(Int offset) const {
        Int pos = (static_cast<Int>(origin) + offset) % static_cast<Int>(size);
        if (pos < 0) pos += static_cast<Int>(size);
        return line[static_cast<std::size_t>(pos)];
      }

      std::vector<Flt> line;
      UInt size;
      UInt writePos;
      UInt origin;
      Flt longestMs;
      Flt capacityMs;
      bool isFrozen;
    };

  } // namespace DSP
} // namespace YSE

#endif // YSE_DSP_DELAY_DELAYLINE_HPP
