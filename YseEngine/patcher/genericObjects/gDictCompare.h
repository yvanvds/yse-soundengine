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
     *  @brief Compare two dictionaries — Max's ``dict.compare`` on the
     *         name-addressed value model ``.dict`` settled (issue #770).
     *
     *  Max's object takes a dictionary on each inlet and reports whether the
     *  two are identical. Here a dictionary never travels down a cord — an
     *  ``OUT_TYPE`` carries a value, not an identity (see gDict.h for the whole
     *  argument) — so both dictionaries are **bound from the creation
     *  arguments**, on the control thread: ``.dict.compare <left> <right>``
     *  holds two ``shared_ptr<dictStore>`` resolved in ``SetParent`` /
     *  ``PARM_PARSE`` / ``RefreshBinding``, exactly as ``gDict`` resolves its
     *  one.
     *
     *  ### What arrives, and what leaves
     *
     *  - **A bang on inlet 0 compares** and sends the verdict — 1 when the two
     *    dictionaries hold the same entries, 0 when they do not — out the int
     *    outlet. This is the "did anything change?" poll the object exists
     *    for.
     *  - **``dictionary <left>`` on inlet 0 also compares.** That is the
     *    message a ``.dict``'s reference outlet emits on a bang, so wiring the
     *    left dictionary's reference outlet here gives Max's own gesture: bang
     *    the dict, out comes the comparison. Honoured only when it names the
     *    dictionary already bound — ``DictReferenceNames``, the bounded
     *    compare — and refused and counted otherwise, because resolving an
     *    unrecognised name means the registry's mutex on whatever thread the
     *    message arrived on.
     *  - **``dictionary <right>`` on inlet 1 is acknowledged and nothing
     *    more.** Max's right inlet *sets* the dictionary to compare against;
     *    here that binding is the second creation argument, so the reference
     *    is accepted silently when it names it — a patch may wire both
     *    reference outlets across, as it would in Max — and refused and
     *    counted when it names anything else. Re-pointing at a different name
     *    from a message is not portable, for the reason gDict.h gives for not
     *    porting ``refer``.
     *
     *  ### What "identical" means
     *
     *  The same entries, order-insensitively: the same number of them, and
     *  every key path of the left dictionary present in the right with
     *  byte-identical value text. Storage order is deliberately no part of it —
     *  a dictionary is a mapping, and two patches that stored the same pairs in
     *  a different order have the same dictionary. Values are compared as the
     *  list text they are stored as, so ``120`` and ``120.`` differ — they are
     *  a different atom downstream, which is Max's reading too.
     *
     *  ### How the comparison avoids holding two guards
     *
     *  Both stores are guarded by ``.value``-style try-locks, and taking both
     *  at once would put a lock-ordering obligation on every pair of objects
     *  that name the same two dictionaries. So the comparison never holds two:
     *  it copies the left dictionary out under its guard into a snapshot the
     *  object pre-allocated at construction, releases, then compares the
     *  snapshot against the right dictionary under that store's guard alone —
     *  bounded ``assign``s into pre-reserved rows, no allocation, and the
     *  degenerate case of both names binding one store compares it against
     *  itself without ever taking its guard twice. A guard another thread
     *  holds refuses the whole comparison, counted, rather than waiting.
     *
     *  ### Real-time behaviour
     *
     *  ``Calculate()`` does nothing — the object is driven by its inlets, the
     *  rule ``.value``, ``.coll`` and ``.dict`` establish. No message path
     *  allocates, locks or blocks: the names are resolved on the control
     *  thread, the snapshot rows are reserved at construction, and the verdict
     *  is an int sent after every guard is released.
     */
    PATCHER_CLASS(gDictCompare, YSE::OBJ::G_DICT_COMPARE)
    _NO_MESSAGES
    _NO_CALCULATE

    _BANG_IN(BangIn)
    _LIST_IN(ListIn)

    _PARM_CLEAR
    _PARM_PARSE

    /** @brief The left dictionary's shared name — the first creation
     *         argument, or empty for a private (empty) side. */
    const std::string& LeftName() const {
      return leftName;
    }

    /** @brief The right dictionary's shared name — the second creation
     *         argument. */
    const std::string& RightName() const {
      return rightName;
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

    /**
     *  @brief Messages refused so far — an unrecognised message, a reference
     *         naming a dictionary this object is not bound to, or a lost
     *         try-lock.
     *
     *  A counter rather than a log line because the refusing thread may be
     *  the audio callback; ``gDict::Dropped``, for ``gDict``'s reason.
     */
    std::uint64_t Dropped() const {
      return dropped.load(std::memory_order_relaxed);
    }

    // Bind both stores the moment the patcher name is known, so a message
    // never resolves a name. gDict::SetParent's rule.
    void SetParent(pObject* parent) override;

    // Re-bind after a patcher rename: the address prefix moved, so the object
    // now compares different dictionaries. Called from
    // patcherImplementation::SetName alongside gDict::RefreshBinding.
    void RefreshBinding();

  private:
    // Point both stores at the current names and parent address. Control
    // thread only (SetParams / SetParent / SetName). A no-op per side when
    // that side's address has not changed.
    void Rebind();

    // The comparison itself: snapshot the left store under its guard, compare
    // the snapshot against the right store under that guard alone, send the
    // verdict after both are released. Refuses (counted) on a lost guard.
    void Compare(YSE::THREAD thread);

    void Refuse() {
      dropped.fetch_add(1, std::memory_order_relaxed);
    }

    // The shared names. First and second creation arguments; an empty name
    // means that side is a private, empty dictionary.
    std::string leftName;
    std::string rightName;

    // The addresses the stores are registered under, or empty while private.
    // Also the "has the binding changed?" keys Rebind() compares against.
    std::string boundLeftAddress;
    std::string boundRightAddress;

    // The two dictionaries. Never null once the object has been constructed,
    // so no message handler needs a null check.
    std::shared_ptr<dictStore> leftStore;
    std::shared_ptr<dictStore> rightStore;

    // Where the left dictionary is copied to while its guard is held, so the
    // right store's guard is never nested inside it. Allocated whole by
    // dictStore's own constructor, on the control thread, once.
    dictStore snapshot;

    // Refusals, published for tests and diagnostics.
    std::atomic<std::uint64_t> dropped{0};
  };
} // namespace PATCHER
} // namespace YSE
