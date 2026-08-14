#pragma once
#include "../pObject.h"
#include "gArray.h"
#include "gArrayEnds.h"
#include <cstddef>
#include <memory>
#include <string>

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief Output only when the array has changed — Max's ``array.change``
     *         on the name-addressed value model ``.array`` settled (issue
     *         #800).
     *
     *  The comparison pair (#800, with ``gArrayCompare`` below): both are an
     *  element-by-element comparison, this one against a baseline the object
     *  keeps and that one against a second bound array. They are the guards a
     *  patch puts in front of expensive downstream work — "is this array
     *  still the one it was a moment ago?" asked before an ``.array.iter``
     *  walks 256 elements through a subgraph, which is exactly the scalar
     *  ``.change``'s job (#468) done for stored data.
     *
     *  An array never travels down a cord — an ``OUT_TYPE`` carries a value,
     *  not an identity (see gArray.h for the whole argument) — so the array
     *  is **bound from the creation argument**, on the control thread:
     *  ``.array.change <name>`` holds one ``shared_ptr<arrayStore>`` resolved
     *  in ``SetParent`` / ``PARM_PARSE`` / ``RefreshBinding`` on
     *  ``gArrayEndsBase``, exactly as the rest of the family, and
     *  ``patcherImplementation::SetName`` re-anchors it through the shared
     *  base's virtual ``RefreshBinding``.
     *
     *  ### What arrives, and what leaves
     *
     *  Max's ``array.change`` takes arrays on its left inlet and outputs the
     *  array when "the order or value of the elements" differs from the
     *  previous one, with a 1/0 report beside it; its right inlet stores an
     *  array without generating output. On the value model those three cords
     *  become:
     *
     *  - **A bang on the trigger inlet polls**: the bound array is compared
     *    against the baseline under one hold of the store's guard, and the
     *    baseline is replaced by what was found whenever they differ. The
     *    changed outlet then sends the verdict as an int — 1 changed, 0 same
     *    — on *every* poll that got an answer, Max's right outlet. On a
     *    change, the reference outlet also emits the bound array's reference,
     *    ``array <name>`` — the way an array leaves an object on the value
     *    model (``gArrayEndsWriter``'s outlet), so wiring it onward gates the
     *    family: into an ``.array.iter`` trigger it means "walk only when
     *    something moved". Unchanged, that outlet stays silent — which is the
     *    object's whole point — and an unnamed object stays silent there too:
     *    a private array has no name to pass on, ``gArray``'s bang rule.
     *  - **``array <name>`` on the trigger inlet also polls** — the message
     *    an ``.array``'s reference outlet emits on a bang, so wiring that
     *    outlet here gives the family's gesture. Honoured only when it names
     *    the array already bound (``ArrayReferenceNames``, the bounded
     *    compare), refused and counted otherwise.
     *  - **``array <name>`` on the baseline inlet re-baselines silently**
     *    when it names the bound array: the current contents become the
     *    baseline and *nothing* is emitted — Max's right inlet ("stores
     *    without generating output"), and the scalar ``.change``'s ``set``,
     *    whose whole value is moving the object's idea of "current" without
     *    telling anybody. Anything else there is refused and counted. Note
     *    the deliberate departure from the family's usual inlet 1, which
     *    acknowledges a reference and does nothing: here doing something *is*
     *    the Max semantics being ported, and it is still silent.
     *
     *  The two sends fire **right to left** — the verdict before the
     *  reference, the scalar ``.change``'s outlet order — so whatever the
     *  reference triggers downstream already sees the matching report.
     *
     *  ### The baseline starts empty, and what that means
     *
     *  The scalar ``.change`` starts its stored value at the creation
     *  argument's 0 rather than at "nothing received yet", so that loading a
     *  patch does not fire an event for a parameter that has not moved. The
     *  array analogue of 0 is the **empty array**: a freshly created
     *  ``.array.change`` polled over an empty array answers 0 and stays
     *  silent, and polled over an array that already has contents answers 1 —
     *  the contents are news this object has not yet reported. A re-parse
     *  (``SetParams``) and a patcher rename both reset the baseline to empty
     *  for the same reason: the object now watches a different binding, and
     *  whatever that array holds is news.
     *
     *  ### What "changed" means
     *
     *  Count and spelling, in order — Max's "order or value": the array
     *  differs from the baseline when the lengths differ or any position
     *  holds a different element, equality by the byte compare that is the
     *  family's rule (``ArrayFind``, ``.array.mode``), so ``7`` and ``7.``
     *  differ and ``10 20`` / ``20 10`` differ. Max's ``@unordered``
     *  attribute (order-insensitive comparison) is deliberately not ported:
     *  #800 defines the pair as element-by-element, and the patch-level
     *  spelling already exists — ``.array.sort`` into a scratch array, or the
     *  set operations, are where order-insensitivity lives in this family.
     *
     *  A poll is a full scan — on a full array, 256 bounded compares plus, on
     *  a change, 256 bounded assigns — which is the price the issue asks to
     *  have written down: cheap beside the downstream work the object exists
     *  to gate, but not free.
     *
     *  ### Real-time behaviour
     *
     *  ``Calculate()`` does nothing — the object is driven by its inlet, the
     *  family's rule. No message path allocates, locks or blocks: the name is
     *  resolved on the control thread, the baseline rows are allocated whole
     *  by ``arrayStore``'s own constructor, the compare and the re-baseline
     *  are bounded reads and ``assign``s into storage that already exists
     *  under one hold of the store's guard, and both sends happen after it is
     *  released — the reference as a string built once per rebind. A lost
     *  try-lock is a counted refusal and *neither* outlet fires: the array's
     *  state is unknown, and a comparator that guessed would corrupt the very
     *  state it exists to track, the scalar ``.change``'s argument for
     *  refusing a NaN. Refusals are counted (``Dropped()``), never logged.
     */
    class gArrayChange : public gArrayEndsBase {
    public:
      gArrayChange();
      const char* Type() const override {
        return YSE::OBJ::G_ARRAY_CHANGE;
      }
      CREATE(gArrayChange)

      /** @brief The message the reference outlet emits on a change —
       *         ``"array <name>"``, or empty for an unnamed object. */
      const std::string& Reference() const {
        return reference;
      }

      /** @brief How many elements the baseline holds — tests and
       *         diagnostics; the baseline itself is the object's state. */
      std::size_t BaselineCount() const {
        return snapshot.count;
      }

      void BangIn(int inlet, YSE::THREAD thread);
      void ListIn(const std::string& value, int inlet, YSE::THREAD thread);

      // A patcher rename moves the binding, so the baseline is reset along
      // with it — whatever the newly watched array holds is news. See the
      // class notes on the empty baseline.
      void RefreshBinding() override;

    protected:
      // Called after ClearParams / ParseParams have re-read the name: the
      // reference follows the name (gArrayEndsWriter's arrangement) and the
      // baseline resets — a re-parse must not keep comparing against the
      // previous binding's contents.
      void ParamsChanged() override;

    private:
      // The poll: compare the store against the baseline and replace the
      // baseline on a difference, all under one hold of the store's guard;
      // release; then send — the verdict always, the reference only on a
      // change. The only path that emits.
      void Poll(YSE::THREAD thread);

      // The silent re-baseline (the baseline inlet): copy the store into the
      // baseline under one hold of its guard and emit nothing.
      void Rebase();

      // Rebuild `reference` from the current name. Control thread only.
      void RefreshReference();

      // The baseline: the array as this object last reported it. Allocated
      // whole by arrayStore's own constructor, on the control thread, once —
      // gArrayIter's snapshot, for the same never-allocate reason.
      arrayStore snapshot;

      // "array <name>", built once per rebind so a change is a send of a
      // string the object already owns rather than a concatenation on
      // whichever thread the poll arrived on.
      std::string reference;
    };

    /**
     *  @brief Report whether two arrays hold the same thing — Max's
     *         ``array.compare`` on the name-addressed value model ``.array``
     *         settled (issue #800).
     *
     *  The other half of the comparison pair: ``gArrayChange`` compares
     *  against a baseline it keeps, this object against a second bound
     *  array. Max's object takes an array on each inlet and sends 1 when the
     *  two are equal "for their value and order", 0 when they are not —
     *  ``dict.compare``'s shape, and the port follows ``gDictCompare``
     *  (#770) as exactly as the two stores allow.
     *
     *  ### Two arrays means two names bound at creation
     *
     *  An array is addressed by name and never passed down a cord (see
     *  gArray.h), so ``.array.compare <left> <right>`` binds **both** names
     *  on the control thread — ``gArraySetOpBase``'s arrangement, mirrored:
     *  the left binding is ``gArrayEndsBase``'s own, the right one is added
     *  here, resolved in the same ``SetParent`` / ``PARM_PARSE`` /
     *  ``RefreshBinding`` moments and re-anchored by
     *  ``patcherImplementation::SetName`` on a rename through the base's
     *  virtual hook. Neither may be re-pointed from a message:
     *
     *  - **A bang on the compare inlet compares** and sends the verdict — 1
     *    equal, 0 not — out the int outlet.
     *  - **``array <left>`` on the compare inlet also compares** — the
     *    family's gesture, the message the left ``.array``'s reference outlet
     *    emits on a bang. Anything else there, the right array's name
     *    included, is refused and counted: resolving an unrecognised name
     *    means the registry's mutex on whatever thread the message arrived
     *    on.
     *  - **``array <right>`` on the right inlet is acknowledged silently** —
     *    a patch may wire both reference outlets across, as it would in Max —
     *    and anything else there is refused and counted. An unbound side
     *    reads a private, empty array of its own, ``gArray``'s rule, so an
     *    unnamed ``.array.compare`` answers 1: two empty arrays hold the same
     *    thing.
     *
     *  ### What "equal" means
     *
     *  Count and spelling, in order — Max's "value and order": the same
     *  number of elements and a byte-identical element at every position, the
     *  family's equality (``ArrayFind``'s byte compare), so ``7`` and ``7.``
     *  differ and ``10 20`` is not ``20 10``. Order is part of it because an
     *  array is a *sequence* — two arrays that spell a different list are
     *  different, whatever set they describe. Max's ``@unordered`` attribute
     *  is deliberately not ported, ``gArrayChange``'s reasons verbatim.
     *
     *  ### Two stores, and why no two guards are ever held at once
     *
     *  ``gDictCompare``'s arrangement, inherited whole with the two-name
     *  binding: the left array is copied out under its guard into a snapshot
     *  the object pre-allocated at construction, the guard is released, and
     *  the verdict is decided against the right store under *that* guard
     *  alone. Two guards at once would put a lock-ordering obligation on
     *  every pair of objects naming the same two arrays — and the same-store
     *  case, ``.array.compare seq seq``, would trip over its own try-lock and
     *  refuse every ask instead of answering the 1 it plainly is. Either
     *  guard lost is the ask dropped whole and counted, and nothing is sent:
     *  a verdict about state the object could not read would be a guess.
     *
     *  ### Real-time behaviour
     *
     *  ``Calculate()`` does nothing — the object is driven by its inlets, the
     *  family's rule. No message path allocates, locks or blocks: both names
     *  are resolved on the control thread, the snapshot's rows are reserved
     *  at construction, the comparison is bounded byte compares, and the
     *  verdict is one int sent after every guard is released. Refusals are
     *  counted (``Dropped()``), never logged.
     */
    class gArrayCompare : public gArrayEndsBase {
    public:
      gArrayCompare();
      const char* Type() const override {
        return YSE::OBJ::G_ARRAY_COMPARE;
      }
      CREATE(gArrayCompare)

      /** @brief The right array's shared name — the second creation
       *         argument, or empty for a private (empty) right side. */
      const std::string& RightName() const {
        return rightName;
      }

      /** @brief The address the right store is registered under —
       *         ``"<patcherName>.<name>"`` — or empty while it is private. */
      const std::string& RightAddress() const {
        return boundRightAddress;
      }

      void BangIn(int inlet, YSE::THREAD thread);
      void ListIn(const std::string& value, int inlet, YSE::THREAD thread);

      // Re-bind both sides after SetParent / a patcher rename — the base
      // re-anchors the left array, the override adds the right one,
      // gArraySetOpBase's arrangement on the hook the base makes virtual for
      // exactly this extension.
      void SetParent(pObject* parent) override;
      void RefreshBinding() override;

    protected:
      // Extends gArrayEndsBase's hook so a re-parse resets the right name
      // along with the left: SetParams("") must not keep comparing against
      // whatever the previous second argument pointed at.
      void ClearParams() override;

      // Called by the base after ClearParams / ParseParams have re-read the
      // names — the moment the right binding follows the left one.
      void ParamsChanged() override;

    private:
      // Point the right store at the current name and parent address — the
      // exact mirror of gArrayEndsBase::Rebind over the second name,
      // gArraySetOpBase's RebindRight. Control thread only; a no-op when the
      // address has not changed.
      void RebindRight();

      // The comparison itself: snapshot the left array under its guard,
      // decide the verdict against the right store under that guard alone,
      // send after both are released. Refuses (counted) on a lost guard.
      void Compare(YSE::THREAD thread);

      // The right array's name, address key and store — the second creation
      // argument's binding, mirroring the base's left-side trio.
      std::string rightName;
      std::string boundRightAddress;
      std::shared_ptr<arrayStore> rightStore;

      // Where the left array is copied while its guard is held, so the right
      // store's guard is never nested inside it. Allocated whole by
      // arrayStore's own constructor, on the control thread, once —
      // gDictCompare's snapshot, for gDictCompare's reason.
      arrayStore snapshot;
    };

  } // namespace PATCHER
} // namespace YSE
