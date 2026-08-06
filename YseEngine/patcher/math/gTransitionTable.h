#pragma once
#include <array>
#include <atomic>
#include <cstddef>
#include "../../headers/types.hpp"

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief Fixed-capacity table of weighted ``from -> to`` transitions, and
     *         the weighted walk over it. Shared by ``.prob`` (#456) and the
     *         ``.anal`` histogram (#457) that feeds it.
     *
     *  A first-order Markov chain is just a bag of ``(from, to, weight)``
     *  triples plus a rule for choosing among the triples that leave a given
     *  state. Both halves live here because both objects need both: ``.prob``
     *  accumulates the triples from list messages and walks them, ``.anal``
     *  accumulates the same triples by *counting* the pairs it sees and emits
     *  them for a ``.prob`` to walk. Keeping one representation means the two
     *  objects agree on capacity, on how weights combine, and on what a
     *  zero-weight entry means, rather than each inventing an answer.
     *
     *  ### Storage
     *
     *  Three parallel fixed arrays and a published count — no map, no vector,
     *  no node allocation. A pair is looked up by linear scan, which is O(n) in
     *  the number of *stored* entries rather than in the capacity, and n is
     *  bounded by ``CAPACITY``. That is deliberately the dumb structure: at a
     *  thousand entries a scan is a few microseconds of straight-line integer
     *  work with no branches worth predicting, and every alternative that is
     *  asymptotically better (hash bucket, sorted index) either allocates or
     *  needs a rebuild step that a reader could observe half-done.
     *
     *  ### Threading
     *
     *  Writes (``Set``, ``Add``, ``Clear``) come from the control thread, and
     *  the object that owns the table is expected to have a single writer.
     *  Reads (``Pick``, ``WeightOf``, ``TotalWeightFrom``, ``Entry``) are
     *  wait-free and may run on the audio thread.
     *
     *  An append fills the three slot words *before* publishing the new count
     *  with a release store, and every reader takes the count with an acquire
     *  load and scans only ``[0, count)``. So a reader never sees a slot that
     *  is still being filled. Updating an existing entry's weight is a single
     *  relaxed store, and ``Clear`` is a single release store of 0 — a reader
     *  already inside a scan may finish it against entries that were just
     *  retired, which is harmless: every word it reads is a complete int, and
     *  the worst outcome is one transition drawn from the table as it stood a
     *  moment ago.
     *
     *  ### Weights
     *
     *  A weight is a *relative* likelihood: for a given ``from`` state the
     *  weights of all its transitions are summed and each one's share of that
     *  sum is its probability, so ``3 4 1`` means 37.5% / 50% / 12.5%, exactly
     *  as Max documents for ``prob``.
     *
     *  - **Zero weight.** The entry stays in the table and is never chosen.
     *    That is what makes ``from to 0`` the way to switch a transition off
     *    without dropping the slot, and it keeps ``Add`` (``.anal``'s
     *    accumulate) from having to resurrect entries.
     *  - **Negative weight.** Clamped to 0. A negative likelihood has no
     *    meaning, and letting one into the sum would make the total smaller
     *    than a prefix of it — which is how a weighted-choice loop falls off
     *    the end of its table.
     *  - **Ceiling.** Clamped to ``MAX_WEIGHT``. With ``CAPACITY`` entries that
     *    bounds any total at 2^30, which is what lets the multiply-shift
     *    reduction in ``Pick`` stay inside 64 bits.
     *  - **A state whose outgoing weights are all zero (or that has no entries
     *    at all) is a dead end**: ``Pick`` returns false and the caller decides
     *    what that means. There is no fallback search and no retry.
     */
    class TransitionTable {
    public:
      /** @brief Number of ``(from, to)`` pairs the table can hold. */
      static constexpr int CAPACITY = 1024;
      /** @brief Largest honoured weight; see the class docs for why it exists. */
      static constexpr int MAX_WEIGHT = 1 << 20;

      /** @brief How many pairs are currently stored. */
      int Size() const {
        return count.load(std::memory_order_acquire);
      }

      /** @brief Forgets every transition. Real-time safe. */
      void Clear() {
        count.store(0, std::memory_order_release);
      }

      /**
       *  @brief Sets the weight of @p from -> @p to, replacing any weight the
       *         pair already had.
       *
       *  @return false when the pair is new and the table is full — the entry
       *          is dropped rather than overwriting someone else's.
       */
      bool Set(int from, int to, int weight) {
        const int clamped = ClampWeight(static_cast<I64>(weight));
        const int found = Find(from, to);
        if (found >= 0) {
          weights[static_cast<std::size_t>(found)].store(clamped, std::memory_order_relaxed);
          return true;
        }
        return Append(from, to, clamped);
      }

      /**
       *  @brief Adds @p delta to the weight of @p from -> @p to, creating the
       *         pair at that weight when it is not there yet.
       *
       *  The accumulate a histogram needs: ``.anal`` calls this once per pair
       *  it observes. The sum is clamped the same way ``Set`` clamps.
       *
       *  @return false when the pair is new and the table is full.
       */
      bool Add(int from, int to, int delta) {
        const int found = Find(from, to);
        if (found >= 0) {
          const auto slot = static_cast<std::size_t>(found);
          const I64 sum = static_cast<I64>(weights[slot].load(std::memory_order_relaxed)) +
                          static_cast<I64>(delta);
          weights[slot].store(ClampWeight(sum), std::memory_order_relaxed);
          return true;
        }
        return Append(from, to, ClampWeight(static_cast<I64>(delta)));
      }

      /** @brief Weight of @p from -> @p to, or 0 when the pair is not stored. */
      int WeightOf(int from, int to) const {
        const int found = Find(from, to);
        if (found < 0) return 0;
        return weights[static_cast<std::size_t>(found)].load(std::memory_order_relaxed);
      }

      /** @brief Summed weight of every transition leaving @p from. */
      I64 TotalWeightFrom(int from) const {
        const int n = count.load(std::memory_order_acquire);
        I64 total = 0;
        for (int i = 0; i < n; ++i) {
          if (fromState[static_cast<std::size_t>(i)].load(std::memory_order_relaxed) == from)
            total += weights[static_cast<std::size_t>(i)].load(std::memory_order_relaxed);
        }
        return total;
      }

      /**
       *  @brief Chooses a transition out of @p from, weighted, and writes the
       *         destination to @p out.
       *
       *  @param draw a uniform 32-bit random word — one draw, supplied by the
       *              caller so the caller keeps control of how often its
       *              generator advances.
       *  @return false when @p from is a dead end (no entries, or every
       *          outgoing weight zero). @p out is untouched then.
       *
       *  Two bounded passes over the stored entries: one to total the weights,
       *  one prefix-sum scan to land on the chosen one. No rejection loop —
       *  "draw again until it lands somewhere" is the usual way to write this
       *  and has no upper bound on its running time, which disqualifies it from
       *  an audio callback. Nothing here allocates, locks, or blocks.
       *
       *  The draw is mapped onto ``[0, total)`` with Lemire's multiply-shift
       *  reduction, the same trick ``RandomSource::Bounded`` uses and for the
       *  same reason: no division, no rejection. Its residual bias is under
       *  ``total / 2^32``, i.e. below one part in four million even at the
       *  largest total this table can hold.
       */
      bool Pick(int from, UInt draw, int& out) const {
        const int n = count.load(std::memory_order_acquire);

        I64 total = 0;
        for (int i = 0; i < n; ++i) {
          if (fromState[static_cast<std::size_t>(i)].load(std::memory_order_relaxed) == from)
            total += weights[static_cast<std::size_t>(i)].load(std::memory_order_relaxed);
        }
        if (total <= 0) return false;

        // total <= CAPACITY * MAX_WEIGHT = 2^30 and draw < 2^32, so the product
        // fits in 62 bits.
        const I64 target =
            static_cast<I64>((static_cast<U64>(draw) * static_cast<U64>(total)) >> 32);

        I64 running = 0;
        int last = -1;
        for (int i = 0; i < n; ++i) {
          const auto slot = static_cast<std::size_t>(i);
          if (fromState[slot].load(std::memory_order_relaxed) != from) continue;
          const int weight = weights[slot].load(std::memory_order_relaxed);
          if (weight <= 0) continue; // a zero-weight transition is never chosen
          last = static_cast<int>(slot);
          running += weight;
          if (target < running) {
            out = toState[slot].load(std::memory_order_relaxed);
            return true;
          }
        }

        // Only reachable when a writer shrank a weight between the two passes,
        // which leaves the target past the end of the prefix sum. Fall back to
        // the last candidate rather than reporting a dead end that the table
        // does not actually have.
        if (last < 0) return false;
        out = toState[static_cast<std::size_t>(last)].load(std::memory_order_relaxed);
        return true;
      }

      /**
       *  @brief Reads entry @p index out. Returns false when @p index is not a
       *         stored entry.
       *
       *  Entries are in insertion order, which is the order ``.prob``'s
       *  ``dump`` reports them and the order a ``.anal`` would replay them in.
       */
      bool Entry(int index, int& from, int& to, int& weight) const {
        if (index < 0 || index >= count.load(std::memory_order_acquire)) return false;
        const auto slot = static_cast<std::size_t>(index);
        from = fromState[slot].load(std::memory_order_relaxed);
        to = toState[slot].load(std::memory_order_relaxed);
        weight = weights[slot].load(std::memory_order_relaxed);
        return true;
      }

    private:
      static int ClampWeight(I64 weight) {
        if (weight < 0) return 0;
        if (weight > static_cast<I64>(MAX_WEIGHT)) return MAX_WEIGHT;
        return static_cast<int>(weight);
      }

      // Index of the (from, to) pair, or -1. Linear over the stored entries.
      int Find(int from, int to) const {
        const int n = count.load(std::memory_order_acquire);
        for (int i = 0; i < n; ++i) {
          const auto slot = static_cast<std::size_t>(i);
          if (fromState[slot].load(std::memory_order_relaxed) == from &&
              toState[slot].load(std::memory_order_relaxed) == to)
            return i;
        }
        return -1;
      }

      // Fills the next free slot and publishes it. The release store is what
      // orders the three slot writes ahead of the count a reader will acquire.
      bool Append(int from, int to, int weight) {
        const int n = count.load(std::memory_order_relaxed);
        if (n >= CAPACITY) return false;
        const auto slot = static_cast<std::size_t>(n);
        fromState[slot].store(from, std::memory_order_relaxed);
        toState[slot].store(to, std::memory_order_relaxed);
        weights[slot].store(weight, std::memory_order_relaxed);
        count.store(n + 1, std::memory_order_release);
        return true;
      }

      std::array<aInt, CAPACITY> fromState{};
      std::array<aInt, CAPACITY> toState{};
      std::array<aInt, CAPACITY> weights{};
      // Published entry count. Acquire/release against the slot writes above.
      std::atomic<int> count{0};
    };

  } // namespace PATCHER
} // namespace YSE
