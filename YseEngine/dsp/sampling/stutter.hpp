/*
  ==============================================================================

    stutter.hpp
    A capture buffer whose last fragment is replayed by phase (issue #1150).

  ==============================================================================
*/

#ifndef YSE_DSP_SAMPLING_STUTTER_HPP
#define YSE_DSP_SAMPLING_STUTTER_HPP

#include <cstdint>
#include <vector>
#include "../../headers/constants.hpp"
#include "../../headers/defines.hpp"
#include "../../headers/types.hpp"
#include "../buffer.hpp"

namespace YSE {
  namespace DSP {

    class sampleWriteView;

    /**
     *  @brief Records its input all the time and, on a trigger, copies the
     *         most recent samples into a playback fragment that one or more
     *         phases read in a cycle: stutter and glitch effects. What the
     *         patcher's ``~stutter`` does (Max's ``stutter~``).
     *
     *  **Recording.** Every input sample goes into a circular record
     *  buffer of ``capacity()`` samples, fixed when the object is made.
     *
     *  **Triggers.** A trigger copies the last ``size()`` samples recorded
     *  into the playback fragment, which is then ``length()`` samples long.
     *  A trigger is an edge of the trigger signal: with a positive
     *  ``polarity()`` (the default) the signal going from 0 or below to
     *  above 0, with a negative one from 0 or above to below 0. The
     *  remembered trigger sample starts at 0, so a trigger signal that is
     *  above 0 from the start triggers once at its first sample. An edge
     *  copies the fragment that ends with the input sample of the same
     *  index, and the outputs read the new fragment from that sample on.
     *  ``trigger()`` (Max's ``bang``) copies at the first sample of the
     *  next block. Before the first trigger, and after ``clear()``, the
     *  fragment is empty and the outputs are silent.
     *
     *  **Copy rate.** With a ``copyRate()`` of 0 (the default) the whole
     *  fragment is copied at the trigger. A rate of ``n`` copies ``n``
     *  samples at the trigger and ``n`` more at the start of every block
     *  after it, as Max spreads the copy over its perform loops to spread
     *  the load: until it is done the end of the fragment still holds what
     *  was there before. The copy reads the samples that were the most
     *  recent at the trigger, so the record buffer needs room for the
     *  samples recorded while it runs as well: ``capacity()`` at least
     *  ``size()`` plus ``size() / copyRate()`` blocks.
     *
     *  **Playback.** Output ``k`` reads the fragment at its own phase,
     *  0 to 1 across the fragment: position ``phase * length()``, between
     *  two samples on the straight line between them, the last sample
     *  leading back to the first, so a ramp from 0 to 1 plays the fragment
     *  once and a phasor loops it. A phase outside 0 to 1 wraps (only its
     *  fraction counts); a phase that is not a number reads 0. A phasor at
     *  ``sampleRate / length()`` Hz plays the fragment at its own speed.
     *
     *  **Chance.** Each trigger draws three random numbers, in this order,
     *  from a source ``seed()`` restarts: with ``repeatChance()`` percent
     *  the fragment is not copied and the one before plays on; with
     *  ``dropoutChance()`` percent the outputs are silent until the next
     *  trigger; and the gain until the next trigger is
     *  ``1 - ampVariation() * u`` for a ``u`` uniform in 0 to 1. All three
     *  are 0 by default: every trigger copies, plays, at gain 1.
     *
     *  Sizes, positions and rates count samples, whatever the engine's
     *  rate, so there is no state derived from the sample rate.
     *
     *  .. code-block:: cpp
     *
     *     // control thread
     *     DSP::stutter st(88200, 1);
     *     st.size(4410);
     *
     *     // audio thread, once per block
     *     DSP::stutter::signal phase(phasorOut);
     *     st(in, trigger, &phase);
     *     DSP::buffer& out = st.out(0);
     *
     *  The record buffer, the fragment and the outputs are allocated when
     *  the object is made. Processing allocates nothing and takes no lock:
     *  a block costs one record, one edge test and one read per output per
     *  sample, plus the copy. Not thread-safe: one thread at a time.
     */
    class API stutter {
    public:
      /** @brief The most outputs, and phases, as in Max. */
      static constexpr UInt MAX_OUTPUTS = 30;
      /** @brief The largest record buffer, in samples. */
      static constexpr UInt MAX_CAPACITY = 1u << 22;
      /** @brief The record buffer a default object gets, in samples. */
      static constexpr UInt DEFAULT_CAPACITY = 88200;
      /** @brief The fragment a default object copies, in samples. */
      static constexpr UInt DEFAULT_SIZE = 4410;

      /**
       *  @brief One input of a block: a signal's samples, or a constant.
       *
       *  Made from a ``buffer`` (which must hold the block's samples while
       *  the call runs) or from a number.
       */
      struct signal {
        const Flt* samples = nullptr;
        Flt value = 0.f;

        signal() = default;
        signal(Flt constant) : value(constant) {} // NOLINT(google-explicit-constructor)
        signal(buffer& block) : samples(block.getPtr()) {} // NOLINT(google-explicit-constructor)

        Flt at(UInt i) const {
          return samples != nullptr ? samples[i] : value;
        }
      };

