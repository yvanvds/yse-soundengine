#pragma once
#include "../pObject.h"
#include "gArray.h"
#include "gArrayEnds.h"
#include <atomic>
#include <cstddef>
#include <string>

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief Output an array's elements one at a time — Max's ``array.iter``
     *         on the name-addressed value model ``.array`` settled (issue
     *         #798).
     *
     *  The ``.uzi`` / ``.iter`` shape applied to stored data: every element
     *  leaves as an ordinary message, one send per element, and the object is
     *  what every "do this for each element" patch is built from. ``.iter``
     *  walks the list it was just handed; this walks the array a patch has
     *  been building up by name — with ``.uzi`` driving indices and ``.iter``
     *  serialising lists, it is the patcher's third iteration primitive.
     *
     *  An array never travels down a cord — an ``OUT_TYPE`` carries a value,
     *  not an identity (see gArray.h for the whole argument) — so the array is
     *  **bound from the creation argument**, on the control thread:
     *  ``.array.iter <name>`` holds one ``shared_ptr<arrayStore>`` resolved in
     *  ``SetParent`` / ``PARM_PARSE`` / ``RefreshBinding`` on
     *  ``gArrayEndsBase``, exactly as the rest of the family, and
     *  ``patcherImplementation::SetName`` re-anchors it through the shared
     *  base's virtual ``RefreshBinding``.
     *
     *  ### What arrives, and what leaves
     *
     *  - **A bang on the trigger inlet walks**: one send per element out the
     *    element outlet, first to last — the order that makes the object a
     *    serialiser, ``.iter``'s own — and, after the last element, one bang
     *    out the done outlet. Each element leaves **typed** the way the
     *    patcher spells it (``SendAtom`` — an int, a float or a symbol by its
     *    spelling), so it reaches the inlets an uncollected value would have
     *    reached, which is the whole point of the object. Each send completes
     *    in full — the whole subgraph behind the outlet, depth first — before
     *    the next element leaves.
     *  - **``array <name>`` on the trigger inlet also walks** — the message an
     *    ``.array``'s reference outlet emits on a bang, so wiring that outlet
     *    here gives the family's gesture: bang the array, out stream the
     *    elements. Honoured only when it names the array already bound —
     *    ``ArrayReferenceNames``, the bounded compare — and refused and
     *    counted otherwise, because resolving an unrecognised name means the
     *    registry's mutex on whatever thread the message arrived on.
     *  - **``array <name>`` on the reference inlet is acknowledged silently**
     *    when it names the bound array, so a patch may wire the reference cord
     *    across without triggering — ``gDictSlice``'s inlet rule, the family's
     *    shape. Anything else there is refused and counted.
     *
     *  ### The done bang, and the outlet order
     *
     *  The element outlet is the left outlet and the done outlet the right
     *  one, and the done bang leaves **last** — the deliberate exception to
     *  the family's right-to-left rule, exactly as Max states it for
     *  ``.uzi``'s carry and as ``.dict.iter`` already applies it: it means
     *  "all the elements have been sent", so it cannot precede them. It fires
     *  even when the array is empty — ``.uzi``'s rule for a count of zero: a
     *  loop that does not run is not an error, and the "and afterwards, do
     *  this" branch must not be silently skipped. An unnamed (private) array
     *  is always empty. A *refused* walk — a lost guard, a re-entrant
     *  trigger — emits no done bang: nothing was walked, and a done that fired
     *  anyway would say something that did not happen.
     *
     *  ### The walk is snapshotted — the decision issue #798 asks for
     *
     *  #798 inherits #548's warning that ``insert`` and ``delete`` renumber,
     *  and asks — more sharply than anywhere else in the family — what a
     *  write arriving mid-walk does: a ``remove`` moves every element above
     *  the cursor down by one. Each element's send runs the whole downstream
     *  subgraph before the next element leaves, and that subgraph may well
     *  write into this very array — the store cannot be guarded across an
     *  outlet send (see ``arrayStoreGuard``), so elements could move under a
     *  cursor walking the live table. Of the three options the issue names —
     *  restart, skip, snapshot — the walk is **snapshotted**: the trigger
     *  copies the array out under its guard into a snapshot the object
     *  pre-allocated at construction, releases, and walks the snapshot. A
     *  mutation arriving mid-walk — from the elements' own subgraph or from
     *  another thread — lands in the store and changes nothing about the walk
     *  in flight: every element the array held at the trigger is emitted
     *  exactly once, none is skipped by a renumbering ``remove`` and none is
     *  visited twice. That is the only one of the three with an output a
     *  patch can reason about, and it is ``.dict.iter``'s answer to the
     *  identical question (#773), inherited deliberately.
     *
     *  The snapshot is also what gives the object **its own cursor**: two
     *  ``.array.iter`` on one name walk independently — ``.coll``'s
     *  per-object pointer rule, the one #548 prescribes for every object that
     *  walks — because each walks its own snapshot, and nothing about a walk
     *  is kept in the shared store.
     *
     *  ### Re-entrancy
     *
     *  The object emits in a loop, so a cord from either outlet back to its
     *  inlet — directly or round a chain — re-enters the handler from
     *  *inside* the walk, and letting it through would restart the walk and
     *  rewrite the very snapshot being walked. A single test-and-set guard is
     *  therefore held across the whole walk, and a trigger that finds it
     *  taken — the loop-back, or another thread — is refused and counted,
     *  exactly as ``.iter`` refuses mid-walk and ``.uzi`` refuses a
     *  re-entrant start.
     *
     *  ### The message budget
     *
     *  One trigger becomes as many sends as the array holds elements, plus
     *  the done bang, all inside the call frame of the one ``inlet::Set*``
     *  that started it — at most ``arrayStore::MAX_ELEMENTS`` (256) subgraph
     *  traversals per stimulus. That is a bound on the *work*, not a promise
     *  the work fits an audio block, and the number worth knowing beside it
     *  is ``.iter``'s: the patcher's value-command queue (#225) is 256 deep,
     *  so a full array streamed into a ``.s`` from the control thread can
     *  saturate it in one burst and hit its documented drop-and-log
     *  backpressure.
     *
     *  ### Real-time behaviour
     *
     *  ``Calculate()`` does nothing — the object is driven by its inlet, the
     *  rule ``.value``, ``.coll``, ``.dict`` and ``.array`` establish. No
     *  message path allocates, locks or blocks: the name is resolved on the
     *  control thread, the snapshot rows are allocated whole by
     *  ``arrayStore``'s own constructor, each copy under the guard is a
     *  bounded ``assign`` into storage that already exists, and each element
     *  is sent through ``SendAtom`` over a scratch reserved at construction.
     *  Refusals are counted (``Dropped()``), never logged.
     */
    class gArrayIter : public gArrayEndsBase {
    public:
      gArrayIter();
      const char* Type() const override {
        return YSE::OBJ::G_ARRAY_ITER;
      }
      CREATE(gArrayIter)

      /**
       *  @brief Most elements one trigger can emit —
       *         ``arrayStore::MAX_ELEMENTS``.
       *
       *  The documented message budget: one trigger costs one send per
       *  element plus the done bang, each running its whole subgraph, on
       *  whichever thread the trigger arrived on.
       */
      static constexpr std::size_t MAX_ITEMS = arrayStore::MAX_ELEMENTS;

      void BangIn(int inlet, YSE::THREAD thread);
      void ListIn(const std::string& value, int inlet, YSE::THREAD thread);

    private:
      // The walk itself: snapshot the store under its guard, emit one typed
      // element per snapshot row with no guard held, bang the done outlet
      // after the last. Refuses (counted) on a lost guard or a re-entrant
      // trigger — see the class notes on both.
      void Walk(YSE::THREAD thread);

      // What the walk actually walks: the array as it stood at the trigger,
      // copied out under the store's guard so no renumbering write from the
      // elements' own subgraph can move rows under the cursor. Allocated
      // whole by arrayStore's own constructor, on the control thread, once.
      arrayStore snapshot;

      // Render buffer for the element outlet, reserved to
      // AtomList::RENDER_CAPACITY at construction. Only SendAtom's symbol
      // path touches it at all.
      std::string emitScratch;

      // The re-entrancy guard, held across the whole walk: a trigger looping
      // back from either outlet, or arriving from another thread mid-walk,
      // would rewrite the snapshot being walked. The loser is dropped and
      // counted rather than made to spin — .iter's guard, for .iter's reason.
      std::atomic<bool> busy{false};
    };

  } // namespace PATCHER
} // namespace YSE
