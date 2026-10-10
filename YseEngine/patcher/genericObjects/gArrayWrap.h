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
     *  @brief Output the element at an index, wrapping — Max's ``array.wrap``
     *         on the name-addressed value model ``.array`` settled (issue
     *         #809).
     *
     *  The whole family treats an index as a position: out of range is a miss
     *  on a fetch and a counted refusal elsewhere, negatives included — never
     *  wrapped and never clamped, ``arrayStore``'s rule, decided once. **This
     *  is the object that exists to provide the alternative.** Every index
     *  lands: taken modulo the length, so 5 into a three-element array reads
     *  position 2 and -1 reads the last element — the modulo addressing a
     *  sequencer does every bar, counting past the end of its pattern without
     *  a miss.
     *
     *  Everything about the binding is ``gArrayEndsBase``'s, inherited whole:
     *  the array is bound from the first creation argument on the control
     *  thread, an ``array <name>`` message is honoured only when it names the
     *  array already bound, an unnamed object reads a private, empty array of
     *  its own, and refusals are counted, never logged.
     *
     *  ### What arrives, and what leaves
     *
     *  Two inlets — one for the index and a second for the reference,
     *  ``.array.at``'s arrangement, kept so a patch can swap the strict fetch
     *  for the wrapping one without rewiring:
     *
     *  - **An int on the index inlet stores the index and fetches**: the
     *    element at that position modulo the length leaves the element
     *    outlet, typed the way the patcher spells it (``SendAtom`` — an int,
     *    a float or a symbol by its spelling). **A negative index counts from
     *    the end** — -1 the last element, -2 the one before it — and keeps
     *    wrapping past that, so -4 into a three-element array is the last
     *    element again. Where ``.array.at`` refuses a negative, here it is
     *    the point.
     *  - **A float truncates to an int** — Max's float method on an int
     *    attribute, ``.table``'s and ``.array.at``'s precedent — then behaves
     *    as the int would. A non-finite float is refused: NaN spells no
     *    position, and folding it to 0 would quietly fetch element 0.
     *  - **A bang re-fetches at the stored index** — the index the last int
     *    or float stored, seeded by the second creation argument (0 when
     *    absent).
     *  - **A list of indices is honoured** — Max's ``array.wrap`` takes
     *    several — and answered **whole**: one list of the named elements, in
     *    the order asked, out the element outlet, every position wrapped
     *    independently. A list with anything in it that is not an integer is
     *    refused whole and counted — half a reply would misalign every
     *    position after the cut — and a list never moves the stored index:
     *    it is a compound fetch answered at the moment it arrives, not a
     *    cursor move. ``.array.at``'s rules, with the miss taken out.
     *  - **``array <name>`` on the index inlet fetches at the stored index**
     *    when it names the array already bound — the message an ``.array``'s
     *    reference outlet emits on a bang, so wiring that outlet here gives
     *    the family's gesture: bang the array, out comes the current
     *    element. Honoured only via ``ArrayReferenceNames``' bounded compare
     *    and refused otherwise, because resolving an unrecognised name means
     *    the registry's mutex on whatever thread the message arrived on.
     *  - **``array <name>`` on the reference inlet is acknowledged
     *    silently** when it names the bound array — ``gDictSlice``'s shape —
     *    and anything else there is refused and counted.
     *  - **An empty array bangs the empty outlet instead** — there is
     *    nothing to wrap onto: a modulus of zero names no position, so the
     *    miss the wrapping removed everywhere else survives exactly here. An
     *    unnamed (private) array is always empty. One bang even for a list
     *    fetch — the whole-reply rule again. A lost try-lock is neither: the
     *    array's state is unknown, so it is a counted refusal and no outlet
     *    fires.
     *
     *  ### A fetch is atomic — the concurrent-write decision #809 asks for
     *
     *  ``insert`` and ``delete`` renumber, and #809 asks what a write
     *  arriving mid-walk does. The answer is ``.array.at``'s: **there is no
     *  walk to be in the middle of.** The length is read and every requested
     *  position wrapped against it and copied out under **one** hold of the
     *  store's guard, into a list the object pre-allocated at construction,
     *  and sent only after releasing it — so the modulus every index wraps by
     *  is the length that same hold read, and the reply is the array as it
     *  stood at the trigger. A writer on another thread loses the try-lock
     *  while the fetch holds it (dropped and counted by the writer, the
     *  store's rule); a write from the fetch's own downstream subgraph
     *  happens after the guard is released and changes what the *next* fetch
     *  sees, never the reply in flight. Nothing of a fetch lives in the
     *  shared store — the stored index is this object's own, ``.coll``'s
     *  per-object pointer rule — so two ``.array.wrap`` on one name fetch
     *  independently, and a renumbering write between two fetches simply
     *  moves what the unchanged index wraps onto, which is what a position
     *  means.
     *
     *  ### Real-time behaviour
     *
     *  ``Calculate()`` does nothing — the object is driven by its inlet, the
     *  family's rule. No message path allocates, locks or blocks: the name is
     *  resolved on the control thread, the wrap is two integer divisions, the
     *  index list and the emit list are fixed storage reserved at
     *  construction, and every send happens after the store's guard is
     *  released. Refusals are counted (``Dropped()``), never logged.
     */
    class gArrayWrap : public gArrayEndsBase {
    public:
      gArrayWrap();
      const char* Type() const override {
        return YSE::OBJ::G_ARRAY_WRAP;
      }
      CREATE(gArrayWrap)

      /** @brief Most indices one list fetch honours — ``AtomList::MAX_ATOMS``,
       *         the patcher's bound on how many atoms travel down a cord. */
      static constexpr std::size_t MAX_INDICES = AtomList::MAX_ATOMS;

      /** @brief The stored index a bang fetches at — the last int or float
       *         received, seeded by the second creation argument. Any sign:
       *         a negative index is a position from the end here. */
      int Index() const {
        return index.load(std::memory_order_relaxed);
      }

      void BangIn(int inlet, YSE::THREAD thread);
      void IntIn(int value, int inlet, YSE::THREAD thread);
      void FloatIn(float value, int inlet, YSE::THREAD thread);
      void ListIn(const std::string& value, int inlet, YSE::THREAD thread);

    protected:
      // Extends the base's hook so a re-parse resets the stored index along
      // with the name: SetParams("") must not keep fetching at wherever the
      // previous arguments left it. gArrayPositionBase's rule.
      void ClearParams() override;

    private:
      // The fetch itself: read the length, wrap every position against it
      // and copy the elements into the emit list under one hold of the
      // store's guard, release, then send — the whole reply out the element
      // outlet, or one bang out the empty outlet when there is nothing to
      // wrap onto. See the class notes on why this is atomic.
      void Fetch(const int* indices, std::size_t count, YSE::THREAD thread);

      // A fetch at the stored index — what a bang and the reference gesture
      // both come down to. Never refuses on sign: any stored index wraps.
      void FetchAtIndex(YSE::THREAD thread);

      // The stored index. Atomic because an int may arrive on any thread
      // while the control thread re-parses the creation arguments; never a
      // lock.
      std::atomic<int> index{0};

      // The indices a list fetch asked for, parsed before the guard is
      // taken. Fixed storage — the price of never allocating on a message
      // path. Signed, because a negative index is a position here.
      int requested[MAX_INDICES] = {};

      // The reply, collected under the guard and sent after it is released.
      // An AtomList rather than a string because it carries the patcher's
      // own bound on how much list text may travel down a cord — a reply
      // that outruns it is refused whole rather than the send allocating.
      AtomList emitList;

      // Render buffer for the outlet, reserved to AtomList::RENDER_CAPACITY
      // at construction.
      std::string emitScratch;
    };

  } // namespace PATCHER
} // namespace YSE
