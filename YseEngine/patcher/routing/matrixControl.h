#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include "../../headers/types.hpp"
#include "../../dsp/routing/mixMatrix.hpp"

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief The control side of ``~matrix`` (issue #1134): its creation
     *         arguments, the connection messages of its left inlet, and the
     *         lock-free hand-over of the crosspoint gains to
     *         ``DSP::mixMatrix`` at the start of a block.
     *
     *  The message thread keeps the gain every crosspoint is heading for, with
     *  the ramp time of its last change, packed into one atomic word per
     *  crosspoint, and marks the crosspoint in an atomic dirty bitmap. At the
     *  start of a block the audio thread takes the dirty bits with one
     *  ``exchange`` per word and hands each marked crosspoint's word to the
     *  DSP class. Nothing waits and nothing is dropped: changes to one
     *  crosspoint between two blocks fold into the last of them.
     *
     *  The ramp time, the default gain and the exclusive mode are read here,
     *  on the message thread, when a message arrives, so every change carries
     *  the ramp it was sent with and ``exclusive`` applies in message order.
     *  The exclusive rule itself is ``DSP::mixMatrix::Exclusions``. One
     *  message thread at a time is assumed, as for every object's messages.
     */
    class matrixControl {
    public:
      /** @brief The matrix the creation arguments ask for. */
      struct shape {
        UInt inputs = DSP::mixMatrix::DEFAULT_CHANNELS;
        UInt outputs = DSP::mixMatrix::DEFAULT_CHANNELS;
        Flt defaultGain = 1.f;
      };

      /** @brief What ``dump`` and ``dumptarget`` ask for. */
      enum dumpKind : int { DUMP_NONE = 0, DUMP_CURRENT = 1, DUMP_TARGET = 2 };

      /** @brief Reads @p arguments (inputs, outputs, default gain, with
       *         ``ramp`` / ``ramptime <ms>`` and ``exclusive <0-3>`` among
       *         them), builds the crosspoint table for that size with
       *         nothing connected, and returns the shape. Control thread,
       *         before the object is published: it allocates. */
      shape Parse(const std::vector<std::string>& arguments);

      /** @brief A word message: ``connect``, ``disconnect``, ``clear``,
       *         ``ramp`` / ``ramptime``, ``exclusive``, ``dump``,
       *         ``dumptarget``, ``dumpconnections``, ``print`` or
       *         ``dictionary``. False for anything else. Allocation-free. */
      bool Message(const std::string& message);

      /** @brief A list: ``<in> <out> <gain> [<ramp ms>]``. False when it is
       *         not one. Allocation-free. */
      bool List(const std::string& list);

      /** @brief Audio thread, at the start of a block: hands every crosspoint
       *         changed since the last call to @p fn. */
      void Apply(DSP::mixMatrix& fn);

      /** @brief Audio thread: the dumps asked for since the last call, as
       *         ``dumpKind`` bits, and clears them. */
      int TakeDumps() {
        return dumps.exchange(DUMP_NONE, std::memory_order_acq_rel);
      }

      /** @brief The gain crosspoint @p in, @p out is heading for, as the
       *         messages left it. */
      Flt Target(UInt in, UInt out) const;

      /** @brief The ramp time for the next change, in ms. */
      Flt Ramp() const {
        return ramp.load(std::memory_order_relaxed);
      }

      /** @brief The exclusive mode for the next connection. */
      Int Exclusive() const {
        return exclusive.load(std::memory_order_relaxed);
      }

    private:
      // A connection change, with the exclusive rule.
      void Set(UInt in, UInt out, Flt gain, Flt rampMs);
      // One crosspoint's word, marked dirty.
      void Write(UInt in, UInt out, Flt gain, Flt rampMs);
      std::size_t Index(UInt in, UInt out) const {
        return (static_cast<std::size_t>(in) * outputs) + out;
      }

      UInt inputs = DSP::mixMatrix::DEFAULT_CHANNELS;
      UInt outputs = DSP::mixMatrix::DEFAULT_CHANNELS;
      std::atomic<Flt> ramp{DSP::mixMatrix::DEFAULT_RAMP};
      std::atomic<Flt> defaultGain{1.f};
      std::atomic<Int> exclusive{DSP::mixMatrix::EXCLUSIVE_OFF};
      std::atomic<int> dumps{DUMP_NONE};

      // One word per crosspoint, input-major: the gain's bits low, the ramp
      // time's high. Sized in Parse.
      std::unique_ptr<std::atomic<std::uint64_t>[]> cells;
      // One bit per crosspoint, set when its word changed.
      std::unique_ptr<std::atomic<std::uint64_t>[]> dirty;
      std::size_t dirtyWords = 0;
      // Set after any dirty bit, so a block with no change reads one flag.
      std::atomic<bool> anyDirty{false};
    };

  } // namespace PATCHER
} // namespace YSE
