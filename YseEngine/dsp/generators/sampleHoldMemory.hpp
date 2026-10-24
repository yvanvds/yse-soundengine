/*
  ==============================================================================

    sampleHoldMemory.hpp
    Sample and hold with a memory of past values, recorded and played back
    by two independent triggers (issue #1083).

  ==============================================================================
*/

#ifndef YSE_DSP_GENERATORS_SAMPLEHOLDMEMORY_HPP
#define YSE_DSP_GENERATORS_SAMPLEHOLDMEMORY_HPP

#include <cstdint>
#include <vector>
#include "../../headers/defines.hpp"
#include "../../headers/types.hpp"
#include "../../headers/constants.hpp"
#include "../buffer.hpp"

namespace YSE {
  namespace DSP {

    /**
     *  @brief Sample and hold with memory: samples an input into a ring of
     *         stored values and plays them back on a separate trigger, so a
     *         sequence of sampled values can loop: a random melody that
     *         repeats.
     *
     *  Ported from Max's ``sash~``. Three signals go in each sample: the
     *  **input**, the **sample trigger** and the **advance trigger**.
     *
     *  **Recording.** Each time the sample trigger rises from at or below
     *  ``sampleLevel()`` to above it, the input is written into the memory at
     *  the write position, which then moves forward one place, wrapping at
     *  ``size()``. An input sample that is not a finite number is not
     *  recorded and the write position stays.
     *
     *  **Playback.** The output is the memory at the read position, which
     *  ``mode()`` moves:
     *
     *  - ``LEVEL`` (the default): each time the advance trigger rises from at
     *    or below ``advanceLevel()`` to above it, the read position moves as
     *    ``direction()`` says: ``FORWARD`` one place on (the default),
     *    ``REVERSE`` one place back, ``RANDOM`` to any place (with
     *    replacement), ``URN`` to a place not read since the last round (each
     *    place once per ``size()`` advances, in random order; a change of
     *    size or direction starts a new round).
     *  - ``INDEX``: the read position is the advance trigger's value, rounded
     *    down and wrapped into the memory (-1 is the last place).
     *  - ``SUBDIVISION``: the advance trigger is a 0..1 phase (wrapped into
     *    0..1) divided into ``size()`` equal parts, and the read position is
     *    the part it is in: a ``phasor`` plays the memory once per cycle.
     *
     *  In a sample, recording comes first, then the read position moves,
     *  then the output is read: with a size of 1 and the same trigger on
     *  both, this is a plain sample and hold. A trigger sample that is not a
     *  finite number is skipped (the next one is compared with the one
     *  before it). Both previous trigger samples start at 0.
     *
     *  **The memory** holds ``size()`` values, 1 (the default) up to
     *  ``maxSize()`` (1000 unless the constructor or ``maxSize()`` says
     *  otherwise), all 0 at first. ``load()`` fills it from a list and sets
     *  the size to the list's length, as Max's ``list`` does; recording then
     *  starts again at the first place. Changing the size keeps the values
     *  and wraps the positions into the new size. Max leaves the defaults of
     *  the size and the two levels unstated; here they are 1, 0.5 and 0.5,
     *  so a ``phasor`` triggers halfway through each cycle and a 0/1 pulse
     *  on each rise.
     *
     *  Nothing depends on the sample rate. Each call processes at most
     *  ``STANDARD_BUFFERSIZE`` samples (the shortest of the three buffers).
     *  Call it from one thread at a time. Processing allocates nothing: the
     *  output is sized to ``STANDARD_BUFFERSIZE`` and the memory to
     *  ``maxSize()`` in the constructor; only ``maxSize()`` allocates.
     */
    class API sampleHoldMemory {
    public:
      /** @brief How the read position moves on an advance in ``LEVEL``
       *         mode, numbered as Max's ``dir``. */
      enum DIRECTION {
        FORWARD = 0,
        REVERSE = 1,
        RANDOM = 2,
        URN = 3,
      };

      /** @brief How the advance trigger is read, numbered as Max's
       *         ``mode``. */
      enum MODE {
        LEVEL = 0,
        INDEX = 1,
        SUBDIVISION = 2,
      };

      /** @brief The capacity Max's ``maxsize`` defaults to. */
      static constexpr UInt DEFAULT_MAX_SIZE = 1000;

      /** @brief A memory of up to ``maxSize`` values (at least 1). */
      explicit sampleHoldMemory(UInt maxSize = DEFAULT_MAX_SIZE);

      /** @brief Sets the capacity (at least 1) and clears the memory; the
       *         size is kept when it fits, else becomes the capacity.
       *         Allocates: not while another thread processes. */
      sampleHoldMemory& maxSize(UInt capacity);

      /** @brief The capacity. */
      UInt maxSize() const {
        return static_cast<UInt>(memory.size());
      }

      /** @brief The number of places in use, 1 to ``maxSize()``
       *         (clamped). */
      sampleHoldMemory& size(UInt places);

      /** @brief The number of places in use. */
      UInt size() const {
        return length;
      }

      /** @brief The sample trigger's level. */
      sampleHoldMemory& sampleLevel(Flt level);

      /** @brief The advance trigger's level, in ``LEVEL`` mode. */
      sampleHoldMemory& advanceLevel(Flt level);

      /** @brief How the read position moves on an advance. */
      sampleHoldMemory& direction(DIRECTION d);

      /** @brief How the advance trigger is read. */
      sampleHoldMemory& mode(MODE m);

      /** @brief Seeds the random source of ``RANDOM`` and ``URN``. */
      sampleHoldMemory& seed(std::uint32_t value);

      /** @brief Fills the memory with the finite values of ``values`` (up to
       *         ``maxSize()``), sets the size to their number and the write
       *         position to the first place. Nothing finite: no change. */
      sampleHoldMemory& load(const Flt* values, UInt count);

      /** @brief The stored value at ``place`` (0 past the capacity). */
      Flt value(UInt place) const {
        return place < memory.size() ? memory[place] : 0.f;
      }

      /** @brief The place the next recording writes. */
      UInt writePosition() const {
        return writeAt;
      }

      /** @brief The place the output reads. */
      UInt readPosition() const {
        return readAt;
      }

      /** @brief One sample: records, moves the read position, returns the
       *         value read. */
      Flt process(Flt in, Flt sampleTrigger, Flt advanceTrigger);

      /** @brief One block (the shortest of the three sets the length). */
      buffer& operator()(buffer& in, buffer& sampleTrigger, buffer& advanceTrigger);

    private:
      void Advance();
      UInt Draw(UInt n);

      buffer output;
      std::vector<Flt> memory;
      // The places not yet read in this round of URN; the first `left`.
      std::vector<UInt> urn;
      UInt left = 0;

      UInt length = 1;
      UInt writeAt = 0;
      UInt readAt = 0;
      Flt sampleAt = 0.5f;
      Flt advanceAt = 0.5f;
      DIRECTION dir = FORWARD;
      MODE how = LEVEL;
      Flt lastSample = 0.f;
      Flt lastAdvance = 0.f;
      std::uint32_t state;
    };

  } // namespace DSP
} // namespace YSE

#endif // YSE_DSP_GENERATORS_SAMPLEHOLDMEMORY_HPP
