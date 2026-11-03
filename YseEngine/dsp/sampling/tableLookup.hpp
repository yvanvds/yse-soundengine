/*
  ==============================================================================

    tableLookup.hpp
    A sample buffer used as a lookup table at signal rate (issue #1148).

  ==============================================================================
*/

#ifndef YSE_DSP_SAMPLING_TABLELOOKUP_HPP
#define YSE_DSP_SAMPLING_TABLELOOKUP_HPP

#include <cstdint>
#include "../../headers/constants.hpp"
#include "../../headers/defines.hpp"
#include "../../headers/types.hpp"
#include "../buffer.hpp"

namespace YSE {
  namespace DSP {

    class sampleView;

    /**
     *  @brief Reads a ``sampleBuffer`` as a table of values at signal rate:
     *         what the patcher's ``~table`` does.
     *
     *  One channel of the buffer is the table, and its frames are the
     *  entries, numbered from 0. The class adds the table behaviour of
     *  Max's ``table~`` on top of a plain index read (``sampleIndexReader``):
     *
     *  - **Interpolation** (``interp``). ``NONE`` reads the entry at or below
     *    the index, ``ROUND`` the nearest one, ``LINEAR`` a straight line
     *    between the two around it: index 1.5 between 5 and 10 reads 7.5.
     *  - **Out of range** (``extend``). An index outside the table reads 0
     *    (``ZERO``), the first or the last entry (``EXTEND``), the entry it
     *    reaches when the table repeats (``WRAP``: 130 in a table of 128
     *    reads entry 2), or leaves the output as it was (``HOLD``).
     *    Linear interpolation follows the same rule for the entry past the
     *    end, so with ``WRAP`` the last entry slides into the first.
     *  - **Input map** (``inputMap``). The input range ``lo`` to ``hi`` is
     *    scaled to the table's length, measured when it is read, so ``0 1``
     *    covers the table whatever its size: 0 reads entry 0 and 1 the index
     *    one past the last (with ``WRAP``, entry 0 again).
     *  - **Output scale** (``outputScale``). The table's value range is
     *    scaled to the range ``lo`` to ``hi``. The value range is set by
     *    ``range`` and ``signedRange``: 0 to ``range - 1``, or ``-range`` to
     *    ``range``, as in Max (default 128, unsigned).
     *  - **Input mode** (``mode``). ``LOOKUP`` reads at the index the input
     *    gives. ``INCREMENT`` steps to the next entry on every trigger in
     *    the input, as ``next`` does. ``RANDOM`` jumps to an entry drawn at
     *    random on every trigger, as ``draw`` does. A trigger is a change
     *    from 0 to anything else (``RISING``), or any change (``CHANGE``),
     *    chosen by ``trigger``. Between triggers the output keeps reading
     *    the same entry, so an edit to it is heard.
     *
     *  The **position** is the index read while there is no input signal,
     *  in ``INCREMENT`` and in ``RANDOM``. ``set`` gives it (through the
     *  input map) or tests it as a trigger, ``goTo`` / ``next`` / ``prev``
     *  step it, and ``draw`` makes it a random entry. ``draw`` treats the
     *  table as a probability distribution: an entry is chosen with a
     *  chance in proportion to its value, and an entry at 0 or below is
     *  never chosen. With no entry above 0 the draw is entry 0. A draw
     *  walks the table, so it costs one pass over the buffer, and the first
     *  one in a block a second.
     *
     *  .. code-block:: cpp
     *
     *     // control thread
     *     reader.bind(DSP::sampleBufferRegistry::acquire("curve"));
     *     table.interp(DSP::tableLookup::interpolation::LINEAR);
     *     table.inputMap(0.f, 1.f);
     *
     *     // audio thread, once per block
     *     DSP::sampleView view = reader.pin();
     *     DSP::buffer& out = table(view, phase);
     *
     *  A channel the buffer lacks, or an empty or unbound buffer, reads
     *  silence. Every read is a relaxed atomic load, so a writer on another
     *  render thread may write the table meanwhile. The table is counted in
     *  frames, whatever its rate or the engine's, so nothing is derived from
     *  the sample rate.
     *
     *  The output is sized to ``STANDARD_BUFFERSIZE`` when the object is
     *  made; a shorter block resizes it down without allocating, and a longer
     *  one is cut to ``STANDARD_BUFFERSIZE``. Processing allocates nothing
     *  and takes no lock. Not thread-safe: one thread at a time, and the
     *  setters on that thread too.
     */
    class API tableLookup {
    public:
      /** @brief How an index between two entries is read: Max's
       *         ``interp`` 0, 1 and 2. */
      enum class interpolation { NONE = 0, LINEAR = 1, ROUND = 2 };

      /** @brief What an index outside the table reads: Max's ``extend``
       *         0 to 3. */
      enum class outOfRange { ZERO = 0, EXTEND = 1, WRAP = 2, HOLD = 3 };

      /** @brief What the input is: Max's ``inputmode`` 0 to 2. */
      enum class inputMode { LOOKUP = 0, INCREMENT = 1, RANDOM = 2 };

      /** @brief What counts as a trigger in ``INCREMENT`` and ``RANDOM``:
       *         Max's ``triggermode`` 0 and 1. */
      enum class triggerMode { RISING = 0, CHANGE = 1 };

      /** @brief Max's default ``range``. */
      static constexpr Int DEFAULT_RANGE = 128;

      tableLookup();

      /** @brief The channel read, from 0. */
      tableLookup& channel(UInt c) {
        targetChannel = c;
        return *this;
      }
      UInt channel() const {
        return targetChannel;
      }

