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
     *  @brief Print a dictionary's contents to the engine log — Max's
     *         ``dict.print`` on the name-addressed value model ``.dict``
     *         settled (issue #776).
     *
     *  Max's summary is "print the contents of a dictionary in the Max
     *  Console". This patcher is headless and there is no Max window, so the
     *  destination is the engine log — ``.print``'s destination, for
     *  ``.print``'s reasons: it is the thing a host already reads, redirects
     *  with ``YSE::Log().setLogfile()`` and intercepts with
     *  ``YSE::Log().setHandler()``.
     *
     *  It is the debugging instrument for structured data and the counterpart
     *  of ``.print`` (#546): a dictionary is the one patcher value a patch
     *  cannot see by wiring it to a sink, because what a cord carries is only
     *  its name.
     *
     *  ### What arrives, and what happens
     *
     *  A dictionary never travels down a cord — an ``OUT_TYPE`` carries a
     *  value, not an identity (see gDict.h for the whole argument) — so the
     *  dictionary is **bound from the first creation argument**, on the
     *  control thread: ``.dict.print <name>`` holds one
     *  ``shared_ptr<dictStore>`` resolved in ``SetParent`` / ``PARM_PARSE`` /
     *  ``RefreshBinding``, exactly as ``gDict`` and the rest of the family
     *  resolve theirs.
     *
     *  - **A bang dumps**: the whole dictionary, as a nested multi-line JSON
     *    document, one log line per row, each prefixed ``<name>: `` so two of
     *    these in one patch are tellable apart. Nested JSON rather than a list
     *    of dotted paths, because that is what a human reading the log wants —
     *    the same document a saved patch holds and ``DictToJson`` produces.
     *  - **``dictionary <name>`` dumps too** — the message a ``.dict``'s
     *    reference outlet emits on a bang, so wiring that outlet here gives
     *    Max's own gesture. Honoured only when it names the dictionary already
     *    bound — ``DictReferenceNames``, the bounded compare — and refused and
     *    counted otherwise, because resolving an unrecognised name means the
     *    registry's mutex on whatever thread the message arrived on.
     *    Re-pointing at a different name from a message is not portable, for
     *    the reason gDict.h gives for not porting ``refer``.
     *
     *  There are **no outlets** — ``.print``'s shape, and the right one: an
     *  outlet would turn a probe into a participant, and a patch that had to
     *  wire this object's output somewhere could no longer leave it in place
     *  once it was working.
     *
     *  ### Real-time behaviour — the whole of the design
     *
     *  A message handler runs on whichever thread dispatched the message, and
     *  in-patcher delivery dispatches on ``T_DSP`` — routinely the audio
     *  callback. ``.print`` already solved "log from a message path" and this
     *  object reuses its mechanism whole: nothing is ever logged from here.
     *  Each line is composed into a buffer the object owns and pushed onto
     *  ``INTERNAL::RtLog()``, the bounded lock-free queue whose control-thread
     *  drain hands it to the log on the host's next ``system::update()`` tick.
     *  Nothing on the message path allocates, locks or blocks; ``Calculate()``
     *  does nothing at all.
     *
     *  Two consequences follow, and both are the family's own moves:
     *
     *  - **The dump is snapshotted.** The store cannot be held while lines are
     *    queued — the guard is a try-lock other writers would then lose — so
     *    the trigger copies the dictionary out under its guard into a snapshot
     *    pre-allocated at construction, releases, and formats the snapshot.
     *    ``gDictIter``'s move, for ``gDictIter``'s reason, and it makes the
     *    dump atomic: what is printed is the dictionary as it stood at the
     *    trigger, however it moves while the lines drain.
     *  - **The JSON is composed by a bounded emitter of this object's own,**
     *    not by ``DictToJson`` — that function builds an ``nlohmann::json``
     *    tree, which allocates by construction and is therefore control-thread
     *    only. The emitter walks the snapshot's flat ``::`` paths directly
     *    (bounded prefix scans, the sub-tree walk gDict.h documents) and
     *    writes the same document: entries appear grouped under their path
     *    segments in storage order of first appearance, an entry whose path
     *    collides with an established leaf or branch loses to the
     *    earlier-stored one (``DictToJson``'s rule, which is ``.coll``'s), an
     *    entry with an empty path segment is skipped, and values are typed by
     *    the patcher's own classifier — a single numeric token prints as a
     *    JSON number, a multi-token value as an array, anything else as a
     *    string. The doctest suite holds the two implementations in lockstep
     *    by parsing what this object printed and comparing it against
     *    ``DictToJson``'s document for the same store.
     *
     *  ### Flooding, and why the drops are visible
     *
     *  ``.print``'s two bounds, unchanged: this object's own budget of lines
     *  per drain tick (the second creation argument), and the shared queue's
     *  capacity. A dump that runs past the budget is cut and the first refusal
     *  in a tick posts one notice line, so a truncated dump reads as a marked
     *  gap rather than as a complete dictionary — the one thing a debugging
     *  instrument must never lie about. Every refusal is counted
     *  (``Dropped``). The default budget is wider than ``.print``'s because
     *  the unit of output here is a whole document rather than a line: 64
     *  covers a dictionary of a few dozen entries, and the argument raises it
     *  to the queue's own capacity when a patch needs more.
     *
     *  A line longer than the queue's record is cut and marked with a trailing
     *  ``...`` — ``.print``'s rule, for ``.print``'s reason: this is a line to
     *  read, where the first 240 characters are almost all of the information
     *  and silence is none of it.
     *
     *  ### The message budget
     *
     *  One trigger emits at most ``LinesPerTick()`` lines, each a bounded
     *  compose and one lock-free push — no sends, since there is no outlet, so
     *  no subgraph runs under the dump. The emitter's prefix grouping is a
     *  bounded scan of at most ``dictStore::MAX_ENTRIES`` rows per row, which
     *  is the family's documented cost for wanting a sub-tree out of a flat
     *  table.
     *
     *  ### What persists
     *
     *  The creation arguments, because they are creation arguments. Nothing
     *  else: the lines already sent belong to the log rather than to the
     *  patch.
     */
    PATCHER_CLASS(gDictPrint, YSE::OBJ::G_DICT_PRINT)
    _NO_MESSAGES
    _NO_CALCULATE

    _BANG_IN(BangIn)
    _LIST_IN(ListIn)

    _PARM_CLEAR
    _PARM_PARSE

    /** @brief The label in front of every line when no name argument gives
     *         one — the unnamed object's private dictionary still dumps, as
     *         the rest of the family treats an unnamed binding. */
    static constexpr const char* DEFAULT_LABEL = "dict.print";

    /** @brief Longest label used as given — ``gPrint::NAME_CAPACITY``, for
     *         gPrint's reason: the whole line has to fit a fixed record. */
    static constexpr std::size_t LABEL_CAPACITY = 32;

    /** @brief Lines per drain tick with no second argument. Wider than
     *         ``.print``'s 16 because one trigger here is a whole document
     *         rather than a line — 64 covers a dictionary of a few dozen
     *         entries without letting one dump spend the shared queue. */
    static constexpr int DEFAULT_LINES_PER_TICK = 64;

    /** @brief Fewest lines a second argument can ask for. Zero is not
     *         offered: an object that printed nothing is a deleted object,
     *         spelled confusingly. */
    static constexpr int MIN_LINES_PER_TICK = 1;

    /** @brief Most lines a second argument can ask for — the queue's own
     *         capacity, past which asking for more cannot buy anything. */
    static constexpr int MAX_LINES_PER_TICK = 256;

    /** @brief Most entries a dump can hold — ``dictStore::MAX_ENTRIES``. */
    static constexpr std::size_t MAX_ENTRIES = dictStore::MAX_ENTRIES;

    /** @brief The dictionary's shared name — the first creation argument, or
     *         empty for a private (empty) dictionary. Also the line label. */
    const std::string& DictName() const {
      return dictName;
    }

    /** @brief The address the store is registered under —
     *         ``"<patcherName>.<name>"`` — or empty while it is private. */
    const std::string& Address() const {
      return boundAddress;
    }

    /** @brief This object's budget, in lines per drain tick. */
    int LinesPerTick() const {
      return linesPerTick;
    }

    /**
     *  @brief Messages refused so far — a reference naming a dictionary this
     *         object is not bound to, an unrecognised message, a lost
     *         try-lock, a re-entrant trigger, and every line the budget or the
     *         full queue turned away.
     *
     *  A counter rather than a log line because the refusing thread may be
     *  the audio callback; ``gPrint::Dropped``, for ``gPrint``'s reason.
     */
    std::uint64_t Dropped() const {
      return dropped.load(std::memory_order_relaxed);
    }

    /** @brief Lines this object has handed to the queue. Diagnostics and
     *         tests; what reaches the log is this minus whatever the queue
     *         itself had to refuse. */
    std::uint64_t Posted() const {
      return posted.load(std::memory_order_relaxed);
    }

    // Bind the store the moment the patcher name is known, so a message
    // never resolves a name. gDict::SetParent's rule.
    void SetParent(pObject* parent) override;

    // Re-bind after a patcher rename: the address prefix moved, so the object
    // now dumps a different dictionary. Called from
    // patcherImplementation::SetName alongside gDict::RefreshBinding.
    void RefreshBinding();

  private:
    // Point the store at the current name and parent address. Control thread
    // only (SetParams / SetParent / SetName). A no-op when the address has
    // not changed.
    void Rebind();

    // The dump itself: snapshot the store under its guard, then walk the
    // snapshot emitting one JSON row per line onto the RT log queue. Refuses
    // (counted) on a lost guard or a re-entrant trigger.
    void Dump();

    // One level of the nested document: every unconsumed snapshot entry under
    // the current `path` prefix, grouped by its next path segment, leaves and
    // sub-objects alike. Recursion is bounded by the deepest path a
    // KEY_CAPACITY key can spell. RT-safe: bounded scans and appends only.
    void EmitLevel(std::size_t prefixLength, int depth);

    // Append one value, spelled as JSON, to `compose`: "" for an empty value,
    // a number for a single numeric token, an array for a multi-token value,
    // an escaped string otherwise — the patcher's own classifier, which is
    // also DictToJson's. RT-safe.
    void AppendValueJson(const std::string& value);
    void AppendTokenJson(const char* text, std::size_t length);
    void AppendStringJson(const char* text, std::size_t length);

    // The pending-line machinery that turns the walk into valid JSON without
    // lookahead: the last composed row is held back until the walk knows
    // whether a sibling follows (comma) or the level closes (none). See the
    // .cpp for the two-row dance.
    void BeginRow(int depth);
    void CommitRow(bool open);
    void FlushPending(bool comma);
    void AppendText(const char* text, std::size_t length);

    // Post one finished line — "<label>: <row>" — through the budget onto the
    // RT log queue. `PostRaw` skips the budget; it carries the notice that the
    // budget has bitten, which would otherwise be its first casualty.
    // gPrint's shapes, unchanged. RT-safe on every branch.
    void Post(const char* body, std::size_t bodyLength);
    void PostRaw(const char* body, std::size_t bodyLength);
    bool ClaimBudget();

    void Refuse() {
      dropped.fetch_add(1, std::memory_order_relaxed);
    }

    // Widest row the emitter can compose before the record-size cut: the
    // deepest indent, an escaped key segment, and a fully escaped multi-token
    // value all fit with room to spare.
    static constexpr std::size_t COMPOSE_CAPACITY = 4096;

    // The shared name. First creation argument; empty means a private, empty
    // dictionary. Control thread only after construction — a live SetParams
    // re-parse rebuilds the object (LIST parameter), gPrint's rule.
    std::string dictName;

    // The budget, from creation argument 2. Written on the same control-thread
    // path as `dictName` and read the same way.
    int linesPerTick = DEFAULT_LINES_PER_TICK;

    // The address the store is registered under, or empty while it is
    // private. Also the "has the binding changed?" key Rebind() compares
    // against.
    std::string boundAddress;

    // The dictionary. Never null once the object has been constructed, so no
    // message handler needs a null check.
    std::shared_ptr<dictStore> store;

    // What the dump actually prints: the dictionary as it stood at the
    // trigger, copied out under the store's guard so the lines describe one
    // consistent document however the store moves while they drain. Allocated
    // whole by dictStore's own constructor, on the control thread, once.
    dictStore snapshot;

    // Which snapshot entries the walk has already spoken for — emitted,
    // skipped as invalid, or lost a collision. Dump-local state, but a member
    // so the message path owns no stack array of MAX_ENTRIES.
    bool consumed[dictStore::MAX_ENTRIES] = {};

    // The "::" path prefix of the level being emitted, built as the walk
    // descends. One key plus one separator of headroom.
    char prefix[dictStore::KEY_CAPACITY + 4] = {};

    // The row being composed, and the finished row held back until the walk
    // knows what follows it. `pendingOpen` marks a held-back "{" opener,
    // which never takes a comma and absorbs its "}" when its level turns out
    // empty.
    char compose[COMPOSE_CAPACITY] = {};
    std::size_t composeLength = 0;
    char pending[COMPOSE_CAPACITY] = {};
    std::size_t pendingLength = 0;
    bool pendingOpen = false;

    // The re-entrancy guard: two threads dumping into one emitter would
    // interleave their rows. The loser is dropped and counted rather than
    // made to spin — the family's guard, for the family's reason.
    std::atomic<bool> busy{false};

    // gPrint's budget clock, unchanged: the drain tick `usedThisTick` belongs
    // to, and the tick whose budget notice has already been posted.
    std::atomic<std::uint64_t> budgetTick{0};
    std::atomic<int> usedThisTick{0};
    std::atomic<std::uint64_t> noticeTick{0};

    std::atomic<std::uint64_t> dropped{0};
    std::atomic<std::uint64_t> posted{0};

    // The creation arguments after the name, as tokens. Control thread only:
    // written by Parameters::Set, read by ParseParams(), never by a message
    // handler. A vector of tokens rather than a plain int so a non-numeric
    // second argument from a saved patch leaves the default standing instead
    // of throwing the load away — gPrint's arrangement.
    std::vector<std::string> budgetArgs;
  };
} // namespace PATCHER
} // namespace YSE
