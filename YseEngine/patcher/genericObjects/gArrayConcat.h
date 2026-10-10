#pragma once
#include "../pAtomList.h"
#include "../pObject.h"
#include "gArray.h"
#include "gArrayEnds.h"
#include "gArraySetOps.h"
#include <cstddef>
#include <string>

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief Output two arrays as one — Max's ``array.concat`` on the
     *         name-addressed value model ``.array`` settled (issue #793).
     *
     *  The combining half of the "put these together" pair (#793): the left
     *  array's elements followed by the right array's, everything kept —
     *  repeats included, order preserved — where the set operations thin.
     *  Max's ``array.concat`` appends the right inlet's data to the left
     *  inlet's and outputs a new array, originals unmodified; the port keeps
     *  exactly that read-only contract on ``gArraySetOpBase``'s body, whole:
     *
     *  - **Two names bound at creation** — ``.array.concat <left> <right>``,
     *    both resolved on the control thread, neither re-pointable from a
     *    message. The trigger inlet honours only the left array's reference,
     *    the right inlet only acknowledges the right one.
     *  - **The result leaves as list text, never as a new named array** —
     *    the value model's form of "a new array", lossless into another
     *    ``.array`` by the store's one-atom-per-element rule. Two empty
     *    arrays bang the empty outlet.
     *  - **No two guards are ever held at once** — the left array is
     *    snapshotted under its guard, the result built against the right
     *    store under that guard alone, so ``.array.concat seq seq`` answers
     *    the array doubled instead of tripping over its own try-lock.
     *  - **A result that outruns what a cord carries is refused whole** and
     *    counted — two half-full arrays already spell more atoms than a list
     *    holds, and a partial concatenation would be truncation by another
     *    name. ``.array.at``'s whole-reply rule.
     *
     *  The one thing that is this object's own is ``CollectLocked``: the
     *  snapshot's elements in order, then the right store's, no thinning and
     *  no membership test — which is why the base makes that step virtual.
     */
    class gArrayConcat : public gArraySetOpBase {
    public:
      gArrayConcat();
      const char* Type() const override {
        return YSE::OBJ::G_ARRAY_CONCAT;
      }
      CREATE(gArrayConcat)

    protected:
      // Everything, in order, repeats kept — concatenation is not a set
      // operation. The caller holds the right store's guard; allocation-free
      // (bounded Adds into the result reserved at construction).
      bool CollectLocked() override;
    };

    /**
     *  @brief Join an array's elements into one symbol — Max's ``array.join``
     *         on the value model ``.array`` settled (issue #793).
     *
     *  Max's "join the elements of an array together to form a string. The
     *  optional separator string will be placed between each element", on the
     *  family's shape: the array is bound from the first creation argument
     *  (``gArrayEndsBase``, whole), the separator is the second — a single
     *  token, empty when absent, which is Max's default — and a bang asks.
     *
     *  ### The result is a message down a cord, not an element
     *
     *  The decision #793 asks for: a joined symbol is **sent straight out the
     *  outlet**, bounded by what a cord carries (``JOINED_CAPACITY``, the
     *  same ceiling every rendered list send has), *not* by the store's
     *  ``ELEMENT_CAPACITY`` — the answer that does not need the element
     *  bound. The result is not stored anywhere, so the element rule has no
     *  claim on it; a patch that pushes it back into an ``.array`` meets that
     *  bound at the push, where it belongs. A result past the cord's ceiling
     *  is refused whole and counted — a partial join would be truncation —
     *  and ``.array.at``'s whole-reply rule holds.
     *
     *  Elements and separator alike are single tokens (the store's
     *  one-atom-per-element rule; a creation argument cannot contain a
     *  space), so the joined text is always **one token**, and it leaves
     *  typed the way the patcher spells it — ``SendAtom``'s rule, the
     *  family's transport convention: ``1 2`` joined by nothing spells
     *  ``12`` and leaves as that int, ``c4 e4`` joined by ``-`` leaves as
     *  the symbol ``c4-e4``. A space separator is unspellable as a creation
     *  argument, deliberately un-missed: space-joined elements are exactly
     *  the list text ``.array``'s own ``getvalue`` already emits.
     *
     *  Two inlets, two outlets — ``gArrayUnique``'s shape: the trigger takes
     *  a bang or the bound array's reference (the family's gesture), the
     *  reference inlet acknowledges the bound name silently, the joined
     *  outlet carries the answer and the empty outlet bangs for an empty or
     *  unnamed (private) array — "no data" is a state a patch must be able
     *  to route on, not an error.
     *
     *  Read-only over one binding: the whole join happens under **one** hold
     *  of the store's guard (the family's mid-walk answer — there is no walk
     *  to be in the middle of), into a buffer reserved at construction, and
     *  the send happens after release. No message path allocates, locks or
     *  blocks; refusals are counted, never logged.
     */
    class gArrayJoin : public gArrayEndsBase {
    public:
      gArrayJoin();
      const char* Type() const override {
        return YSE::OBJ::G_ARRAY_JOIN;
      }
      CREATE(gArrayJoin)

      /**
       *  @brief Longest joined result, in characters — what a cord carries.
       *
       *  ``AtomList::RENDER_CAPACITY``: the ceiling every rendered list send
       *  already has, applied to the one token a join spells. Past it the ask
       *  is refused whole and counted, never truncated.
       */
      static constexpr std::size_t JOINED_CAPACITY = AtomList::RENDER_CAPACITY;

      /** @brief The separator placed between each pair of elements — the
       *         second creation argument, or empty (Max's default) when
       *         absent. */
      const std::string& Separator() const {
        return separator;
      }

      void BangIn(int inlet, YSE::THREAD thread);
      void ListIn(const std::string& value, int inlet, YSE::THREAD thread);

    protected:
      // Extends gArrayEndsBase's hook so a re-parse resets the separator
      // along with the name: SetParams("") must not keep gluing with
      // whatever the previous second argument spelled. gArraySliceBase's
      // rule.
      void ClearParams() override;

    private:
      // What a bang and the reference gesture both come down to: build the
      // joined text under one hold of the store's guard, release, then send
      // — the token out the joined outlet, or a bang out the empty outlet
      // when the array held nothing to join.
      void Ask(YSE::THREAD thread);

      // The separator. A single token; empty joins the elements butted
      // together, Max's default. Read on the message path exactly as
      // `arrayName` is — both are written on the control thread only.
      std::string separator;

      // The joined text, built under the guard and sent after it is
      // released. Reserved to JOINED_CAPACITY at construction — the price of
      // never allocating on a message path.
      std::string joined;

      // Scratch for SendAtom's symbol path, reserved to
      // AtomList::RENDER_CAPACITY at construction.
      std::string emitScratch;
    };

  } // namespace PATCHER
} // namespace YSE
