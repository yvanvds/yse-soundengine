#pragma once
#include "../pObject.h"
#include "gDict.h"
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief Merge two dictionaries into one — Max's ``dict.join`` on the
     *         name-addressed value model ``.dict`` settled (issue #774).
     *
     *  Layering a preset over a base: the operation that makes dictionaries
     *  composable, so a patch keeps defaults and overrides as separate
     *  objects instead of one hand-merged table.
     *
     *  A dictionary never travels down a cord — an ``OUT_TYPE`` carries a
     *  value, not an identity (see gDict.h for the whole argument) — so all
     *  three dictionaries are **bound from the creation arguments**, on the
     *  control thread: ``.dict.join <left> <right> <target>`` holds three
     *  ``shared_ptr<dictStore>`` resolved in ``SetParent`` / ``PARM_PARSE`` /
     *  ``RefreshBinding``, exactly as ``gDict``, ``gDictCompare`` and
     *  ``gDictGroup`` resolve theirs. Three names rather than an in-place
     *  merge, because the use case is layering — both sources survive the
     *  join, so the base and the preset stay reusable — and the result goes
     *  into a bound *target* rather than a new anonymous dictionary because
     *  there is no way to hand a fresh dictionary's identity down a cord
     *  (``gDictGroup``'s rule). A patch that *wants* the in-place merge
     *  spells it: ``.dict.join a b a``, which the snapshot design below makes
     *  safe.
     *
     *  ### Which side wins a collision
     *
     *  **The right dictionary overwrites the left** — Max's own rule for
     *  ``dict.join``, and the one that makes the layering read naturally:
     *  the left argument is the base, the right argument is the override.
     *  The target is **replaced whole** by the join, so re-running it is
     *  idempotent and a stale entry from the last run cannot survive into
     *  this one.
     *
     *  ### What arrives, and what leaves
     *
     *  - **A bang on inlet 0 joins**: the target is replaced with the left
     *    dictionary's entries overlaid by the right's, and the target's
     *    reference ``dictionary <target>`` leaves the outlet so the rest of
     *    the ``dict.*`` family can pick the result up. Silent for an unnamed
     *    target, which has no name to pass on.
     *  - **``dictionary <left>`` on inlet 0 also joins** — the message a
     *    ``.dict``'s reference outlet emits on a bang, so wiring the base
     *    dictionary's reference outlet here gives Max's own gesture.
     *    Honoured only when it names the dictionary already bound —
     *    ``DictReferenceNames``, the bounded compare — and refused and
     *    counted otherwise, because resolving an unrecognised name means the
     *    registry's mutex on whatever thread the message arrived on.
     *  - **``dictionary <right>`` on inlet 1 is acknowledged and nothing
     *    more.** Max's right inlet *sets* the dictionary to join; here that
     *    binding is the second creation argument, so the reference is
     *    accepted silently when it names it — a patch may wire both
     *    reference outlets across, as it would in Max — and refused and
     *    counted when it names anything else. Re-pointing at a different
     *    name from a message is not portable, for the reason gDict.h gives
     *    for not porting ``refer``.
     *
     *  ### Refusal, never truncation — and the partial merge
     *
     *  Two dictionaries of up to ``MAX_ENTRIES`` each can join to more than
     *  ``MAX_ENTRIES``. An entry past the bound is refused whole and counted
     *  — never truncated — and the rest of the join still lands, so **a
     *  partial merge is possible**: the left dictionary's entries are placed
     *  first and the right's merged over them in storage order, and whatever
     *  fits is the result. ``.coll``'s rule for an over-long record.
     *
     *  ### How the join avoids holding two guards
     *
     *  All three stores are guarded by ``.value``-style try-locks, and taking
     *  two at once would put a lock-ordering obligation on every pair of
     *  objects that name the same dictionaries. So the join never holds two:
     *  it copies the left dictionary out under its guard into a merge buffer
     *  the object pre-allocated at construction, releases, merges the right
     *  dictionary into the buffer under that store's guard alone, releases,
     *  then replaces the target under *its* guard alone — bounded
     *  ``assign``s into pre-reserved rows, no allocation. Any aliasing
     *  between the three names — target on a source, left on right — is safe
     *  for the same reason: no store's guard is ever taken while another is
     *  held, and the sources are fully read before the target is written. A
     *  guard another thread holds refuses the whole join, counted, rather
     *  than waiting.
     *
     *  ### Real-time behaviour
     *
     *  ``Calculate()`` does nothing — the object is driven by its inlets, the
     *  rule ``.value``, ``.coll`` and ``.dict`` establish. No message path
     *  allocates, locks or blocks: the names are resolved on the control
     *  thread, the merge buffer's rows are reserved at construction, and the
     *  reference is built once per rebind so the send is of a string the
     *  object already owns.
     */
    PATCHER_CLASS(gDictJoin, YSE::OBJ::G_DICT_JOIN)
    _NO_MESSAGES
    _NO_CALCULATE

    _BANG_IN(BangIn)
    _LIST_IN(ListIn)

    _PARM_CLEAR
    _PARM_PARSE

    /** @brief The left (base) dictionary's shared name — the first creation
     *         argument, or empty for a private (empty) side. */
    const std::string& LeftName() const {
      return leftName;
    }

    /** @brief The right (override) dictionary's shared name — the second
     *         creation argument. */
    const std::string& RightName() const {
      return rightName;
    }

    /** @brief The target dictionary's shared name — the third creation
     *         argument. */
    const std::string& TargetName() const {
      return targetName;
    }

    /** @brief The address the left store is registered under —
     *         ``"<patcherName>.<name>"`` — or empty while it is private. */
    const std::string& LeftAddress() const {
      return boundLeftAddress;
    }

    /** @brief The address the right store is registered under, or empty. */
    const std::string& RightAddress() const {
      return boundRightAddress;
    }

    /** @brief The address the target store is registered under, or empty. */
    const std::string& TargetAddress() const {
      return boundTargetAddress;
    }

    /**
     *  @brief Messages and entries refused so far — an unrecognised message,
     *         a reference naming a dictionary this object is not bound to, a
     *         lost try-lock, or an entry past ``MAX_ENTRIES``.
     *
     *  A counter rather than a log line because the refusing thread may be
     *  the audio callback; ``gDict::Dropped``, for ``gDict``'s reason.
     */
    std::uint64_t Dropped() const {
      return dropped.load(std::memory_order_relaxed);
    }

    // Bind all three stores the moment the patcher name is known, so a
    // message never resolves a name. gDict::SetParent's rule.
    void SetParent(pObject* parent) override;

    // Re-bind after a patcher rename: the address prefix moved, so the object
    // now joins different dictionaries. Called from
    // patcherImplementation::SetName alongside gDict::RefreshBinding.
    void RefreshBinding();

  private:
    // Point all three stores at the current names and parent address. Control
    // thread only (SetParams / SetParent / SetName). A no-op per side when
    // that side's address has not changed.
    void Rebind();

    // Rebuild `reference` from the current target name. Control thread only.
    void RefreshReference();

    // The join itself: copy the left store into the merge buffer under its
    // guard, merge the right store into the buffer under that guard alone,
    // replace the target under its guard alone, send the target's reference
    // after every guard is released. Refuses (counted) on a lost guard;
    // refuses (counted) entries past MAX_ENTRIES.
    void Join(YSE::THREAD thread);

    void Refuse() {
      dropped.fetch_add(1, std::memory_order_relaxed);
    }

    // The shared names. First, second and third creation arguments; an empty
    // name means that side is a private, empty dictionary.
    std::string leftName;
    std::string rightName;
    std::string targetName;

    // The addresses the stores are registered under, or empty while private.
    // Also the "has the binding changed?" keys Rebind() compares against.
    std::string boundLeftAddress;
    std::string boundRightAddress;
    std::string boundTargetAddress;

    // The three dictionaries. Never null once the object has been
    // constructed, so no message handler needs a null check.
    std::shared_ptr<dictStore> leftStore;
    std::shared_ptr<dictStore> rightStore;
    std::shared_ptr<dictStore> targetStore;

    // Where the join is assembled while the source guards are held one at a
    // time, so no store's guard is ever nested inside another's — and why
    // aliasing between the three names is safe. Allocated whole by
    // dictStore's own constructor, on the control thread, once.
    dictStore merged;

    // "dictionary <target>", built once per rebind so the send after a join
    // is of a string the object already owns rather than a concatenation on
    // whichever thread the trigger arrived on.
    std::string reference;

    // Refusals, published for tests and diagnostics.
    std::atomic<std::uint64_t> dropped{0};
  };
} // namespace PATCHER
} // namespace YSE
