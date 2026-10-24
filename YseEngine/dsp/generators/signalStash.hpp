/*
  ==============================================================================

    signalStash.hpp
    Stores signal values into indexed places and recalls them: a sample and
    hold over a memory, a lookup table or an audio-rate step sequencer
    (issue #1084).

  ==============================================================================
*/

#ifndef YSE_DSP_GENERATORS_SIGNALSTASH_HPP
#define YSE_DSP_GENERATORS_SIGNALSTASH_HPP

#include <cstdint>
#include <vector>
#include "../../headers/defines.hpp"
#include "../../headers/types.hpp"
#include "../../headers/constants.hpp"
#include "../buffer.hpp"

namespace YSE {
  namespace DSP {

    /**
     *  @brief Stores signal values into a memory of places and recalls them:
     *         a sample and hold that reads from a memory, an interpolating
     *         lookup table, or an audio-rate step sequencer.
     *
     *  Ported from Max's ``stash~``. Four signals go in each sample: the
     *  **input**, the **sample trigger**, the **advance trigger** and the
     *  **write index**.
     *
     *  **Triggers.** Both triggers compare each sample with the one before
     *  it and with a level (``sampleLevel()``, ``advanceLevel()``), as
     *  ``triggerMode`` says, numbered as Max's ``sampletriggermode`` and
     *  ``advancetriggermode`` and read as ``sampleHold`` reads its modes:
     *  ``ASCENDING`` (the default; from at or below the level to above it),
     *  ``DESCENDING``, ``BOTH``, ``EQUALS`` (every sample equal to the level),
     *  ``ZERO_TO_NONZERO`` and ``NONZERO_TO_ZERO`` (the level ignored), and
     *  ``PHASE`` (the trigger passing the level as a 0..1 phase in either
     *  direction, so level 0 fires at each reset of a ``phasor`` of either
     *  sign). A trigger sample that is not a finite number is skipped (the
     *  next one is compared with the one before it). Both previous trigger
     *  samples start at 0.
     *
     *  **Recording.** When the sample trigger fires (or once after
     *  ``capture()``), a finite input is written into the memory where
     *  ``writeMode()`` says:
     *
     *  - ``NEXT`` (the default): at the write position, which then moves one
     *    place on, wrapping at ``size()``. The write index is ignored.
     *  - ``INDEX``: at the place the write index names.
     *  - ``NORMALIZED``: at the place the write index names as a 0..1 share
     *    of the size (times ``size()``).
     *
     *  **Playback.** ``mode()`` says how the advance trigger picks the output:
     *
     *  - ``NEXT`` (the default): each time it fires, the read position moves
     *    as ``direction()`` says: ``FORWARD`` one place on (the default),
     *    ``REVERSE`` one place back, ``RANDOM`` to any place (with
     *    replacement), ``URN`` to a place not read since the last round (each
     *    place once per ``size()`` advances; a change of size or direction
     *    starts a new round). The output is the memory at the read position.
     *  - ``INDEX``: the advance trigger is the place to read.
     *  - ``NORMALIZED``: the advance trigger is a 0..1 share of the size: with
     *    size 2, below 0.5 reads place 0 and from 0.5 place 1.
     *
     *  **Indices.** An index (the write index, or the advance trigger in the
     *  two index modes) that is not a whole number is read as
     *  ``interpolation()`` says: ``TRUNCATE`` (the default) rounds it down,
     *  ``ROUND`` to the nearest place, ``LINEAR`` (playback only; a write
     *  rounds down) mixes the place below and the one above. One outside
     *  0 .. size - 1 is read as ``range()`` says: ``ZERO`` reads 0 (a write
     *  is dropped), ``EXTEND`` the first or last place, ``WRAP`` (the
     *  default) wraps it into the size (with size 128, 130 is place 2; linear
     *  mixing from the last place goes on to the first), ``KEEP`` changes
     *  nothing (the output keeps its value, a write is dropped). With
     *  ``ZERO`` or ``KEEP`` a linear index above size - 1 is out of range. An
     *  index that is not a finite number changes nothing.
     *
     *  **Duration.** With ``duration(n)`` above 0, a new output (an advance
     *  in ``NEXT`` mode, a change of the value read in the index modes) lasts
     *  ``n`` samples, the first being the one it came in, and the output is
     *  0 after that (and before the first one): 1 makes impulses. With 0 (the
     *  default) the output holds.
     *
     *  In a sample, recording comes first, then playback, so a value recorded
     *  where the output reads shows at once. **The memory** holds ``size()``
     *  values, 1 (the default) up to ``maxSize()`` (1000 unless the
     *  constructor or ``maxSize()`` says otherwise), all 0 at first.
     *  ``load()`` fills it from a list and sets the size, as Max's ``list``
     *  does; ``write()``, ``writePosition()`` and ``advanceWrite()`` are
     *  Max's numbers and bangs in the left and right inlets. Changing the
     *  size keeps the values and wraps the positions into it. Max leaves the
     *  defaults of the size, the levels, ``dir``, ``extend``, ``interp``,
     *  ``mode`` and ``writemode`` unstated; here they are 1, 0.5 and 0.5
     *  (as ``sampleHoldMemory``), forward, wrap, truncate, next and next.
     *
     *  Nothing depends on the sample rate. Each call processes at most
     *  ``STANDARD_BUFFERSIZE`` samples (the shortest of the four buffers).
     *  Call it from one thread at a time. Processing allocates nothing: the
     *  output is sized to ``STANDARD_BUFFERSIZE`` and the memory to
     *  ``maxSize()`` in the constructor; only ``maxSize()`` allocates.
     */
    class API signalStash {
    public:
      /** @brief When a trigger fires, numbered as Max's
       *         ``sampletriggermode`` / ``advancetriggermode``. */
      enum TRIGGER_MODE {
        ASCENDING = 0,
        DESCENDING = 1,
        BOTH = 2,
        EQUALS = 3,
        ZERO_TO_NONZERO = 4,
        NONZERO_TO_ZERO = 5,
        PHASE = 6,
      };

