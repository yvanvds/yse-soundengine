#pragma once
#include "../io/fileScheduler.h"
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
     *  @brief Serialise a dictionary to text — Max's ``dict.serialize`` on the
     *         name-addressed value model ``.dict`` settled (issue #778).
     *
     *  Max's summary is "convert a dictionary into a single symbol". This is
     *  the write half of the interchange pair whose read half is
     *  ``.dict.deserialize`` (#771): a dictionary becomes a JSON string a
     *  host, a ``.textedit``, a ``.s``/``.r`` pair or the live-coding DSL can
     *  carry — what makes a dictionary the patcher's interchange format
     *  rather than only its store.
     *
     *  ### What arrives, and what leaves
     *
     *  A dictionary never travels down a cord — an ``OUT_TYPE`` carries a
     *  value, not an identity (see gDict.h for the whole argument) — so the
     *  dictionary is **bound from the creation argument**, on the control
     *  thread: ``.dict.serialize <name>`` holds one ``shared_ptr<dictStore>``
     *  resolved in ``SetParent`` / ``PARM_PARSE`` / ``RefreshBinding``,
     *  exactly as the rest of the family resolves theirs.
     *
     *  - **A bang serialises**: the whole dictionary leaves the outlet as one
     *    list message holding a **single-line compact JSON object** — ``::``
     *    paths expanded into real nesting, members in storage order of first
     *    appearance, no whitespace outside strings. The same document
     *    ``DictToJson`` builds and a saved patch holds, spelled the way
     *    ``nlohmann::json::dump()`` spells it, so what leaves here parses
     *    anywhere JSON parses — and ``.dict.deserialize`` reads back exactly
     *    this.
     *  - **``dictionary <name>`` serialises too** — the message a ``.dict``'s
     *    reference outlet emits on a bang, so wiring that outlet here gives
     *    Max's own gesture. Honoured only when it names the dictionary
     *    already bound — ``DictReferenceNames``, the bounded compare — and
     *    refused and counted otherwise, because resolving an unrecognised
     *    name means the registry's mutex on whatever thread the message
     *    arrived on. Re-pointing at a different name from a message is not
     *    portable, for the reason gDict.h gives for not porting ``refer``.
     *
     *  ### The document, precisely
     *
     *  The format is a contract — ``.dict.deserialize`` parses exactly this —
     *  so it is worth spelling out. One JSON object, one line, no whitespace
     *  outside strings. Nesting is real: ``voice::1::freq`` appears as
     *  ``{"voice":{"1":{"freq":...}}}``. Members appear in storage order of
     *  first appearance; an entry whose path collides with an established
     *  leaf or branch loses to the earlier-stored one (``DictToJson``'s rule,
     *  which is ``.coll``'s), and an entry with an empty path segment is
     *  skipped. Values are typed by the patcher's own classifier, shared with
     *  ``DictToJson`` and ``.dict.print``: a single numeric token is a JSON
     *  number (an integer as its own digits, a float by the patcher's
     *  formatter), a multi-token value is an array of those, an empty value
     *  is ``""``, and anything else is an escaped JSON string. An empty
     *  dictionary is ``{}``.
     *
     *  ### The outlet route, and the bound — the decision #778 asks for
     *
     *  The issue offers two deliveries — an outlet emission bounded by what a
     *  list payload can carry, or a ``write <file>`` route for large
     *  dictionaries — and this object delivers the **outlet**: it is Max's
     *  own ``dict.serialize`` (whose file half lives on ``dict`` itself as
     *  ``write``), and it is the half ``.dict.deserialize`` pairs with down a
     *  cord. The consequence is a bound: the patcher's value queue carries a
     *  list payload of at most ``kValueListCap - 1`` characters
     *  (``DOCUMENT_CAPACITY``), so a document past it could reach a direct
     *  neighbour but silently vanish crossing a ``.s``/``.r`` — the same
     *  message, deliverable or not depending on wiring. A document longer
     *  than ``DOCUMENT_CAPACITY`` is therefore **refused whole and counted**
     *  (``Dropped``), never truncated: a truncated document is not a shorter
     *  document, it is not a document at all — or worse, it parses as a
     *  different one. Refusal-not-truncation is the family's rule, and here
     *  it is also the format's.
     *
     *  ### ``write <file>`` — the file route (issue #840)
     *
     *  The route that bound points at: a ``write <file>`` message snapshots
     *  the dictionary, composes the same document the outlet would carry,
     *  and hands it to the patcher's ``fileScheduler`` (#683) instead of the
     *  outlet — ``RequestWrite`` copies the bytes into a patcher-owned slot
     *  before returning, the disk work runs on the background pool, and the
     *  requesting object is never touched again. The pair's file half lives
     *  here rather than on ``.dict`` (where Max keeps ``write``) because the
     *  read half #840 installs lives on ``.dict.deserialize``, and because
     *  this object already owns the one bounded emitter the document needs —
     *  a ``write`` on ``.dict`` would mean a second copy of it. The file
     *  route's bound is the scheduler slot's, not the transport's: the
     *  compose buffer is sized so any document a slot can hold
     *  (``fileScheduler::BYTES_CAPACITY``, 128 KiB) composes uncut, and a
     *  document past it is refused whole and counted — refusal, never
     *  truncation, at the slot bound too. A bare ``write`` reuses the last
     *  path given (``.textfile``'s rule; there is no dialog to ask with) and
     *  is refused, counted, when no path has ever been given — as is a
     *  ``write`` on a standalone object, which has no patcher and so no
     *  plumbing. Max has no outlet for a finished write and neither does
     *  this: a write reports by having happened.
     *
     *  ### Real-time behaviour
     *
     *  ``Calculate()`` does nothing — the object is driven by its inlet, the
     *  family's rule. No message path allocates, locks or blocks:
     *
     *  - **The document is snapshotted.** The store cannot be held across the
     *    outlet send — the guard is a try-lock other writers would then lose,
     *    and the send runs the whole downstream subgraph (see
     *    ``dictStoreGuard``) — so the trigger copies the dictionary out under
     *    its guard into a snapshot pre-allocated at construction, releases,
     *    and composes from the snapshot. ``gDictIter``'s move, for
     *    ``gDictIter``'s reason, and it makes the document atomic: what
     *    leaves is the dictionary as it stood at the trigger.
     *  - **The JSON is composed by the bounded emitter shape ``.dict.print``
     *    established** — bounded prefix scans over the snapshot's flat ``::``
     *    paths (the sub-tree walk gDict.h documents), values spelled by the
     *    shared ``DictAppendValueJson`` classifier — because ``DictToJson``
     *    builds an ``nlohmann::json`` tree, which allocates by construction
     *    and is control-thread only. The doctest suite holds this emitter in
     *    lockstep with ``DictToJson`` by parsing what the object emitted and
     *    comparing it against ``DictToJson``'s document for the same store.
     *  - **The send is one ``SendList``** of a string reserved at
     *    construction, after composition, with no guard held.
     *
     *  A single test-and-set guard is held across snapshot, composition and
     *  send: a trigger looping back from the outlet — or arriving from
     *  another thread mid-composition — would rewrite the snapshot and the
     *  compose buffer under themselves, so the loser is refused and counted
     *  rather than made to spin, the family's guard for the family's reason.
     *
     *  ### The message budget
     *
     *  One trigger is one send running its whole subgraph, composed by a
     *  bounded walk of at most ``dictStore::MAX_ENTRIES`` rows per row — the
     *  family's documented cost for wanting a tree out of a flat table.
     *
     *  ### What persists
     *
     *  The creation argument, because it is a creation argument. The
     *  dictionary's contents persist with the ``.dict`` that owns them.
     */
    PATCHER_CLASS(gDictSerialize, YSE::OBJ::G_DICT_SERIALIZE)
    _NO_MESSAGES
    _NO_CALCULATE

    _BANG_IN(BangIn)
    _LIST_IN(ListIn)

    _PARM_CLEAR
    _PARM_PARSE

    /** @brief Most entries a document can hold — ``dictStore::MAX_ENTRIES``. */
    static constexpr std::size_t MAX_ENTRIES = dictStore::MAX_ENTRIES;

    /**
     *  @brief Longest document the outlet emits, in characters — 255,
     *         ``patcherImplementation::kValueListCap - 1``, the longest list
     *         payload the patcher's value queue carries inline.
     *
     *  A finished document longer than this is refused whole and counted,
     *  never truncated or sent anyway: a longer send could reach a direct
     *  neighbour but silently vanish crossing a ``.s``/``.r``, and a message
     *  that is deliverable or not depending on wiring is a trap. See the
     *  class notes for the whole decision.
     */
    static constexpr std::size_t DOCUMENT_CAPACITY = 255;

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
     *  @brief Messages refused so far — a reference naming a dictionary this
     *         object is not bound to, an unrecognised message, a lost
     *         try-lock, a re-entrant trigger, a finished document past
     *         ``DOCUMENT_CAPACITY``, and the write route's refusals (#840):
     *         no plumbing, no path, a document past the scheduler slot's
     *         bound, or a full scheduler table.
     *
     *  A counter rather than a log line because the refusing thread may be
     *  the audio callback; ``gDict::Dropped``, for ``gDict``'s reason.
     */
    std::uint64_t Dropped() const {
      return dropped.load(std::memory_order_relaxed);
    }

    /** @brief Documents this object has sent. Diagnostics and tests. */
    std::uint64_t Emitted() const {
      return emitted.load(std::memory_order_relaxed);
    }

    // Bind the store the moment the patcher name is known, so a message
    // never resolves a name. gDict::SetParent's rule.
    void SetParent(pObject* parent) override;

    // Re-bind after a patcher rename: the address prefix moved, so the object
    // now serialises a different dictionary. Called from
    // patcherImplementation::SetName alongside gDict::RefreshBinding.
    void RefreshBinding();

  private:
    // Point the store at the current name and parent address. Control thread
    // only (SetParams / SetParent / SetName). A no-op when the address has
    // not changed.
    void Rebind();

    // The serialisation itself: snapshot the store under its guard, compose
    // the compact document from the snapshot, send it whole — or refuse
    // (counted) on a lost guard, a re-entrant trigger, or a document past
    // DOCUMENT_CAPACITY.
    void Serialize(YSE::THREAD thread);

    // A `write [file]` message (issue #840): snapshot and compose exactly as
    // Serialize does, then hand the document to the patcher's fileScheduler
    // instead of the outlet — RequestWrite copies the bytes into its slot
    // before returning, so the compose buffer is free again the moment this
    // returns. Refused and counted when there is no plumbing (a standalone
    // object), no path (none given and none remembered), a lost guard, a
    // document the compose buffer cut (past the scheduler slot's bound), or
    // a full scheduler table.
    void WriteFile(const char* name, std::size_t length);

    // The shared middle of Serialize and WriteFile: snapshot the store under
    // its guard, compose the compact document into `compose`. Called with
    // `busy` held. False — the caller refuses — on a lost store guard or a
    // document the buffer cut.
    bool ComposeDocument();

    // One level of the nested document: every unconsumed snapshot entry under
    // the current `path` prefix, grouped by its next path segment, leaves and
    // sub-objects alike, comma-separated in emission order. Recursion is
    // bounded by the deepest path a KEY_CAPACITY key can spell. RT-safe:
    // bounded scans and appends only.
    void EmitLevel(std::size_t prefixLength);

    // One bounded structural append — "{", ",", ":" — clearing `fit` when it
    // does not fit, so a cut document is refused rather than sent broken.
    void Append(const char* text, std::size_t length);

    void Refuse() {
      dropped.fetch_add(1, std::memory_order_relaxed);
    }

    // The tag a write request carries — this object makes only one kind of
    // request, but the recipe's rule is to tag anyway.
    static constexpr int FILE_TAG_WRITE = 0;

    // Widest document the emitter composes before the delivery bounds are
    // checked: wide enough that any document either route could accept — the
    // outlet's DOCUMENT_CAPACITY, or the file route's full scheduler slot
    // (issue #840) — has certainly not been cut by the buffer first. Two
    // over BYTES_CAPACITY so Append's headroom check still admits a document
    // of exactly the slot's size. Heap-held (see `compose`): at 128 KiB it
    // is a file-sized buffer, not a member array.
    static constexpr std::size_t COMPOSE_CAPACITY = fileScheduler::BYTES_CAPACITY + 2;

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

    // What the emitter actually reads: the dictionary as it stood at the
    // trigger, copied out under the store's guard so the document describes
    // one consistent dictionary however the store moves afterwards. Allocated
    // whole by dictStore's own constructor, on the control thread, once.
    dictStore snapshot;

    // Which snapshot entries the walk has already spoken for — emitted,
    // skipped as invalid, or lost a collision. Trigger-local state, but a
    // member so the message path owns no stack array of MAX_ENTRIES.
    bool consumed[dictStore::MAX_ENTRIES] = {};

    // The "::" path prefix of the level being emitted, built as the walk
    // descends. One key plus one separator of headroom.
    char prefix[dictStore::KEY_CAPACITY + 4] = {};

    // The document being composed, and whether every append fit. `fit` is
    // plain rather than atomic because it is only touched under `busy`.
    // Allocated whole at construction (control thread) and held by pointer:
    // COMPOSE_CAPACITY is a fileScheduler slot's worth of document (#840),
    // which is no size for an inline member array.
    std::unique_ptr<char[]> compose;
    std::size_t composeLength = 0;
    bool fit = true;

    // The last path a `write` named — what a bare `write` reuses, there
    // being no dialog to ask with (issue #840). Reserved to PATH_CAPACITY at
    // construction and touched only under `busy`, so remembering a name on
    // the message path is a bounded assign into storage that already exists.
    std::string writePath;

    // The finished document, copied out of `compose` for the send. Reserved
    // at construction to DOCUMENT_CAPACITY, so the assign never allocates.
    std::string emit;

    // The re-entrancy guard, held across snapshot, composition and send: a
    // trigger looping back from the outlet would rewrite the buffers under
    // themselves. The loser is dropped and counted rather than made to spin —
    // the family's guard, for the family's reason.
    std::atomic<bool> busy{false};

    std::atomic<std::uint64_t> dropped{0};
    std::atomic<std::uint64_t> emitted{0};
  };
} // namespace PATCHER
} // namespace YSE
