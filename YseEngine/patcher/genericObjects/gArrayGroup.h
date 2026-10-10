#pragma once
#include "../pAtomList.h"
#include "../pObject.h"
#include "gArray.h"
#include "gArrayEnds.h"
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief Group an array's elements by value — issue #801's reading of
     *         Max's ``array.group`` on the name-addressed value model
     *         ``.array`` settled (#548).
     *
     *  The bucketing operation: collect equal elements together, one bucket
     *  per distinct value — "which notes share a pitch class", the analytical
     *  step that precedes most statistics. ``.array.unique`` answers *which*
     *  values occur; this answers *what occurs together*: every bucket whole,
     *  so its size is the value's frequency and ``.array.mode``'s winner is
     *  simply the longest message this object emits.
     *
     *  A deliberate divergence, written down as #801 asks: Max's own
     *  ``array.group`` is a count batcher — it accumulates incoming elements
     *  and emits an array per ``groupsize`` received. On the value model that
     *  accumulation already exists as ``.array``'s own ``append`` (an array
     *  never travels down a cord, so there is nothing to batch *in transit*),
     *  and #801 specifies the by-value grouping instead — the operation the
     *  family was otherwise missing.
     *
     *  An array is addressed by name and never passed down a cord — an
     *  ``OUT_TYPE`` carries a value, not an identity (see gArray.h for the
     *  whole argument) — so the array is **bound from the creation argument**,
     *  on the control thread: ``.array.group <name>`` resolves the name in
     *  ``SetParent`` / ``PARM_PARSE`` / ``RefreshBinding`` on
     *  ``gArrayEndsBase``, exactly as the rest of the family, and
     *  ``patcherImplementation::SetName`` re-anchors it through the shared
     *  base's virtual ``RefreshBinding``.
     *
     *  ### The output shape — the question #801 says is the whole question
     *
     *  An element is one atom, so an array cannot hold an array, and a group
     *  of groups therefore cannot leave as one value. Of the two shapes the
     *  issue names — several named result arrays, or one message per group —
     *  this object emits **one message per group**, ``.array.iter``'s shape
     *  and the issue's own recommendation: creating a named array from a
     *  message path would mean resolving a name on whichever thread the
     *  trigger arrived on, exactly what the binding rule exists to keep off
     *  cords. Each bucket leaves whole out the group outlet, groups in order
     *  of their value's **first occurrence** (``.zl thin``'s order, the one
     *  ``.array.unique`` already emits the keys in), and each is sent typed
     *  (``SendAtoms``): a bucket of one leaves as the int, float or symbol it
     *  spells, a bucket of several as one list — the value repeated, since
     *  equality is the whole of what a bucket means here. After the last
     *  bucket, one bang out the done outlet.
     *
     *  ### Equality is the spelling
     *
     *  ``ArrayFind``'s byte compare, the family's rule (``.array.mode``,
     *  ``.array.unique``, ``.array.indexof``): ``7`` and ``7.`` are different
     *  elements, because they are a different atom downstream. A "group by
     *  pitch class" is this object downstream of ``.array.map "$1 % 12"``,
     *  not a numeric tolerance here.
     *
     *  ### What arrives, and what leaves
     *
     *  - **A bang on the trigger inlet groups**: one send per bucket out the
     *    group outlet, then the done bang. Each send completes in full — the
     *    whole subgraph behind the outlet, depth first — before the next
     *    bucket leaves.
     *  - **``array <name>`` on the trigger inlet also groups** — the message
     *    an ``.array``'s reference outlet emits on a bang, the family's
     *    gesture. Honoured only when it names the array already bound —
     *    ``ArrayReferenceNames``, the bounded compare — and refused and
     *    counted otherwise, because resolving an unrecognised name means the
     *    registry's mutex on whatever thread the message arrived on.
     *  - **``array <name>`` on the reference inlet is acknowledged silently**
     *    when it names the bound array — ``gDictSlice``'s inlet rule, the
     *    family's shape. Anything else there is refused and counted.
     *
     *  ### The done bang
     *
     *  The done bang leaves **last** — the carry exception to right-to-left,
     *  exactly as ``.array.iter`` and ``.uzi`` state it: it means "all the
     *  buckets have been sent", so it cannot precede them. It fires even for
     *  an empty or unnamed (private) array — a grouping of nothing is no
     *  buckets, not an error — but never for a **refused** grouping: a lost
     *  guard, a re-entrant trigger, or a bucket that cannot leave whole emit
     *  no done bang, because nothing (or not everything) was grouped.
     *
     *  ### The grouping is snapshotted — #548's renumbering warning, answered
     *
     *  Each bucket's send runs the whole downstream subgraph before the next
     *  leaves, and that subgraph may write into this very array; the store
     *  cannot be guarded across an outlet send (see ``arrayStoreGuard``).
     *  So the trigger copies the array out under one hold of the store's
     *  guard into a snapshot the object pre-allocated at construction,
     *  releases, and groups the snapshot — ``.array.iter``'s answer (#798),
     *  inherited deliberately: a mutation arriving mid-grouping lands in the
     *  store and changes nothing about the buckets in flight, so every
     *  element the array held at the trigger is bucketed exactly once. The
     *  snapshot is also the object's own cursor (``.coll``'s per-object
     *  pointer rule): two ``.array.group`` on one name group independently.
     *
     *  ### Whole buckets, or nothing
     *
     *  A bucket is bounded by what a cord carries (``AtomList``'s
     *  ``TEXT_CAPACITY``), and 256 copies of a 64-character element outrun
     *  it. A bucket that lost members would lie about the value's frequency —
     *  and by the time it is discovered, earlier buckets may already have
     *  been sent and cannot be unsaid — so the fit of **every** bucket is
     *  checked before the first send: a grouping any bucket of which cannot
     *  leave whole is refused whole and counted, nothing emitted, no done
     *  bang. ``.array.sect``'s whole-refusal rule (#792), for the same
     *  reason: a partial answer about membership is a lie.
     *
     *  ### Re-entrancy
     *
     *  The object emits in a loop, so a cord from either outlet back to its
     *  inlet re-enters the handler from *inside* the grouping, and letting it
     *  through would rewrite the very snapshot being walked. A single
     *  test-and-set guard is held across the whole grouping, and a trigger
     *  that finds it taken — the loop-back, or another thread — is refused
     *  and counted, ``.array.iter``'s rule (#798).
     *
     *  ### The message budget
     *
     *  One trigger becomes at most one send per distinct value plus the done
     *  bang — at most ``arrayStore::MAX_ELEMENTS`` (256) subgraph traversals
     *  per stimulus, ``.array.iter``'s bound, reached only by an array with
     *  no repeats at all. The bucketing itself is a bounded quadratic scan
     *  (first-occurrence search over at most 256 elements of at most 64
     *  characters), the family's ``ArrayFind`` cost shape.
     *
     *  ### Real-time behaviour
     *
     *  ``Calculate()`` does nothing — the object is driven by its inlet, the
     *  family's rule. No message path allocates, locks or blocks: the name is
     *  resolved on the control thread, the snapshot rows are allocated whole
     *  by ``arrayStore``'s own constructor, each copy under the guard is a
     *  bounded ``assign`` into storage that already exists, the bucket tables
     *  are fixed members, and each bucket is built into an ``AtomList``
     *  reserved at construction and sent through ``SendAtoms`` over a scratch
     *  reserved alongside it. Refusals are counted (``Dropped()``), never
     *  logged.
     */
    class gArrayGroup : public gArrayEndsBase {
    public:
      gArrayGroup();
      const char* Type() const override {
        return YSE::OBJ::G_ARRAY_GROUP;
      }
      CREATE(gArrayGroup)

      /**
       *  @brief Most buckets one trigger can emit —
       *         ``arrayStore::MAX_ELEMENTS``.
       *
       *  The documented message budget: one trigger costs one send per
       *  distinct value plus the done bang, each running its whole subgraph,
       *  on whichever thread the trigger arrived on.
       */
      static constexpr std::size_t MAX_GROUPS = arrayStore::MAX_ELEMENTS;

      void BangIn(int inlet, YSE::THREAD thread);
      void ListIn(const std::string& value, int inlet, YSE::THREAD thread);

    private:
      // The grouping itself: snapshot the store under its guard, bucket the
      // snapshot by spelling, prove every bucket fits a cord, then emit one
      // send per bucket with no guard held and bang the done outlet after the
      // last. Refuses (counted) on a lost guard, a re-entrant trigger or a
      // bucket that cannot leave whole — see the class notes on each.
      void Group(YSE::THREAD thread);

      // What the buckets are built from: the array as it stood at the
      // trigger, copied out under the store's guard so no renumbering write
      // from the buckets' own subgraph can move elements mid-grouping.
      // Allocated whole by arrayStore's own constructor, on the control
      // thread, once.
      arrayStore snapshot;

      // The bucket table, rebuilt per trigger: bucket `g` is
      // `memberCount[g]` occurrences of the element at snapshot row
      // `firstAt[g]` — nothing more needs remembering, because equality is
      // the whole spelling, so a bucket's members are one text repeated.
      std::uint16_t firstAt[arrayStore::MAX_ELEMENTS] = {};
      std::uint16_t memberCount[arrayStore::MAX_ELEMENTS] = {};

      // Where each bucket is assembled before it leaves — fixed storage,
      // reserved by AtomList's own constructor, so building a bucket on a
      // message path allocates nothing.
      AtomList emitList;

      // Render buffer for the group outlet, reserved to
      // AtomList::RENDER_CAPACITY at construction.
      std::string emitScratch;

      // The re-entrancy guard, held across the whole grouping: a trigger
      // looping back from either outlet, or arriving from another thread
      // mid-grouping, would rewrite the snapshot being walked. The loser is
      // dropped and counted rather than made to spin — .array.iter's guard,
      // for .array.iter's reason.
      std::atomic<bool> busy{false};
    };

  } // namespace PATCHER
} // namespace YSE