      /** @brief How the read position moves on an advance in ``NEXT`` mode,
       *         numbered as Max's ``dir``. */
      enum DIRECTION {
        FORWARD = 0,
        REVERSE = 1,
        RANDOM = 2,
        URN = 3,
      };

      /** @brief Where a recording goes (``writeMode()``) or what the output
       *         reads (``mode()``), numbered as Max's ``writemode`` and
       *         ``mode``. */
      enum MODE {
        NEXT = 0,
        INDEX = 1,
        NORMALIZED = 2,
      };

      /** @brief How an index outside the memory is read, numbered as Max's
       *         ``extend`` (whose 3, Ignore, is ``KEEP`` here). */
      enum RANGE {
        ZERO = 0,
        EXTEND = 1,
        WRAP = 2,
        KEEP = 3,
      };

      /** @brief How an index between places is read, numbered as Max's
       *         ``interp``. */
      enum INTERPOLATION {
        TRUNCATE = 0,
        LINEAR = 1,
        ROUND = 2,
      };

      /** @brief The capacity Max's ``maxsize`` defaults to. */
      static constexpr UInt DEFAULT_MAX_SIZE = 1000;

      /** @brief A memory of up to ``maxSize`` values (at least 1). */
      explicit signalStash(UInt maxSize = DEFAULT_MAX_SIZE);

      /** @brief Sets the capacity (at least 1) and clears the memory; the
       *         size is kept when it fits, else becomes the capacity.
       *         Allocates: not while another thread processes. */
      signalStash& maxSize(UInt capacity);

      /** @brief The capacity. */
      UInt maxSize() const {
        return static_cast<UInt>(memory.size());
      }

      /** @brief The number of places in use, 1 to ``maxSize()``
       *         (clamped). */
      signalStash& size(UInt places);

      /** @brief The number of places in use. */
      UInt size() const {
        return length;
      }

      /** @brief The sample trigger's level. */
      signalStash& sampleLevel(Flt level);

      /** @brief The advance trigger's level, in ``NEXT`` mode. */
      signalStash& advanceLevel(Flt level);

      /** @brief When the sample trigger fires. */
      signalStash& sampleTriggerMode(TRIGGER_MODE m);