      tableLookup& interp(interpolation value) {
        interpolate = value;
        return *this;
      }
      interpolation interp() const {
        return interpolate;
      }

      tableLookup& extend(outOfRange value) {
        edge = value;
        return *this;
      }
      outOfRange extend() const {
        return edge;
      }

      tableLookup& mode(inputMode value) {
        input = value;
        return *this;
      }
      inputMode mode() const {
        return input;
      }

      tableLookup& trigger(triggerMode value) {
        triggerOn = value;
        return *this;
      }
      triggerMode trigger() const {
        return triggerOn;
      }

      /** @brief Scale the input range @p lo to @p hi to the table's length.
       *         Equal ends (Max's ``0 0``), or one not finite, clear it. */
      tableLookup& inputMap(Flt lo, Flt hi);
      /** @brief No input map: the input is the index. */
      tableLookup& clearInputMap() {
        mapIn = false;
        return *this;
      }
      bool hasInputMap() const {
        return mapIn;
      }

      /** @brief Scale the table's value range to @p lo to @p hi. Equal ends
       *         (Max's ``0 0``), or one not finite, clear it. */
      tableLookup& outputScale(Flt lo, Flt hi);
      /** @brief No output scale: the entries as they are. */
      tableLookup& clearOutputScale() {
        scaleOut = false;
        return *this;
      }
      bool hasOutputScale() const {
        return scaleOut;
      }

      /** @brief The table's value range for ``outputScale``. Below 1 is 1. */
      tableLookup& range(Int value) {
        valueRange = value < 1 ? 1 : value;
        return *this;
      }
      Int range() const {
        return valueRange;
      }

      /** @brief Whether the value range runs from ``-range`` to ``range``
       *         instead of 0 to ``range - 1``. Changes no entry. */
      tableLookup& signedRange(bool on) {
        isSigned = on;
        return *this;
      }
      bool signedRange() const {
        return isSigned;
      }

      /** @brief Restart the random draws from @p value. */
      tableLookup& seed(std::uint32_t value) {
        state = value;
        return *this;
      }

      /**
       *  @brief A number for the input while no signal is connected (Max's
       *         ``float``). In ``LOOKUP`` it is the position, through the
       *         input map; in ``INCREMENT`` and ``RANDOM`` it is tested as a
       *         trigger against the input before it.
       */
      void set(const sampleView& view, Flt value);

      /** @brief Where the next ``next`` or ``prev`` goes: that one reads
       *         @p index itself rather than stepping. The output does not
       *         change, and the input map does not apply. */
      void goTo(Flt index);

      /** @brief Step the position one entry up, past the end to the start;
       *         to the ``goTo`` index instead when one is waiting. The
       *         fraction of the position is kept. */
      void next(const sampleView& view);

      /** @brief Step the position one entry down, below 0 to the end; to the
       *         ``goTo`` index instead when one is waiting. */
      void prev(const sampleView& view);

      /** @brief Make the position an entry drawn at random, each entry with
       *         a chance in proportion to its value. */
      void draw(const sampleView& view);

      /** @brief The position, as a table index. Diagnostics and tests. */
      Dbl position(const sampleView& view) const;

      /** @brief One block driven by a signal: an index per sample in
       *         ``LOOKUP``, triggers in the other modes. As long as @p in,
       *         up to ``STANDARD_BUFFERSIZE``. */
      buffer& operator()(const sampleView& view, buffer& in);

      /** @brief One block of @p length samples with no input signal: the
       *         position read for each. */
      buffer& operator()(const sampleView& view, UInt length = STANDARD_BUFFERSIZE);

      /** @brief One sample: the table at @p index (no input map) through the
       *         interpolation, the out-of-range rule and the output scale.
       *         ``HOLD`` keeps the value this returned last. */
      Flt read(const sampleView& view, Dbl index);

      /** @brief The output of the last call. */
      buffer& out() {
        return output;
      }

    private:
      // The table of the view for this block, or null and 0.
      struct table {
        const Flt* samples = nullptr;
        UInt frames = 0;
      };
      table Table(const sampleView& view) const;

      Dbl MapIn(Dbl value, UInt frames) const;
      Flt ScaleOut(Flt value) const;
      Flt Read(const table& t, Dbl index);
      bool Triggered(Flt value);
      void Step(const table& t, Dbl by);
      void Draw(const table& t, Dbl& total, bool& haveTotal);

      UInt targetChannel = 0;
      interpolation interpolate = interpolation::NONE;
      outOfRange edge = outOfRange::ZERO;
      inputMode input = inputMode::LOOKUP;
      triggerMode triggerOn = triggerMode::RISING;
      bool mapIn = false;
      Dbl inLo = 0.0;
      Dbl inHi = 1.0;
      bool scaleOut = false;
      Flt outLo = 0.f;
      Flt outHi = 1.f;
      Int valueRange = DEFAULT_RANGE;
      bool isSigned = false;

      // The position: a table index, or the last `set` number in LOOKUP
      // that the input map turns into one when read (the map follows the
      // table's length).
      Dbl cursor = 0.0;
      bool cursorIsInput = false;
      Dbl gotoIndex = 0.0;
      bool gotoWaiting = false;

      Flt lastInput = 0.f; // for the triggers
      Flt held = 0.f; // the last value read, before the output scale: HOLD
      std::uint32_t state;
      buffer output;
    };

  } // namespace DSP
} // namespace YSE

#endif // YSE_DSP_SAMPLING_TABLELOOKUP_HPP
