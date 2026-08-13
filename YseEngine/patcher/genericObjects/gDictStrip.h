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
     *  @brief Remove a dictionary's entries under a key path — Max's
     *         ``dict.strip`` on the name-addressed value model ``.dict``
     *         settled (issue #780).
     *
     *  Dropping a sub-tree **in place**: clearing one voice from a voice
     *  table, removing a section of a preset before saving it. This is the
     *  operation a patch cannot spell any other way — ``.dict``'s ``delete``
     *  removes one path, not a branch, and ``.dict.slice`` partitions into
     *  *other* dictionaries where this object edits the one it is bound to.
     *  A sub-tree is not a value in the flat store (see the nesting note in
     *  gDict.h), so removing one is a real operation: a bounded scan of every
     *  path that begins with the strip prefix, erased where it stands.
     *
     *  A dictionary never travels down a cord — an ``OUT_TYPE`` carries a
     *  value, not an identity (see gDict.h for the whole argument) — so the
     *  dictionary is **bound from the creation argument**, on the control
     *  thread: ``.dict.strip <name> [<path>]`` holds one
     *  ``shared_ptr<dictStore>`` resolved in ``SetParent`` / ``PARM_PARSE``
     *  / ``RefreshBinding``, exactly as the rest of the family resolves
     *  theirs.
     *
     *  ### What the strip removes
     *
     *  ``gDictSlice::UnderPath``'s rule, verbatim, because the two objects
     *  are the two halves of one partition — what ``.dict.slice`` puts in
     *  its remainder is exactly what a strip of the same path leaves behind:
     *
     *  - **An entry under the path** — its key begins ``<path>::`` with at
     *    least one character after the separator — is erased. Stripping
     *    ``voice::1`` removes ``voice::1::freq`` and ``voice::1::adsr``.
     *  - **Every other entry survives, untouched** — including an entry
     *    stored *at* exactly the path, which is a leaf value, not a
     *    sub-tree. And a key that only *begins like* the path (``voices``
     *    against ``voice``) is no match at all: the prefix ends at a ``::``
     *    boundary or it is not a prefix, so stripping ``voice`` never
     *    removes ``voices::1``.
     *  - **An empty path matches nothing** — there is no sub-tree above the
     *    root — so a ``.dict.strip`` with no path argument strips nothing.
     *    Clearing a whole dictionary is ``.dict``'s ``clear``, not a strip.
     *
     *  ### What arrives, and what leaves
     *
     *  - **A bang on inlet 0 strips**, then the dictionary's reference
     *    ``dictionary <name>`` leaves the outlet, so the rest of the
     *    ``dict.*`` family can pick the edited dictionary up — the family's
     *    chaining gesture. Silent for an unnamed dictionary, which has no
     *    name to pass on (the strip still ran, on the private store).
     *  - **``dictionary <name>`` on inlet 0 also strips** — the message a
     *    ``.dict``'s reference outlet emits on a bang, so wiring that outlet
     *    here gives Max's own gesture. Honoured only when it names the
     *    dictionary already bound — ``DictReferenceNames``, the bounded
     *    compare — and refused and counted otherwise, because resolving an
     *    unrecognised name means the registry's mutex on whatever thread the
     *    message arrived on. Re-pointing at a different name from a message
     *    is not portable, for the reason gDict.h gives for not porting
     *    ``refer``.
     *
     *  ### How the erase stays inside one guard
     *
     *  One dictionary means one guard, so the two-guard problem
     *  ``.dict.slice`` solves with its snapshot does not arise: the strip
     *  takes the store's try-lock once, walks the table **back to front**
     *  erasing every entry under the path — back to front because
     *  ``DictEraseAt`` closes the gap by moving the rows *above* the erased
     *  position down, so a forward cursor would step over the row that just
     *  slid into its place — releases, and only then sends. A guard another
     *  thread holds refuses the whole strip, counted, rather than waiting,
     *  so the dictionary is never left half-stripped and the reference never
     *  announces an edit that did not happen. Erasing can only shrink the
     *  table, so there is no capacity to refuse over.
     *
     *  ### Real-time behaviour
     *
     *  ``Calculate()`` does nothing — the object is driven by its inlet, the
     *  rule ``.value``, ``.coll`` and ``.dict`` establish. No message path
     *  allocates, locks or blocks: the name is resolved on the control
     *  thread, the prefix match is a bounded compare into keys the store
     *  already owns, ``DictEraseAt`` moves rows by ``assign`` into storage
     *  they already own, and the reference is built once per rebind so the
     *  send is of a string the object already owns.
     */
    PATCHER_CLASS(gDictStrip, YSE::OBJ::G_DICT_STRIP)
    _NO_MESSAGES
    _NO_CALCULATE

    _BANG_IN(BangIn)
    _LIST_IN(ListIn)

    _PARM_CLEAR
    _PARM_PARSE

    /** @brief The dictionary's shared name — the first creation argument, or
     *         empty for a private (empty) dictionary. */
    const std::string& DictName() const {
      return dictName;
    }

    /** @brief The key path whose sub-tree a strip removes — the second
     *         creation argument, or empty to strip nothing. */
    const std::string& StripPath() const {
      return stripPath;
    }

    /** @brief The address the store is registered under —
     *         ``"<patcherName>.<name>"`` — or empty while it is private. */
    const std::string& Address() const {
      return boundAddress;
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

    // Bind the store the moment the patcher name is known, so a message
    // never resolves a name. gDict::SetParent's rule.
    void SetParent(pObject* parent) override;

    // Re-bind after a patcher rename: the address prefix moved, so the
    // object now strips a different dictionary. Called from
    // patcherImplementation::SetName alongside gDict::RefreshBinding.
    void RefreshBinding();

  private:
    // Point the store at the current name and parent address. Control thread
    // only (SetParams / SetParent / SetName). A no-op when the address has
    // not changed.
    void Rebind();

    // Rebuild `reference` from the current name. Control thread only.
    void RefreshReference();

    // The strip itself: under the store's guard, erase every entry under the
    // path — back to front, so DictEraseAt's gap-closing never moves a row
    // past the cursor — then send the reference after the guard is released.
    // A lost guard refuses the whole strip (counted) and sends nothing.
    void Strip(YSE::THREAD thread);

    // Whether one entry is under the path: its key begins "<path>::" with at
    // least one character after the separator. gDictSlice::UnderPath's rule,
    // because a strip removes exactly what a slice of the same path takes.
    // Bounded compares into strings the store already owns — no allocation,
    // so it is safe on whichever thread the trigger arrived on.
    bool UnderPath(const dictEntry& entry) const;

    void Refuse() {
      dropped.fetch_add(1, std::memory_order_relaxed);
    }

    // The shared name. First creation argument; empty means a private,
    // empty dictionary.
    std::string dictName;

    // The key path whose sub-tree a strip removes. Second creation argument;
    // empty strips nothing.
    std::string stripPath;

    // The address `store` is registered under, or empty while it is private.
    // Also the "has the binding changed?" key Rebind() compares against.
    std::string boundAddress;

    // The dictionary. Never null once the object has been constructed, so no
    // message handler needs a null check.
    std::shared_ptr<dictStore> store;

    // "dictionary <name>", built once per rebind so the send after a strip
    // is of a string the object already owns rather than a concatenation on
    // whichever thread the trigger arrived on.
    std::string reference;

    // Refusals, published for tests and diagnostics.
    std::atomic<std::uint64_t> dropped{0};
  };
} // namespace PATCHER
} // namespace YSE
