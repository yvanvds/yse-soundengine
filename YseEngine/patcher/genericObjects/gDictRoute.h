#pragma once
#include "../pObject.h"
#include "gDict.h"
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief Route a dictionary by the keys it holds — Max's ``dict.route``
     *         on the name-addressed value model ``.dict`` settled (issue
     *         #777).
     *
     *  The dispatcher for structured messages: a patch that receives
     *  dictionaries of several shapes sends each to the part of the graph
     *  that understands it, which is what ``.route`` does for list text and
     *  this object does for dictionaries. ``gRoute`` is the model, including
     *  its rightmost-outlet-is-the-reject rule; what is matched is the
     *  **presence of a top-level key** rather than a leading selector, and
     *  what leaves an outlet is the dictionary's **reference**, never its
     *  contents — the routing decision is about *which dictionary*, and the
     *  receiver still binds the name itself.
     *
     *  ### The arguments are the name, then the keys
     *
     *  A dictionary never travels down a cord — an ``OUT_TYPE`` carries a
     *  value, not an identity (see gDict.h for the whole argument) — so the
     *  dictionary is **bound from the first creation argument**, on the
     *  control thread: ``.dict.route <name> <key> [<key> ...]`` holds one
     *  ``shared_ptr<dictStore>`` resolved in ``SetParent`` / ``PARM_PARSE``
     *  / ``RefreshBinding``, exactly as the rest of the family resolves
     *  theirs. Every argument after the name is a key, and the outlet count
     *  comes from that list, ``gRoute``'s argument-driven shape: one outlet
     *  per key plus the rightmost reject.
     *
     *  A key argument is a **path**: usually one top-level segment
     *  (``voice``), but ``voice::1`` tests two levels down the same way,
     *  because presence is the family's sub-tree question — an entry *at*
     *  the path or *under* it (``<path>::...``), the bounded prefix scan
     *  gDict.h documents. An empty key token, or one past ``KEY_CAPACITY``
     *  (which no stored entry can ever spell), matches nothing; it costs an
     *  unreachable outlet and keeps the argument indices parallel to the
     *  outlets, ``gRoute``'s rule for an empty selector.
     *
     *  ### The shape, and the no-key case
     *
     *  ``.dict.route <name> a b`` has three outlets: ``a``, ``b``, and the
     *  reject. With **no key arguments** the object has the reject outlet
     *  and nothing else — ``.routepass``'s bare shape, not ``.route``'s
     *  default-0, because Max documents a default selector for ``route``
     *  and none for ``dict.route``, and inventing one would put a branch in
     *  a patch that did not ask for a branch.
     *
     *  ### What arrives, and what leaves
     *
     *  - **A bang routes**: the bound dictionary is tested for each key in
     *    argument order, and the reference ``dictionary <name>`` leaves the
     *    outlet of the **leftmost key present** — exactly one outlet fires
     *    per trigger, ``gRoute``'s rule, so a chain of ``.dict.route``
     *    objects strings together with each reject feeding the next inlet.
     *    A dictionary holding none of the keys leaves the rightmost outlet,
     *    reference intact, so the next stage tests the same dictionary the
     *    first one saw.
     *  - **``dictionary <name>`` routes too** — the message a ``.dict``'s
     *    reference outlet emits on a bang, so wiring that outlet here gives
     *    Max's own gesture: bang the dict, and it dispatches itself.
     *    Honoured only when it names the dictionary already bound —
     *    ``DictReferenceNames``, the bounded compare — and refused and
     *    counted otherwise, because resolving an unrecognised name means
     *    the registry's mutex on whatever thread the message arrived on.
     *    Re-pointing at a different name from a message is not portable,
     *    for the reason gDict.h gives for not porting ``refer``.
     *  - **An unnamed ``.dict.route`` is inert**: its private dictionary
     *    has no name to pass on (``gDict``'s rule for an unnamed
     *    reference), so a trigger routes nothing and sends nothing —
     *    silently, since the wiring is not an error, merely incomplete.
     *
     *  ### Real-time behaviour
     *
     *  ``Calculate()`` does nothing — the object is driven by its inlet,
     *  the family's rule. No message path allocates, locks or blocks: the
     *  name is resolved on the control thread, the decision is a bounded
     *  scan of at most ``dictStore::MAX_ENTRIES`` rows per key under the
     *  store's guard — **released before the send**, which runs the whole
     *  downstream subgraph and may well store into this same dictionary —
     *  and the reference is built once per rebind, so the send is of a
     *  string the object already owns. A trigger that loses the store's
     *  try-lock is refused whole and counted rather than made to wait.
     *
     *  A test-and-set guard is held across decision and send: the emitted
     *  reference is itself a trigger, so an outlet wired back into the
     *  inlet — directly or round a chain — would recurse without bound on
     *  whatever thread the trigger arrived on. The loser is refused and
     *  counted rather than made to spin, the family's guard for the
     *  family's reason.
     *
     *  ### What persists
     *
     *  The creation arguments, because they are creation arguments. The
     *  dictionary's contents persist with the ``.dict`` that owns them.
     */
    PATCHER_CLASS(gDictRoute, YSE::OBJ::G_DICT_ROUTE)
    _NO_MESSAGES
    _NO_CALCULATE

    _BANG_IN(BangIn)
    _LIST_IN(ListIn)

    _PARM_CLEAR
    _PARM_PARSE

    /** @brief Longest key path an argument can usefully spell, in
     *         characters — ``dictStore::KEY_CAPACITY``. A longer one can
     *         never be stored, so its outlet never fires. */
    static constexpr std::size_t KEY_CAPACITY = dictStore::KEY_CAPACITY;

    /** @brief The dictionary's shared name — the first creation argument,
     *         or empty for an inert, unnamed object. */
    const std::string& DictName() const {
      return dictName;
    }

    /** @brief The address the store is registered under —
     *         ``"<patcherName>.<name>"`` — or empty while it is private. */
    const std::string& Address() const {
      return boundAddress;
    }

    /** @brief How many key arguments — and so how many match outlets — the
     *         object built. The outlet count is one more: the reject. */
    std::size_t KeyCount() const {
      return keys.size();
    }

    /** @brief The key of match outlet @p index, or empty out of range.
     *         Diagnostics and tests; control thread only. */
    std::string KeyAt(std::size_t index) const;

    /** @brief The message a route emits — ``"dictionary <name>"``, or
     *         empty for an unnamed object, which has no name to pass on. */
    const std::string& Reference() const {
      return reference;
    }

    /** @brief Triggers that completed a routing decision and sent the
     *         reference. Diagnostics and tests. */
    std::uint64_t Routed() const {
      return routed.load(std::memory_order_relaxed);
    }

    /**
     *  @brief Messages refused so far — a reference naming a dictionary
     *         this object is not bound to, an unrecognised message, a lost
     *         try-lock, or a re-entrant trigger.
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

    // Re-bind after a patcher rename: the address prefix moved, so the
    // object now routes a different dictionary. Called from
    // patcherImplementation::SetName alongside gDict::RefreshBinding.
    void RefreshBinding();

  private:
    // Rebuild the outlets from the key arguments, docs included: one per
    // key plus the rightmost reject. Control thread only — the constructor
    // and the two parameter callbacks, all of which run before the object
    // is wired or published; a *live* SetParams never reaches here on a
    // published object, because registering the callbacks makes
    // ParamsNeedRebuild() true and #234 replaces the object. gRoute's rule.
    void ShapePorts();

    // Point the store at the current name and parent address. Control
    // thread only (SetParams / SetParent / SetName). A no-op when the
    // address has not changed.
    void Rebind();

    // Rebuild `reference` from the current name. Control thread only.
    void RefreshReference();

    // The routing itself: pick the leftmost key present under the store's
    // guard, release, send the reference out the matched outlet — or the
    // reject. Refuses (counted) on a lost guard or a re-entrant trigger;
    // inert (silent) while the object is unnamed.
    void Route(YSE::THREAD thread);

    void Refuse() {
      dropped.fetch_add(1, std::memory_order_relaxed);
    }

    // The shared name. First creation argument; empty means an inert,
    // unnamed object.
    std::string dictName;

    // The keys as typed — everything after the name. LIST parameter,
    // control thread only.
    std::vector<std::string> keyArgs;

    // The keys, parallel to the match outlets — every token kept, empty
    // ones included, so an index into the arguments is an index into
    // `outputs`. Built by ShapePorts and never resized by a message
    // handler; a message path only ever reads the characters.
    std::vector<std::string> keys;

    // The address `store` is registered under, or empty while it is
    // private. Also the "has the binding changed?" key Rebind() compares
    // against.
    std::string boundAddress;

    // The dictionary. Never null once the object has been constructed, so
    // no message handler needs a null check.
    std::shared_ptr<dictStore> store;

    // "dictionary <name>", built once per rebind so a route is a send of a
    // string the object already owns rather than a concatenation on
    // whichever thread the trigger arrived on.
    std::string reference;

    // The re-entrancy guard, held across decision and send: the emitted
    // reference is itself a trigger, so an outlet wired back into the
    // inlet would recurse without bound. The loser is dropped and counted
    // rather than made to spin — the family's guard, for the family's
    // reason.
    std::atomic<bool> busy{false};

    std::atomic<std::uint64_t> routed{0};
    std::atomic<std::uint64_t> dropped{0};
  };
} // namespace PATCHER
} // namespace YSE
