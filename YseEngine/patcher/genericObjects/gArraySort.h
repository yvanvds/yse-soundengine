#pragma once
#include "../pAtomList.h"
#include "../pObject.h"
#include "gArray.h"
#include "gArrayPermute.h"
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief Order an array's elements — Max's ``array.sort`` on the
     *         name-addressed value model ``.array`` settled (issue #789).
     *
     *  The fifth permutation, and the one with a decision the other four do
     *  not have: **what the comparison is**. It is ``gArrayPermuteBase``'s
     *  shape whole — an index order plus the shared "apply this order to the
     *  store" helper, the whole sort one hold of the store's guard through
     *  the scratch table, the reference emitted after a sort that lands so
     *  the family chains — with the order coming from a comparison over the
     *  elements instead of from arithmetic or a draw.
     *
     *  ### The comparison — #789's decision, written down
     *
     *  ``.zl sort``'s ordering (``AtomsBefore``), applied to the store's
     *  elements, so a stored array and the list it spells sort identically:
     *
     *  - **Each element is classified once per sort**, under the guard, with
     *    ``ReadNumericToken`` — the same classifier ``AtomList`` applies to
     *    every atom on the way in — into "one finite number" or "a symbol".
     *  - **Numbers come before symbols in both directions.** Which of the
     *    two an element is, is a type ordering rather than a value one, and
     *    a descending sort that swept every symbol to the front would make
     *    ascending and descending two different questions rather than one
     *    asked two ways — ``.zl sort``'s rule, kept verbatim.
     *  - **Numbers compare by value**, so ``7`` and ``7.`` are the same
     *    number here exactly as they are to ``.zl`` — spelling decides how
     *    an element leaves, never where it sorts. **Symbols compare by
     *    their characters**, case-sensitively as every comparison in this
     *    patcher is, with the shorter first when one is a prefix of the
     *    other; ``.tolower`` exists to fold a case-blind sort first.
     *  - **The sort is stable** — equal elements keep the order they
     *    arrived in. That is what makes the published order one a patch can
     *    reason about, and it is bought the way ``.zl`` bought it: a
     *    bottom-up merge sort through a fixed scratch, O(n log n) whatever
     *    the data, on a path the audio callback takes.
     *
     *  ### Ascending, descending — and the second outlet
     *
     *  The direction is ``.array.rotate``'s arrangement with ``.zl sort``'s
     *  meaning: the second creation argument seeds it, an int on the
     *  direction inlet moves it silently, and **negative sorts descending,
     *  anything else ascending** — so an unset argument is an ascending
     *  sort, Max's default. An int on the trigger sorts with that direction
     *  at the moment it arrives and stores nothing, ``gArrayIndexMap``'s
     *  trigger rule.
     *
     *  A sort that lands **publishes the applied order** — the picks as
     *  zero-based indices into the array as it stood — out the order
     *  outlet, *before* the reference leaves: Max's ``zl sort`` index map
     *  and ``gArrayScrambleBase``'s exact arrangement, the idiom that lets
     *  a patch sort one array and put a parallel array into the same new
     *  order through an ``.array.indexmap``. The family precedent answers
     *  #789's second-outlet question with yes.
     *
     *  ### What arrives, and what leaves
     *
     *  Three inlets, two outlets — ``.array.rotate``'s inlets over the
     *  randomising pair's outlets:
     *
     *  - **A bang on the trigger sorts the bound array in place** by the
     *    stored direction; an empty array sorts to itself and still
     *    announces, though there is no order to publish.
     *  - **An int on the trigger sorts by that direction at the moment it
     *    arrives, and stores nothing**; a float truncates to an int first,
     *    and a list spelling exactly one signed int is the direction it
     *    spells, kept equivalent to the int.
     *  - **An int on the direction inlet stores the direction, silently** —
     *    the cold half of the Max idiom. A float truncates first; a
     *    non-finite one is refused rather than quietly becoming ascending.
     *  - **``array <name>`` on the trigger sorts by the stored direction**
     *    when it names the array already bound — the family's gesture — and
     *    on the reference inlet it is acknowledged silently; anything else
     *    on either is refused and counted.
     *  - **The order outlet publishes the applied order, then the reference
     *    outlet emits the bound array's reference** — right before left. A
     *    lost guard emits nothing (counted), and an unnamed object sorts
     *    its private array silently.
     *
     *  The mid-walk question is answered as the whole family answers it:
     *  there is no walk to be in the middle of. The elements are classified,
     *  ordered and rearranged under one hold of the store's guard, so the
     *  order applied is the array as it stood at the trigger — which is also
     *  why a sort, like every permutation, can never miss.
     */
    class gArraySort : public gArrayPermuteBase {
    public:
      gArraySort();
      const char* Type() const override {
        return YSE::OBJ::G_ARRAY_SORT;
      }
      CREATE(gArraySort)

      /** @brief The stored direction the next bang sorts by — the last int
       *         received on the direction inlet, seeded by the second
       *         creation argument (0 when absent). Negative sorts
       *         descending, anything else ascending. */
      int Direction() const {
        return direction.load(std::memory_order_relaxed);
      }

      void BangIn(int inlet, YSE::THREAD thread);
      void IntIn(int value, int inlet, YSE::THREAD thread);
      void FloatIn(float value, int inlet, YSE::THREAD thread);
      void ListIn(const std::string& value, int inlet, YSE::THREAD thread);

    protected:
      // Extends gArrayEndsBase's hook so a re-parse resets the stored
      // direction along with the name: SetParams("") must not keep sorting
      // by whatever the previous arguments planted. gArrayRotate's rule.
      void ClearParams() override;

    private:
      // The sort: one hold of the store's guard around the classification,
      // the stable merge order and the shared apply; release, then the
      // order out the right outlet and the reference out the left.
      void Sort(int by, YSE::THREAD thread);

      // Strictly "element `i` sorts before element `j`" under the current
      // classification — .zl sort's AtomsBefore over the store's elements.
      // Read under the guard, like everything it reads.
      bool Before(std::size_t i, std::size_t j, bool descending) const;

      // Fill `order[0..count)` with the stable ascending/descending index
      // order — gZl's SortIndices through `merge`. The caller holds the
      // store's guard.
      void SortOrder(std::size_t count, bool descending);

      // The stored direction. Atomic because a number may arrive on any
      // thread while the control thread re-parses the creation arguments;
      // never a lock. Negative descends — see the class notes.
      std::atomic<int> direction{0};

      // The merge sort's scratch half — gZl's `merge`, sized like the
      // base's `order` table it merges into.
      std::uint16_t merge[arrayStore::MAX_ELEMENTS] = {};

      // The per-sort classification, filled under the guard before the
      // order is computed so the O(n log n) comparisons never re-read the
      // same characters — AtomList's own arrangement, per sort instead of
      // per entry because the store keeps text, not atoms.
      bool elementIsNumber[arrayStore::MAX_ELEMENTS] = {};
      float elementValue[arrayStore::MAX_ELEMENTS] = {};

      // The applied order as a list, built under the guard and sent after
      // it is released — gArrayScrambleBase's arrangement. Both fixed at
      // construction.
      AtomList orderOut;
      std::string orderRender;
    };

  } // namespace PATCHER
} // namespace YSE
