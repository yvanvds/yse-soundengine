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
     *  @brief Output a dictionary's key/value pairs one at a time — Max's
     *         ``dict.iter`` on the name-addressed value model ``.dict``
     *         settled (issue #773).
     *
     *  Max's summary is "Stream the content of a dictionary": every key/value
     *  pair leaves as an ordinary list message, one per entry. This is the
     *  ``.uzi`` / ``.iter`` shape applied to structured data — the object
     *  every "do this for each entry" patch is built from, and without it a
     *  dictionary can only be read at paths the patch already knows the names
     *  of.
     *
     *  A dictionary never travels down a cord — an ``OUT_TYPE`` carries a
     *  value, not an identity (see gDict.h for the whole argument) — so the
     *  dictionary is **bound from the creation argument**, on the control
     *  thread: ``.dict.iter <name>`` holds one ``shared_ptr<dictStore>``
     *  resolved in ``SetParent`` / ``PARM_PARSE`` / ``RefreshBinding``,
     *  exactly as ``gDict``, ``gDictCompare`` and ``gDictGroup`` resolve
     *  theirs.
     *
     *  ### What arrives, and what leaves
     *
     *  - **A bang on the inlet walks**: one list ``<path> <value...>`` per
     *    entry out the pair outlet, in storage order — the order paths were
     *    first written, the same order ``getkeys`` and a JSON dump report —
     *    and, after the last pair, one bang out the done outlet. An entry
     *    holding nothing (``set <path>`` stored no value) leaves as its path
     *    alone. Paths are whole ``::`` paths — ``voice::1::freq`` — because
     *    nesting lives in the key, so what leaves here feeds ``.route``,
     *    ``.zl`` and a ``set``-building ``.prepend`` unchanged.
     *  - **``dictionary <name>`` on the inlet also walks** — the message a
     *    ``.dict``'s reference outlet emits on a bang, so wiring the
     *    dictionary's reference outlet here gives Max's own gesture: bang the
     *    dict, out stream the pairs. Honoured only when it names the
     *    dictionary already bound — ``DictReferenceNames``, the bounded
     *    compare — and refused and counted otherwise, because resolving an
     *    unrecognised name means the registry's mutex on whatever thread the
     *    message arrived on. Re-pointing at a different name from a message
     *    is not portable, for the reason gDict.h gives for not porting
     *    ``refer``.
     *
     *  ### The done bang, and the outlet order
     *
     *  The pair outlet is the left outlet and the done outlet the right one,
     *  and the done bang leaves **last** — the deliberate exception to the
     *  family's right-to-left rule, exactly as Max states it for ``.uzi``'s
     *  carry: it means "all the pairs have been sent", so it cannot precede
     *  them. It fires even when the dictionary is empty — ``.uzi``'s rule for
     *  a count of zero: a loop that does not run is not an error, and the
     *  "and afterwards, do this" branch must not be silently skipped. A
     *  *refused* walk (a lost guard, a re-entrant trigger) emits no done
     *  bang: nothing was walked, and a done that fired anyway would say
     *  something that did not happen.
     *
     *  ### The walk is snapshotted — the decision issue #773 asks for
     *
     *  Each pair send runs the whole downstream subgraph before the next pair
     *  leaves, and that subgraph may well ``set`` or ``delete`` into this
     *  very dictionary — the store cannot be guarded across an outlet send
     *  (see ``dictStoreGuard``), so entries could move under a cursor walking
     *  the live table. Of the three options the issue names — restart, skip,
     *  snapshot — the walk is **snapshotted**: the trigger copies the
     *  dictionary out under its guard into a snapshot the object
     *  pre-allocated at construction, releases, and walks the snapshot. A
     *  mutation arriving mid-walk — from the pairs' own subgraph or from
     *  another thread — lands in the store and changes nothing about the walk
     *  in flight: every entry the dictionary held at the trigger is emitted
     *  exactly once, none is skipped by a renumbering ``delete`` and none is
     *  visited twice. That is the only one of the three with an output a
     *  patch can reason about, and it costs nothing the family was not
     *  already paying — ``gDictCompare`` and ``gDictGroup`` take the same
     *  snapshot for the same reason.
     *
     *  The snapshot is also what gives the object **its own cursor**: two
     *  ``.dict.iter`` on one name walk independently — ``.coll``'s
     *  per-object pointer rule — because each walks its own snapshot, and
     *  nothing about a walk is kept in the shared store.
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
     *  One trigger becomes as many list sends as the dictionary holds
     *  entries, plus the done bang, all inside the call frame of the one
     *  ``inlet::Set*`` that started it — at most ``dictStore::MAX_ENTRIES``
     *  (256) subgraph traversals per stimulus. That is a bound on the *work*,
     *  not a promise the work fits an audio block, and the number worth
     *  knowing beside it is ``.iter``'s: the patcher's value-command queue
     *  (#225) is 256 deep, so a full dictionary streamed into a ``.s`` from
     *  the control thread can saturate it in one burst and hit its documented
     *  drop-and-log backpressure.
     *
     *  ### Real-time behaviour
     *
     *  ``Calculate()`` does nothing — the object is driven by its inlet, the
     *  rule ``.value``, ``.coll`` and ``.dict`` establish. No message path
     *  allocates, locks or blocks: the name is resolved on the control
     *  thread, the snapshot rows are allocated whole by ``dictStore``'s own
     *  constructor, and each pair is composed by bounded appends into a
     *  buffer reserved at construction before it is sent.
     */
    PATCHER_CLASS(gDictIter, YSE::OBJ::G_DICT_ITER)
    _NO_MESSAGES
    _NO_CALCULATE

    _BANG_IN(BangIn)
    _LIST_IN(ListIn)

    _PARM_CLEAR
    _PARM_PARSE

    /**
     *  @brief Most pairs one trigger can emit — ``dictStore::MAX_ENTRIES``.
     *
     *  The documented message budget: one trigger costs one list send per
     *  entry plus the done bang, each running its whole subgraph, on
     *  whichever thread the trigger arrived on.
     */
    static constexpr std::size_t MAX_ENTRIES = dictStore::MAX_ENTRIES;

    /** @brief The dictionary's shared name — the creation argument, or empty
     *         for a private (empty) dictionary. */
    const std::string& DictName() const {
      return dictName;
    }

    /** @brief The address the store is registered under —
     *         ``"<patcherName>.<name>"`` — or empty while it is private. */
    const std::string& Address() const {
      return boundAddress;
    }

    /**
     *  @brief Messages refused so far — an unrecognised message, a reference
     *         naming a dictionary this object is not bound to, a lost
     *         try-lock, or a trigger arriving mid-walk.
     *
     *  A counter rather than a log line because the refusing thread may be
     *  the audio callback; ``gDict::Dropped``, for ``gDict``'s reason.
     */
    std::uint64_t Dropped() const {
      return dropped.load(std::memory_order_relaxed);
    }

    // Bind the store the moment the patcher name is known, so a message
    // never resolves a name. gDict::SetParent's rule.
    void SetParent(pObject* parent) override;

    // Re-bind after a patcher rename: the address prefix moved, so the object
    // now walks a different dictionary. Called from
    // patcherImplementation::SetName alongside gDict::RefreshBinding.
    void RefreshBinding();

  private:
    // Point the store at the current name and parent address. Control thread
    // only (SetParams / SetParent / SetName). A no-op when the address has
    // not changed.
    void Rebind();

    // The walk itself: snapshot the store under its guard, emit one pair per
    // snapshot entry with no guard held, bang the done outlet after the last.
    // Refuses (counted) on a lost guard or a re-entrant trigger — see the
    // class notes on both.
    void Walk(YSE::THREAD thread);

    void Refuse() {
      dropped.fetch_add(1, std::memory_order_relaxed);
    }

    // The shared name. The creation argument; empty means a private, empty
    // dictionary.
    std::string dictName;

    // The address the store is registered under, or empty while it is
    // private. Also the "has the binding changed?" key Rebind() compares
    // against.
    std::string boundAddress;

    // The dictionary. Never null once the object has been constructed, so no
    // message handler needs a null check.
    std::shared_ptr<dictStore> store;

    // What the walk actually walks: the dictionary as it stood at the
    // trigger, copied out under the store's guard so no mutation from the
    // pairs' own subgraph can move entries under the cursor. Allocated whole
    // by dictStore's own constructor, on the control thread, once.
    dictStore snapshot;

    // Where a pair is composed — "<path> <value...>" — before it is sent.
    // Reserved at construction to the widest pair a store can hold, so the
    // walk's appends never allocate.
    std::string emit;

    // The re-entrancy guard, held across the whole walk: a trigger looping
    // back from either outlet, or arriving from another thread mid-walk,
    // would rewrite the snapshot being walked. The loser is dropped and
    // counted rather than made to spin — .iter's guard, for .iter's reason.
    std::atomic<bool> busy{false};

    // Refusals, published for tests and diagnostics.
    std::atomic<std::uint64_t> dropped{0};
  };
} // namespace PATCHER
} // namespace YSE
