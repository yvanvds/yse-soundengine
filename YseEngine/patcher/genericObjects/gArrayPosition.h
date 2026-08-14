#pragma once
#include "../pAtomList.h"
#include "../pObject.h"
#include "gArray.h"
#include "gArrayEnds.h"
#include <atomic>
#include <cstddef>
#include <string>

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief Shared body for the two position-mutators of the ``array.*``
     *         family — ``.array.insert`` and ``.array.remove`` (issue #785).
     *
     *  The middle-of-the-array counterparts of the end-mutators: where
     *  ``.array.push`` and friends act on an end that names itself, these two
     *  act at a **position that arrives on a cord** — the edit ``.array``'s
     *  own ``insert`` / ``delete`` messages cannot take from a patch, because
     *  the base object needs the index inside the message text.
     *
     *  Everything about the binding is ``gArrayEndsBase``'s, inherited whole:
     *  the array is bound from the first creation argument on the control
     *  thread, an ``array <name>`` message is honoured only when it names the
     *  array already bound, an unnamed object acts on a private, empty array
     *  of its own, and refusals are counted, never logged. What this base
     *  adds is the **stored position**: the second creation argument seeds
     *  it, an int on the index inlet moves it, and the operation applies at
     *  wherever it points — ``gArrayAt``'s stored index, kept per object
     *  (``.coll``'s per-object pointer rule) so two mutators on one name
     *  address independently.
     *
     *  ### The index is a position, applied at the moment of the operation
     *
     *  Zero-based, never wrapped and never clamped — the family rule decided
     *  on ``arrayStore``. A negative index is malformed and is refused before
     *  it is stored; only a creation argument can plant one, and the apply
     *  path refuses it there. What an out-of-range *non-negative* position
     *  does differs between the two halves, and the difference is the
     *  fetch/mutation split the family already made:
     *
     *  - **``.array.insert`` refuses it, counted.** An insert is a pure
     *    mutation — nothing is fetched — so out of range is "a counted
     *    refusal elsewhere", exactly as a push onto a full array. Valid
     *    positions run 0..count *inclusive*: inserting at ``count`` appends,
     *    ``ArrayInsertAt``'s own rule.
     *  - **``.array.remove`` misses on it, on an outlet.** A remove is a
     *    fetch that also erases — the departing element leaves the object —
     *    so a position the array does not have is ``.array.at``'s miss: one
     *    bang out the miss outlet, nothing counted, nothing changed. Kept on
     *    an outlet rather than in the refusal counter because a patch editing
     *    by position must be able to *see* that the edit did not land — the
     *    remover twins' empty outlet, generalised to a position.
     *
     *  ### Every operation is one guard hold — the mid-walk answer, again
     *
     *  #785 inherits #548's warning that ``insert`` and ``delete`` renumber
     *  and asks what a write arriving mid-walk does. The answer is the one
     *  #782 and #784 gave: **there is no walk to be in the middle of**. An
     *  insert — one element or a whole list of them — is validated before the
     *  store's guard is taken and applied entirely under one hold of it; a
     *  remove reads the departing element and closes the gap under one hold.
     *  Every send happens after the guard is released, so an operation
     *  triggered by the send changes what the *next* trigger sees, never the
     *  one in flight. The stored position is this object's own and a
     *  renumbering write between two operations simply moves what the
     *  unchanged position names, which is what a position means.
     *
     *  ### Real-time behaviour
     *
     *  ``Calculate()`` does nothing — both objects are driven by their
     *  inlets, the family's rule. No message path allocates, locks or blocks:
     *  the name is resolved on the control thread, an arriving number is
     *  rendered by ``ExprFormatValue`` into a fixed ``AtomList``, a departing
     *  element is copied into a fixed buffer under the guard and sent after
     *  it, and the insert's reference message is built once per rebind.
     */
    class gArrayPositionBase : public gArrayEndsBase {
    public:
      /** @brief The stored position the next operation applies at — the last
       *         int or float received on the index inlet, seeded by the
       *         second creation argument (0 when absent). */
      int Index() const {
        return index.load(std::memory_order_relaxed);
      }

    protected:
      gArrayPositionBase();

      // Extends gArrayEndsBase's hook so a re-parse resets the stored
      // position along with the name: SetParams("") must not keep pointing at
      // wherever the previous arguments left it. gArrayAt's rule.
      void ClearParams() override;

      // Store a position arriving as an int. Negative is refused and counted,
      // and the stored position does not move — an operation after the
      // refusal applies where it would have applied before it.
      void StoreIndex(int value);

      // The truncating float method on the index — Max's float method on an
      // int attribute, .table's and .array.at's precedent. False (counted)
      // for a negative or non-finite value, which must not quietly become
      // position 0.
      bool IndexFromFloat(float value, int& out);

      // Load the stored position for an apply. False (counted) when it is
      // negative, which only a creation argument can plant — the inlet
      // refuses one before storing it. Malformed, not a miss.
      bool LoadPosition(std::size_t& out);

      // The stored position. Atomic because a number may arrive on any thread
      // while the control thread re-parses the creation arguments; never a
      // lock.
      std::atomic<int> index{0};
    };

    /**
     *  @brief Add an element at a position, shifting the rest up — Max's
     *         ``array.insert`` on the name-addressed value model ``.array``
     *         settled (issue #785).
     *
     *  Three inlets, one outlet:
     *
     *  - **An int, a float or a symbol on the element inlet is inserted** at
     *    the stored position, as the text that spells it — the end-writers'
     *    element rules whole: a non-finite float is refused, and the bound
     *    array's own ``array <name>`` reference is refused too, an identity
     *    mis-wired into the data. **A list lands whole, in the order sent** —
     *    ``insert a b c`` at position 1 leaves ``a b c`` starting at 1 — or
     *    is refused whole, one counted refusal and nothing changed, when the
     *    position is past the end, the array cannot take all of it, or an
     *    element outruns ``ELEMENT_CAPACITY``.
     *  - **An int on the index inlet stores the position**, silently — the
     *    cold half of the Max idiom: position on the right, element on the
     *    left triggers. A float truncates to an int first; a negative or
     *    non-finite value is refused and the position does not move. Nothing
     *    else lands there.
     *  - **``array <name>`` on the reference inlet is acknowledged
     *    silently** when it names the bound array — ``gDictSlice``'s shape,
     *    exactly as on the end-mutators — and anything else there is refused.
     *  - **The outlet emits the bound array's reference after an insert that
     *    landed** — the way an array leaves an object on the value model, so
     *    the family chains. A refused insert emits nothing, and an unnamed
     *    object stays silent: the write happens, but there is no name to
     *    pass on.
     *
     *  Positions run 0..count inclusive — inserting at ``count`` appends —
     *  and out of range past that is a counted refusal, never a clamp; see
     *  ``gArrayPositionBase`` for the split with ``.array.remove``'s miss.
     */
    class gArrayInsert : public gArrayPositionBase {
    public:
      gArrayInsert();
      const char* Type() const override {
        return YSE::OBJ::G_ARRAY_INSERT;
      }
      CREATE(gArrayInsert)

      /** @brief The message the outlet emits after an insert that landed —
       *         ``"array <name>"``, or empty for an unnamed object. */
      const std::string& Reference() const {
        return reference;
      }

      void IntIn(int value, int inlet, YSE::THREAD thread);
      void FloatIn(float value, int inlet, YSE::THREAD thread);
      void ListIn(const std::string& value, int inlet, YSE::THREAD thread);

    protected:
      void ParamsChanged() override;

    private:
      // Rebuild `reference` from the current name. Control thread only.
      void RefreshReference();

      // The insert itself: validate `pending` outside the guard, apply it
      // whole under one hold at the stored position, release, then emit the
      // reference. See gArrayPositionBase on whole-or-nothing.
      void Apply(YSE::THREAD thread);

      // The elements one message inserts, parsed (and, for a number,
      // rendered) before the guard is taken. Fixed storage — the price of
      // never allocating on a message path.
      AtomList pending;

      // "array <name>", built once per rebind so a landed insert is a send
      // of a string the object already owns rather than a concatenation on
      // whichever thread the message arrived on.
      std::string reference;
    };

    /**
     *  @brief Drop the element at a position and output it — Max's
     *         ``array.remove`` on the name-addressed value model ``.array``
     *         settled (issue #785).
     *
     *  ``.array.insert``'s exact inverse over ``ArrayEraseAt``. Two inlets,
     *  two outlets:
     *
     *  - **An int on the position inlet stores the position and removes**:
     *    the departing element leaves the element outlet, typed the way the
     *    patcher spells it (``SendAtom``), and every element behind it moves
     *    down one — the read and the erase one hold of the store's guard, so
     *    the element that leaves is exactly the one that left the array. A
     *    float truncates to an int first; a negative or non-finite value is
     *    refused and the position does not move.
     *  - **A bang removes at the stored position** — the position the last
     *    int stored, seeded by the second creation argument. The position
     *    moves even when the remove missed: a miss is a property of the
     *    array at that moment, not of the request, so a bang after the
     *    array has grown removes the element the earlier ask could not —
     *    ``.array.at``'s rule.
     *  - **A position the array does not have bangs the miss outlet** —
     *    nothing removed, nothing counted; see ``gArrayPositionBase`` for
     *    why a remove misses where an insert refuses. An empty or unnamed
     *    (private) array misses on every position.
     *  - **``array <name>`` on the position inlet removes at the stored
     *    position** when it names the array already bound — the message an
     *    ``.array``'s reference outlet emits on a bang, the family gesture.
     *    Any other list is refused: a multi-position remove is not offered,
     *    because ``delete`` renumbers and a list of positions would name
     *    different elements after each erase than it did when it was sent —
     *    refused rather than guessed.
     *  - **``array <name>`` on the reference inlet is acknowledged
     *    silently** when it names the bound array, and anything else there
     *    is refused and counted.
     */
    class gArrayRemove : public gArrayPositionBase {
    public:
      gArrayRemove();
      const char* Type() const override {
        return YSE::OBJ::G_ARRAY_REMOVE;
      }
      CREATE(gArrayRemove)

      void BangIn(int inlet, YSE::THREAD thread);
      void IntIn(int value, int inlet, YSE::THREAD thread);
      void FloatIn(float value, int inlet, YSE::THREAD thread);
      void ListIn(const std::string& value, int inlet, YSE::THREAD thread);

    private:
      // The removal itself: copy the departing element and close the gap
      // under one hold of the store's guard, release, then send — the
      // element out the element outlet, or a bang out the miss outlet when
      // the position names no element.
      void Remove(std::size_t position, YSE::THREAD thread);

      // A removal at the stored position — what a bang and the reference
      // gesture both come down to. Refuses (counted) a negative stored
      // position, which only a creation argument can plant.
      void RemoveAtIndex(YSE::THREAD thread);

      // Where the departing element is copied while the guard is held, so
      // the send can happen after it is released. A plain array rather than
      // a string because it is written from inside the critical section —
      // gArray's `fetched`, for gArray's reason.
      char fetched[arrayStore::ELEMENT_CAPACITY + 1] = {};
      std::size_t fetchedLength = 0;

      // Render buffer for the element outlet, reserved to
      // AtomList::RENDER_CAPACITY at construction.
      std::string emitScratch;
    };

  } // namespace PATCHER
} // namespace YSE
