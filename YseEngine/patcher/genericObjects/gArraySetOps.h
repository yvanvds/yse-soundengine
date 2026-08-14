#pragma once
#include "../pAtomList.h"
#include "../pObject.h"
#include "gArray.h"
#include "gArrayEnds.h"
#include <cstddef>
#include <memory>
#include <string>

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief Shared body for the two-array set operations of the ``array.*``
     *         family — ``.array.union`` and ``.array.sect`` (issue #792).
     *
     *  The set operations: the objects a harmonic or scale patch is written
     *  from — "which notes are in both chords", "everything either hand
     *  plays". They are read-only, ``gArrayStatsBase``'s kin rather than the
     *  mutating bases: nothing here ever writes to either store, so an ask
     *  reads both arrays and sends the answer after every guard is released.
     *
     *  ### Two arrays means two names bound at creation
     *
     *  An array is addressed by name and never passed down a cord (see
     *  gArray.h for the whole argument), so ``.array.union <left> <right>``
     *  binds **both** names on the control thread — ``gDictCompare``'s
     *  arrangement, inherited whole: the left binding is ``gArrayEndsBase``'s
     *  own, the right one is added here, resolved in the same
     *  ``SetParent`` / ``PARM_PARSE`` / ``RefreshBinding`` moments and
     *  re-anchored by ``patcherImplementation::SetName`` on a rename. Neither
     *  may be re-pointed from a message: an ``array <name>`` on the trigger
     *  inlet is honoured only when it names the *left* array, the right
     *  inlet only acknowledges the *right* one, and anything else is refused
     *  and counted — resolving an unrecognised name means the registry's
     *  mutex on whatever thread the message arrived on. An unbound side
     *  reads a private, empty array of its own, ``gArray``'s rule.
     *
     *  ### The result leaves as list text, never as a new named array
     *
     *  Max's ``array.union`` / ``array.sect`` output a new array; the value
     *  model's form of "a new array" is the list it spells (``SendAtoms`` —
     *  one element as the atom it is, several as list text). Creating a named
     *  array from a message would mean resolving a name on a message path —
     *  exactly what the binding rule exists to keep off cords — and the
     *  store's one-atom-per-element rule makes the list and the array the
     *  same thing seen twice, so the result pushed into another ``.array`` is
     *  lossless. An ask whose result is empty bangs the **empty outlet**:
     *  "no data" is a state a patch must be able to route on, not an error —
     *  and for ``sect`` an empty intersection is half the point of asking.
     *
     *  ### The semantics, which are ``.zl``'s for lists, verbatim
     *
     *  ``.zl union`` / ``.zl sect`` already establish what a set operation
     *  means here, and both objects follow them:
     *
     *  - **A set operation produces a set**: each element appears once in the
     *    result, at the position of its first occurrence, however many times
     *    either array repeats it.
     *  - ``.array.union`` is the left array thinned, followed by the right
     *    array's elements the left does not hold, thinned — Max's "if the two
     *    contain any items in common, only one will be output".
     *  - ``.array.sect`` is the left array's elements the right also holds,
     *    thinned, in the left array's order — Max's "the elements common to
     *    both".
     *  - **Equality is the spelling** — ``ArrayFind``'s byte compare, the
     *    family's rule (``.array.mode``, ``.array.indexof``): ``7`` and
     *    ``7.`` are different elements, because they are a different atom
     *    downstream.
     *
     *  ### Two stores, and why no two guards are ever held at once
     *
     *  #792 asks what happens when only one of two try-locks is won. The
     *  answer is to never be in that position — ``gDictCompare``'s
     *  arrangement, inherited with the two-name binding: the left array is
     *  copied out under its guard into a snapshot the object pre-allocated at
     *  construction, the guard is released, and the result is then built
     *  against the right store under *that* guard alone. Two guards at once
     *  would put a lock-ordering obligation on every pair of objects naming
     *  the same two arrays — and the same-store case, ``.array.union chord
     *  chord``, would trip over its own try-lock and refuse every ask. Either
     *  guard lost is the operation dropped whole and counted, the store's
     *  rule; the two holds are two moments, so a write landing between them
     *  shows in the result exactly as it would had the ask arrived after it.
     *
     *  ### Real-time behaviour
     *
     *  ``Calculate()`` does nothing — every object here is driven by its
     *  inlet, the family's rule. No message path allocates, locks or blocks:
     *  both names are resolved on the control thread, the snapshot's rows and
     *  the result list are fixed members reserved at construction, and the
     *  membership tests are ``ArrayFind``'s bounded byte compares. A result
     *  that outruns what a cord carries — a union of two large arrays can
     *  spell more atoms than a list holds — is refused whole and counted,
     *  ``.array.at``'s whole-reply rule; a partial set would be a lie about
     *  membership.
     *
     *  ### The base outlives the set operations
     *
     *  Everything above except the thinning is "read two arrays bound at
     *  creation, safely, and send one result", which is what any two-array
     *  reader needs — so the combination step is the virtual ``CollectLocked``
     *  hook, and ``gArrayConcat`` (#793) is the first subclass that is not a
     *  set operation at all: it keeps everything, repeats included, in order.
     */
    class gArraySetOpBase : public gArrayEndsBase {
    public:
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

      // Re-bind both sides after SetParent / a patcher rename: the base
      // re-anchors the left array, the override adds the right one —
      // gDictCompare's arrangement for its two dictionaries, on the hook the
      // base makes virtual for exactly this extension.
      void SetParent(pObject* parent) override;
      void RefreshBinding() override;

    protected:
      // `intersect` is the whole difference between .array.sect and
      // .array.union: keep the left elements the right also holds, against
      // keep everything either holds. A subclass that overrides
      // CollectLocked (gArrayConcat, #793) passes false — the flag only
      // drives the base's own collection.
      explicit gArraySetOpBase(bool intersect);

      // Extends gArrayEndsBase's hook so a re-parse resets the right name
      // along with the left: SetParams("") must not keep reading whatever
      // the previous second argument pointed at. gArraySliceBase's rule.
      void ClearParams() override;

      // Called by the base after ClearParams / ParseParams have re-read the
      // names — the moment the right binding follows the left one.
      void ParamsChanged() override;

      // The combination itself: build `result` from `snapshot` (the left
      // array, already copied out) and `rightStore`. The caller holds the
      // right store's guard — and only that one. False when the result
      // outran what a cord carries; the caller then refuses whole. Virtual
      // for the reason RefreshBinding is on gArrayEndsBase: a subclass whose
      // combination is not a set operation (gArrayConcat's keep-everything
      // append, #793) replaces the arithmetic while inheriting the two-name
      // binding, the snapshot and the send whole. Runs under the guard on
      // whichever thread asked, so an override must not allocate, lock or
      // block.
      virtual bool CollectLocked();

      // The right array's name, address key and store — the second creation
      // argument's binding, mirroring the base's left-side trio (and
      // protected exactly as that trio is, for the CollectLocked override).
      std::string rightName;
      std::string boundRightAddress;
      std::shared_ptr<arrayStore> rightStore;

      // Where the left array is copied while its guard is held, so the right
      // store's guard is never nested inside it. Allocated whole by
      // arrayStore's own constructor, on the control thread, once —
      // gDictCompare's snapshot, for gDictCompare's reason.
      arrayStore snapshot;

      // The result, built under the right store's guard and sent after it is
      // released. An AtomList rather than a string because it carries the
      // patcher's own bound on how much list text may travel down a cord.
      AtomList result;

    private:
      // Point the right store at the current name and parent address. The
      // exact mirror of gArrayEndsBase::Rebind over the second name. Control
      // thread only; a no-op when the address has not changed.
      void RebindRight();

      // What a bang and the left reference both come down to: snapshot the
      // left array under its guard, release, build the result against the
      // right store under that guard alone, release, then send — the result
      // out the result outlet, or a bang out the empty outlet when the
      // operation selected nothing.
      void Ask(YSE::THREAD thread);

      const bool intersect;

      // Render buffer for the result outlet, reserved to
      // AtomList::RENDER_CAPACITY at construction.
      std::string emitScratch;
    };

    /**
     *  @brief Output the elements held by either of two arrays — Max's
     *         ``array.union`` on the name-addressed value model ``.array``
     *         settled (issue #792).
     *
     *  The left array thinned, then the right array's elements the left does
     *  not hold, thinned — ``.zl union``'s order, each element once at its
     *  first occurrence. Read ``gArraySetOpBase`` for the two-name binding,
     *  the snapshot and the empty outlet.
     */
    class gArrayUnion : public gArraySetOpBase {
    public:
      gArrayUnion();
      const char* Type() const override {
        return YSE::OBJ::G_ARRAY_UNION;
      }
      CREATE(gArrayUnion)
    };

    /**
     *  @brief Output the elements held by both of two arrays — Max's
     *         ``array.sect`` on the value model ``.array`` settled
     *         (issue #792).
     *
     *  The left array's elements the right also holds, thinned, in the left
     *  array's order — ``.zl sect``'s order. An empty intersection bangs the
     *  empty outlet, which for this object is half the point of asking. Read
     *  ``gArraySetOpBase`` for the two-name binding, the snapshot and the
     *  guards.
     */
    class gArraySect : public gArraySetOpBase {
    public:
      gArraySect();
      const char* Type() const override {
        return YSE::OBJ::G_ARRAY_SECT;
      }
      CREATE(gArraySect)
    };

    /**
     *  @brief Output an array with its repeated elements dropped — Max's
     *         ``array.unique`` on the value model ``.array`` settled
     *         (issue #792).
     *
     *  The one-array set operation: each element once, at the position of its
     *  first occurrence — ``.zl thin``'s selection, under Max's
     *  ``array.unique`` name for it (``.zl``'s own ``unique`` mode is a
     *  different thing, a filter *by* another list; the reference pages
     *  agree with the port, not the shared word). Equality is the spelling,
     *  ``ArrayFind``'s byte compare, so ``7`` and ``7.`` are different
     *  elements.
     *
     *  Read-only over one binding — everything is ``gArrayEndsBase``'s: the
     *  array is bound from the creation argument on the control thread, the
     *  whole selection happens under **one** hold of the store's guard (the
     *  family's mid-walk answer: there is no walk to be in the middle of),
     *  and the result leaves as the list it spells, never as a new named
     *  array, sent after the guard is released. An empty array bangs the
     *  empty outlet; a result that outruns what a cord carries is refused
     *  whole and counted — an array of only distinct 64-character elements
     *  can spell more text than a list holds.
     */
    class gArrayUnique : public gArrayEndsBase {
    public:
      gArrayUnique();
      const char* Type() const override {
        return YSE::OBJ::G_ARRAY_UNIQUE;
      }
      CREATE(gArrayUnique)

      void BangIn(int inlet, YSE::THREAD thread);
      void ListIn(const std::string& value, int inlet, YSE::THREAD thread);

    private:
      // One hold of the store's guard around the whole selection, release,
      // then send — the thinned list, or the empty bang.
      void Ask(YSE::THREAD thread);

      // The result, built under the guard and sent after it is released.
      AtomList result;

      // Render buffer for the result outlet, reserved to
      // AtomList::RENDER_CAPACITY at construction.
      std::string emitScratch;
    };

  } // namespace PATCHER
} // namespace YSE