      /** @brief When the advance trigger fires, in ``NEXT`` mode. */
      signalStash& advanceTriggerMode(TRIGGER_MODE m);

      /** @brief How the read position moves on an advance. */
      signalStash& direction(DIRECTION d);

      /** @brief What the output reads. */
      signalStash& mode(MODE m);

      /** @brief Where a recording goes. */
      signalStash& writeMode(MODE m);

      /** @brief How an index outside the memory is read. */
      signalStash& range(RANGE r);

      /** @brief How an index between places is read. */
      signalStash& interpolation(INTERPOLATION i);

      /** @brief Samples a new output lasts; 0 holds it. */
      signalStash& duration(UInt samples);

      /** @brief Seeds the random source of ``RANDOM`` and ``URN``. */
      signalStash& seed(std::uint32_t value);

      /** @brief Fills the memory with the finite values of ``values`` (up to
       *         ``maxSize()``), sets the size to their number and the write
       *         position to the first place. Nothing finite: no change. */
      signalStash& load(const Flt* values, UInt count);

      /** @brief Writes a finite ``value`` at the write position, which stays
       *         (Max's number in the left inlet). */
      signalStash& write(Flt value);

      /** @brief Sets the write position to ``index``, read as the write
       *         index is in ``writeMode()`` (``NEXT`` as ``INDEX``), rounded
       *         down (to the nearest with ``ROUND``); out of range as
       *         ``range()`` says, ``ZERO`` and ``KEEP`` leaving it (Max's
       *         number in the right inlet). */
      signalStash& writePosition(Flt index);

      /** @brief Moves the write position one place on, wrapping (Max's bang
       *         in the right inlet). */
      signalStash& advanceWrite();

      /** @brief Moves the read position as ``direction()`` says and starts
       *         a ``duration()`` (Max's bang in the advance inlet; the index
       *         modes read their trigger again at the next sample). */
      signalStash& advance();

      /** @brief Records the next sample's input whatever the sample trigger
       *         does (Max's bang in the sample inlet). */
      signalStash& capture();

      /** @brief The stored value at ``place`` (0 past the capacity). */
      Flt value(UInt place) const {
        return place < memory.size() ? memory[place] : 0.f;
      }

      /** @brief The place ``NEXT`` recording and ``write()`` write. */
      UInt writePosition() const {
        return writeAt;
      }

      /** @brief The place the output last read (the lower one when
       *         mixing). */
      UInt readPosition() const {
        return readAt;
      }

      /** @brief One sample: records, plays back, returns the output. */
      Flt process(Flt in, Flt sampleTrigger, Flt advanceTrigger, Flt writeIndex);

      /** @brief One block (the shortest of the four sets the length). */
      buffer& operator()(buffer& in, buffer& sampleTrigger, buffer& advanceTrigger,
                         buffer& writeIndex);

    private:
      void Advance();
      UInt Draw(UInt n);
      // `x` as a whole place: false when out of range under ZERO or KEEP.
      bool Place(Dbl x, UInt& place) const;
      // The value at index `x`; false when out of range under ZERO or KEEP.
      bool Lookup(Dbl x, Flt& result);
      void Record(Flt in, Flt writeIndex);

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
      Dbl sampleWrapped = 0.5;
      Dbl advanceWrapped = 0.5;
      TRIGGER_MODE sampleHow = ASCENDING;
      TRIGGER_MODE advanceHow = ASCENDING;
      DIRECTION dir = FORWARD;
      MODE how = NEXT;
      MODE writeHow = NEXT;
      RANGE outside = WRAP;
      INTERPOLATION between = TRUNCATE;
      UInt hold = 0;
      UInt remaining = 0;
      bool armed = false;
      Flt lastSample = 0.f;
      Flt lastAdvance = 0.f;
      // The value the index modes read last (kept under KEEP), and the one
      // a duration compares with.
      Flt held = 0.f;
      Flt lastRead = 0.f;
      std::uint32_t state;
    };

  } // namespace DSP
} // namespace YSE

#endif // YSE_DSP_GENERATORS_SIGNALSTASH_HPP