      /**
       *  @brief Allocates the record buffer, the fragment and the outputs.
       *         Control thread.
       *  @param capacity The record buffer in samples, from 1 to
       *                  ``MAX_CAPACITY`` (held there). Also the largest
       *                  ``maxSize()``.
       *  @param outputs  The outputs and phases, from 1 to ``MAX_OUTPUTS``.
       */
      explicit stutter(UInt capacity = DEFAULT_CAPACITY, UInt outputs = 1);

      /** @brief The record buffer's length, in samples. */
      UInt capacity() const {
        return static_cast<UInt>(record.size());
      }

      /** @brief The number of outputs. */
      UInt outputs() const {
        return static_cast<UInt>(outs.size());
      }

      /** @brief The largest ``size()``, at most ``capacity()`` (held
       *         there). A ``size()`` above it comes down to it. Default
       *         ``capacity()``. Max's ``maxsize``, within the memory
       *         allocated when the object was made. */
      stutter& maxSize(UInt samples);
      UInt maxSize() const {
        return largest;
      }

      /** @brief The samples a trigger copies, at most ``maxSize()`` (held
       *         there). Default ``DEFAULT_SIZE``, or ``maxSize()`` when that
       *         is less. 0 copies an empty fragment: silence. */
      stutter& size(UInt samples);
      UInt size() const {
        return copySize;
      }

      /** @brief True for positive triggers (the default), false for
       *         negative ones. */
      stutter& polarity(bool positive) {
        rising = positive;
        return *this;
      }
      bool polarity() const {
        return rising;
      }

      /** @brief Samples copied per block after a trigger; 0 (the default)
       *         copies the whole fragment at once. */
      stutter& copyRate(UInt samples) {
        rate = samples;
        return *this;
      }
      UInt copyRate() const {
        return rate;
      }

      /** @brief The random gain variation, 0 to 1 (held there): 0 keeps
       *         the gain at 1, 1 draws it anywhere from 0 to 1. */
      stutter& ampVariation(Flt amount);
      Flt ampVariation() const {
        return ampVar;
      }

      /** @brief The chance in percent, 0 to 100 (held there), that a
       *         trigger silences the outputs until the next one. */
      stutter& dropoutChance(Flt percent);
      Flt dropoutChance() const {
        return dropout;
      }

      /** @brief The chance in percent, 0 to 100 (held there), that a
       *         trigger keeps the fragment it has instead of copying. */
      stutter& repeatChance(Flt percent);
      Flt repeatChance() const {
        return repeat;
      }

      /** @brief Restart the random source. */
      stutter& seed(std::uint32_t value) {
        random = value;
        return *this;
      }

      /** @brief Copy at the first sample of the next block. */
      stutter& trigger() {
        triggerPending = true;
        return *this;
      }

      /** @brief Set the record buffer and the fragment to 0, empty the
       *         fragment and stop a copy that runs. */
      stutter& clear();

      /** @brief The fragment's length, in samples: the size at the last
       *         trigger that copied, 0 before one. */
      UInt length() const {
        return fragmentLength;
      }

      /** @brief Whether the last trigger dropped the outputs out. */
      bool droppedOut() const {
        return muted;
      }

      /** @brief The gain the last trigger drew. */
      Flt gain() const {
        return level;
      }

      /** @brief Whether a copy is still running (a ``copyRate()`` above
       *         0). */
      bool copying() const {
        return copied < fragmentLength;
      }

      /** @brief Fragment sample @p i, 0 past ``length()``. */
      Flt fragment(UInt i) const {
        return i < fragmentLength ? play[i] : 0.f;
      }

      /**
       *  @brief One block.
       *  @param length  The samples in the block, up to
       *                 ``STANDARD_BUFFERSIZE`` (cut there).
       *  @param in      The signal recorded.
       *  @param trigger The trigger signal.
       *  @param phases  ``outputs()`` phases, one per output.
       */
      void operator()(UInt length, signal in, signal trigger, const signal* phases);

      /** @brief One block of the length of @p in, with one phase per
       *         output, all @p phase. */
      void operator()(buffer& in, signal trigger, signal phase);

      /** @brief Output @p k of the last block. */
      buffer& out(UInt k) {
        return outs[k];
      }

      /**
       *  @brief Write the fragment into channel @p channel of @p view from
       *         frame @p frame: Max's ``setbuf``.
       *  @return How many samples were written: fewer at the end of the
       *          buffer, 0 for a channel it does not have.
       */
      UInt copyTo(sampleWriteView& view, UInt channel, UInt frame) const;

    private:
      // A trigger at the sample just recorded.
      void fire();
      // Copy up to `count` more samples of the running copy.
      void copyChunk(UInt count);
      // A uniform number in [0, 1).
      Flt draw();
      // The fragment at a phase.
      Flt read(Flt phase) const;

      std::vector<Flt> record;
      std::vector<Flt> play;
      std::vector<buffer> outs;

      UInt write = 0; // the next record position
      UInt largest = 0;
      UInt copySize = 0;
      UInt rate = 0;
      bool rising = true;
      Flt ampVar = 0.f;
      Flt dropout = 0.f;
      Flt repeat = 0.f;
      std::uint32_t random = 0;

      bool triggerPending = false;
      Flt lastTrigger = 0.f;
      UInt fragmentLength = 0;
      UInt copyFrom = 0; // the record position of fragment sample 0
      UInt copied = 0; // fragment samples copied so far
      bool muted = false;
      Flt level = 1.f;
    };

  } // namespace DSP
} // namespace YSE

#endif // YSE_DSP_SAMPLING_STUTTER_HPP
