#pragma once
#include "../namedStore.h"
#include "../pAtomList.h"
#include "../pObject.h"
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief One key/value pair of a ``dictStore``.
     *
     *  ``key`` is a **whole path** — ``"voice::1::freq"``, not one segment. See
     *  the nesting note on ``dictStore``: the tree exists in the key, not in the
     *  storage.
     *
     *  ``value`` is the patcher's own transport form, list text, so a stored
     *  value is exactly what arrived on the cord and exactly what leaves again.
     *  Its int/float/symbol/list reading is decided at the outlet by
     *  ``SendAtom``, the same classifier ``.zl``, ``.thresh`` and ``.trigger``
     *  use, rather than being carried as a second field that could disagree with
     *  the characters.
     */
    struct dictEntry {
      std::string key;
      std::string value;
    };

    /**
     *  @brief The shared table behind ``.dict`` — one per name, held by every
     *         object that addresses it (issue #550).
     *
     *  ``collStore``'s shape with ``collStore``'s justification, and the reasons
     *  are worth restating because this is the store the whole ``dict.*`` family
     *  will be written against:
     *
     *  - **Allocated whole in the constructor.** The table is sized to
     *    ``MAX_ENTRIES`` and every string inside it reserved to its capacity, on
     *    the control thread, once. Nothing on a message path resizes the vector
     *    or grows a string inside it, so a ``set`` arriving from a rendering
     *    graph is a bounded ``assign`` into storage that already exists.
     *  - **Guarded by a try-lock, not a mutex.** A ``.dict`` is written from
     *    whichever thread the message arrived on, and in-patcher delivery
     *    dispatches on ``T_DSP`` — "the audio thread stores into a dict" is the
     *    normal case. ``std::mutex`` is out by the engine's standing rule, and a
     *    seqlock is out because several objects on one name means there is no
     *    single writer. What is left is mutual exclusion that never waits:
     *    ``busy`` is claimed with one ``exchange`` and the loser **drops** its
     *    store or its read rather than spinning. ``.value``'s guard, for
     *    ``.value``'s reason.
     *  - **Refusal, never truncation.** A key or a value past its capacity, or a
     *    store into a full table, is refused whole and counted
     *    (``gDict::Dropped``) rather than logged — formatting a log line builds a
     *    ``std::string`` on whichever thread the message arrived on.
     *
     *  ### Nesting lives in the key
     *
     *  Max's ``dict`` is a *nested* structure, and this table is flat. The tree
     *  is carried by the key instead: ``"voice::1::freq"`` is one entry whose
     *  path has three segments, and ``::`` is Max's own path separator (``dict``
     *  addresses ``getvalue foo::bar`` exactly this way), so a patch brought
     *  across spells its paths unchanged.
     *
     *  That is the trade the real-time constraint forces, and it is worth being
     *  explicit about what it buys and what it costs. A genuinely nested store
     *  is a tree of nodes, and a tree of nodes is an allocation per branch on a
     *  path that may be the audio callback — which is the whole reason the
     *  patcher has no dynamic containers anywhere on a message path. Flattening
     *  the tree into the key makes every mutation a bounded write into a
     *  pre-sized row, and it costs exactly one thing: a sub-tree is not a value,
     *  so it cannot be fetched, passed or replaced in one message. Every
     *  operation that wants a sub-tree walks the paths that begin with its
     *  prefix — a bounded scan of at most ``MAX_ENTRIES`` rows, which is what
     *  ``dict.slice``, ``dict.group`` and ``dict.strip`` will each do.
     *
     *  The nesting is real again the moment it leaves: ``DictToJson`` expands
     *  ``::`` into nested JSON objects and ``DictFromJson`` flattens them back,
     *  so what a patch saves and what ``dict.serialize`` will emit is a proper
     *  nested document rather than a table of dotted strings.
     */
    struct dictStore {
      /**
       *  @brief Most entries a dictionary holds — 256.
       *
       *  The patcher's own bound: ``collStore::MAX_ENTRIES``,
       *  ``patcherImplementation::kValueListCap``, ``AtomList::MAX_ATOMS``. A
       *  ``set`` past it is refused rather than growing the table, since growing
       *  it would allocate on whichever thread the message arrived on.
       */
      static constexpr std::size_t MAX_ENTRIES = 256;

      /**
       *  @brief Longest key path, in characters — 128.
       *
       *  Twice ``collStore::KEY_CAPACITY``, and for a reason rather than for
       *  symmetry: a ``.coll`` address is one word, where a dict key is a whole
       *  path and every level of nesting spends its segment plus two characters
       *  of separator.
       */
      static constexpr std::size_t KEY_CAPACITY = 128;

      /** @brief Longest stored value, in characters — ``kValueListCap``, the
       *         bound every other patcher payload carries. */
      static constexpr std::size_t VALUE_CAPACITY = 256;

      dictStore();

      // Claimed with a single exchange by readers and writers alike; the loser
      // drops. `.value`'s guard, for `.value`'s reason.
      std::atomic<bool> busy{false};

      // The table. Sized to MAX_ENTRIES when the store is built and never
      // resized; only the first `count` entries are live.
      std::vector<dictEntry> entries;
      std::size_t count = 0;
    };

    /**
     *  @brief Non-blocking exclusive access to a ``dictStore``.
     *
     *  ``Held()`` is false when another thread had the table — the caller then
     *  does nothing at all. Never waits, never allocates. Do not hold one across
     *  an outlet send: the send runs the whole downstream graph, which may well
     *  store into this same dictionary, and inside the guard that store would be
     *  the one thing the try-lock drops.
     */
    class dictStoreGuard {
    public:
      explicit dictStoreGuard(std::atomic<bool>& flag)
        : flag_(flag), held_(!flag.exchange(true, std::memory_order_acquire)) {}
      ~dictStoreGuard() {
        if (held_) flag_.store(false, std::memory_order_release);
      }
      dictStoreGuard(const dictStoreGuard&) = delete;
      dictStoreGuard& operator=(const dictStoreGuard&) = delete;
      dictStoreGuard(dictStoreGuard&&) = delete;
      dictStoreGuard& operator=(dictStoreGuard&&) = delete;

      bool Held() const {
        return held_;
      }

    private:
      std::atomic<bool>& flag_;
      bool held_;
    };

    /** @brief The path separator between key segments — Max's own. */
    constexpr char kDictPathSeparator[] = "::";

    /** @brief The message word a dictionary reference travels as, matching
     *         Max's ``dictionary <name>``. */
    constexpr char kDictReferenceWord[] = "dictionary";

    /**
     *  @brief True when the @p length characters at @p text are the message
     *         ``dictionary <name>`` naming exactly @p name.
     *
     *  **This is the whole of what a ``dict.*`` object may do with a name that
     *  arrives on a cord**, and it is a shared function rather than a habit
     *  because twelve objects are about to need it. A bounded comparison: no
     *  allocation, no lock, no registry lookup, so it is safe on whichever
     *  thread the message was dispatched on. See the addressing note on
     *  ``gDict`` for why resolving an *unrecognised* name here is not
     *  available.
     */
    bool DictReferenceNames(const char* text, std::size_t length, const std::string& name);

    /** @brief ``DictReferenceNames`` over a whole ``std::string`` message. */
    inline bool DictReferenceNames(const std::string& text, const std::string& name) {
      return DictReferenceNames(text.c_str(), text.size(), name);
    }

    /**
     *  @brief Position of the key path @p key in @p store, or ``store.count``
     *         when it is not there.
     *
     *  **The caller holds the store's guard.** Allocation-free and lock-free, so
     *  it is safe on whichever thread a message was dispatched on. A linear scan
     *  of at most ``MAX_ENTRIES`` rows: the table is small and bounded, and an
     *  index would be a second structure to keep in step with ``DictEraseAt``'s
     *  renumbering for no measurable gain at this size.
     */
    std::size_t DictFind(const dictStore& store, const char* key, std::size_t keyLength);

    /**
     *  @brief Store @p value at the key path @p key, replacing an entry of the
     *         same path.
     *
     *  **The caller holds the store's guard.** Allocation-free: both strings are
     *  reserved to their capacities when the store is built, and neither this
     *  nor anything else on a message path resizes the table.
     *
     *  False, changing nothing, when the key is empty or longer than
     *  ``KEY_CAPACITY``, the value is longer than ``VALUE_CAPACITY``, or the
     *  table is full — refused rather than truncated, and silently, since this
     *  may be the audio thread. Reporting the refusal is the caller's job
     *  (``gDict::Dropped``).
     */
    bool DictStoreAt(dictStore& store, const char* key, std::size_t keyLength, const char* value,
                     std::size_t valueLength);

    /**
     *  @brief Drop the entry at @p position, closing the gap.
     *
     *  **The caller holds the store's guard.** Allocation-free: the rows above
     *  move down by ``assign`` into storage they already own. Out of range is a
     *  no-op.
     */
    void DictEraseAt(dictStore& store, std::size_t position);

    /**
     *  @brief Expand @p store's ``::`` paths into a nested JSON object.
     *
     *  **Control thread only** — it allocates, by construction. The caller holds
     *  the store's guard.
     *
     *  This is the half of the design that makes a flat table a nested document
     *  again, and it is a free function rather than a member so that
     *  ``dict.serialize`` (and the ``DumpState`` hook below) share one
     *  implementation. Values are typed on the way out by the patcher's own
     *  classifier: a single numeric token becomes a JSON number spelled the way
     *  it was stored (``120`` an integer, ``120.`` a real), a multi-token value
     *  becomes an array of those, and anything else becomes a string.
     *
     *  An entry whose path collides with an established leaf — ``a`` stored as a
     *  value and ``a::b`` stored under it — is skipped rather than silently
     *  destroying the leaf. Storage order decides which of the two wins, which
     *  is the same rule ``.coll`` applies to a duplicate address.
     */
    void DictToJson(const dictStore& store, nlohmann::json::value_type& out);

    /**
     *  @brief Flatten a nested JSON object into @p store's ``::`` paths,
     *         replacing whatever it held.
     *
     *  ``DictToJson``'s inverse and the reader half ``dict.deserialize`` will
     *  use. **Control thread only**, and the caller holds the guard. Objects
     *  recurse; every other JSON kind is a leaf rendered as patcher list text —
     *  an array becomes the space-separated list it spells, a boolean becomes
     *  ``1`` / ``0``, and null becomes an empty value.
     *
     *  Entries whose path or value does not fit the store's capacities, and
     *  entries past ``MAX_ENTRIES``, are dropped; the rest of the document still
     *  loads. That is ``.coll``'s rule for an over-long record, for
     *  ``.coll``'s reason.
     */
    void DictFromJson(const nlohmann::json::value_type& in, dictStore& store);

    /**
     *  @brief Append one stored value to @p out, spelled as JSON — the RT-safe
     *         walk of the classifier ``DictToJson`` applies.
     *
     *  ``DictToJson`` builds an ``nlohmann::json`` tree, which allocates by
     *  construction and is therefore control-thread only; a message path that
     *  wants the same document walks the value in place with these instead.
     *  One shared implementation rather than a habit because two objects
     *  already need it — ``.dict.print``'s log emitter and
     *  ``.dict.serialize``'s message emitter — and both hold it in lockstep
     *  with ``DictToJson`` through their doctest suites.
     *
     *  The classification is ``DictToJson``'s, unchanged: a stored value is
     *  list text, so a single numeric token becomes a JSON number (an integer
     *  spelled as its own digits — no ``+``, no leading zeros — and a float
     *  by the patcher's own formatter, its trailing point repaired), a
     *  multi-token value becomes an array of those joined by @p separator,
     *  nothing at all becomes ``""``, and anything else becomes an escaped
     *  string. ``@p separator`` is the text between array elements —
     *  ``", "`` for a document meant to be read, ``","`` for a compact one —
     *  because that is the only spelling the two emitters disagree on.
     *
     *  Bounded appends into @p out, never past @p capacity, no allocation, no
     *  lock — safe on whichever thread a message was dispatched on. False when
     *  the capacity margin cut anything: a caller whose document must parse
     *  (``.dict.serialize``) refuses the whole send on false, where a caller
     *  producing lines to read (``.dict.print``) lets its record-size cut mark
     *  the loss instead.
     */
    bool DictAppendValueJson(const std::string& value, char* out, std::size_t& outLength,
                             std::size_t capacity, const char* separator,
                             std::size_t separatorLength);

    /** @brief One token of ``DictAppendValueJson``'s walk: a number when the
     *         token spells one, an escaped string otherwise. Same bounds and
     *         thread-safety; false when anything was cut. */
    bool DictAppendTokenJson(const char* text, std::size_t length, char* out,
                             std::size_t& outLength, std::size_t capacity);

    /** @brief Append @p length characters at @p text as one JSON string —
     *         quoted, with ``"``, ``\``, and control characters escaped. Same
     *         bounds and thread-safety; false when anything was cut. */
    bool DictAppendStringJson(const char* text, std::size_t length, char* out,
                              std::size_t& outLength, std::size_t capacity);

    /**
     *  @brief A nested key/value dictionary shared by name — ``.dict``
     *         (issue #550), and **the patcher's answer to the reference-passed
     *         value question** the array (#548), string (#549) and dict epics
     *         all asked.
     *
     *  Max's ``dict``: "a dictionary is a collection of key/value pairs" whose
     *  values may themselves be dictionaries, addressed by name and shared by
     *  every object that names it.
     *
     *  ### The decision: dicts are name-addressed, not reference-passed
     *
     *  The three data-type epics were filed as one question — *how does a
     *  mutable, reference-passed value travel between patcher objects?* — and
     *  the answer for all three is that **it does not travel; its name does.**
     *
     *  A cord in this patcher is not a message object. ``OUT_TYPE`` is
     *  ``BANG / FLOAT / INT / BUFFER / LIST / ANY``, and a send is a direct
     *  synchronous call — ``SendInt(int)``, ``SendList(const std::string&)`` —
     *  with the payload passed by value or as a borrowed string the receiver
     *  must copy before it returns. There is no atom, no variant and no
     *  refcounted box anywhere in the transport. ``.textedit`` (#560) put the
     *  consequence plainly: an ``OUT_TYPE`` cannot carry an identity down a
     *  cord.
     *
     *  Adding one is not a small change and it is not a good one. A cord that
     *  carried a shared pointer would put reference-count traffic on
     *  ``outlet::Send*``, which is the hottest path in the graph and routinely
     *  the audio callback; it would make a value's lifetime span the
     *  ``GraphState`` swap (#226/#227), so an edit that deletes an object could
     *  free a value another object is mid-read on; and it would give every one
     *  of the sixty-odd existing objects a payload kind they have no handler
     *  for. The wait-free scalar plan (#234) and the ``GraphState`` swap are how
     *  state crosses the control/audio boundary here, and neither of them moves
     *  ownership across a cord — they publish a snapshot the reader does not
     *  own.
     *
     *  So the model is Max's own, which is name addressing:
     *
     *  - **Storage lives in the named registry.** ``AcquireNamedStore<dictStore>``
     *    (``patcher/namedStore.h``, #684) keys real storage by
     *    ``"<patcherName>.<name>"`` — the ``INTERNAL::NamedBus`` address form,
     *    so a ``.dict tempo``, a ``.value tempo``, a ``.s tempo`` and a ``.r
     *    tempo`` in one patcher all speak about one word.
     *  - **The registry holds stores weakly, so ownership is exactly "whoever
     *    addresses the name".** A dictionary lives as long as some object holds
     *    it and is freed when the last one goes. There is no owner object, no
     *    manual free, and no way for a name a patch once spelled to leak for the
     *    life of the process.
     *  - **The name is resolved once, on the control thread**, in ``SetParent``
     *    / ``PARM_PARSE`` / ``RefreshBinding``. ``AcquireNamedStore`` takes a
     *    mutex and may allocate, and a message handler runs on whichever thread
     *    dispatched it, so a name resolved on a message path would be a mutex
     *    and an allocation on the audio callback. After that the object holds a
     *    ``shared_ptr`` and every access is one pointer hop and one
     *    ``exchange``. **No message path ever copies or drops the
     *    ``shared_ptr``**, so no refcount operation lands on the audio thread.
     *  - **What a cord carries is the message ``dictionary <name>``** — the
     *    *local* name, not the resolved address, because the receiving object
     *    prefixes it with its own patcher's name exactly as this one does. Out
     *    of outlet 1 on a bang, which is what feeds the ``dict.*`` family.
     *
     *  ### The consequence the ``dict.*`` sub-issues inherit
     *
     *  A ``dict.*`` object binds its dictionary from its **creation argument**,
     *  on the control thread. A ``dictionary <name>`` message arriving on an
     *  inlet is honoured only when it names the dictionary already bound —
     *  ``DictReferenceNames`` above, a bounded compare — and is otherwise
     *  refused and counted. Re-pointing at a *different* name from a message is
     *  not portable, for the reason ``.coll`` gives for not porting Max's
     *  ``refer`` and ``clockBridge`` gives for binding names wait-free: the
     *  lookup is a mutex.
     *
     *  If some later operation genuinely needs run-time rebinding, the pattern
     *  already exists and it is ``clockBridge``'s (#688): claim a slot from a
     *  fixed table with one CAS, copy the name inline, and let a pre-allocated
     *  background-pool job do the registry lookup and publish the result with a
     *  release store. That is a bridge to be written when an operation needs it,
     *  not machinery to add speculatively.
     *
     *  ### The named bus carries the name, never the dictionary
     *
     *  ``INTERNAL::NamedBus`` is a publish/subscribe **value** bus: it hands a
     *  copy of a published payload to every subscriber, holds no storage, owns
     *  no lifetime, and cannot answer "give me the object called X" — which is
     *  the paragraph ``namedStore.h`` was written to record. Its audio-path
     *  queue entry is a fixed POD, so ``publish`` from ``T_DSP`` drops anything
     *  that is not an int or a float, silently and by design.
     *
     *  **That stays exactly as it is, and no dict payload is added to it.**
     *  Publishing a dictionary's contents would mean copying a table per
     *  subscriber per write, on a path that may be the audio callback; and a
     *  dictionary is shared state rather than an event, so a value bus is the
     *  wrong shape for it twice over. What goes on the bus is the **name**: a
     *  ``.s`` fed from this object's reference outlet publishes the symbol
     *  ``dictionary <name>``, a ``.r`` on the other side receives it, and
     *  whatever wants the contents binds that name on the control thread. The
     *  bus stays a value bus; the registry stays the thing that owns storage.
     *
     *  ### An unnamed ``.dict`` is private
     *
     *  Not "shares the empty name". ``"<patcherName>."`` is a real, reachable
     *  address, so unnamed objects pooling on it would silently connect two
     *  dictionaries a patch author never wired together. ``.value``'s rule, for
     *  ``.value``'s reason. A ``.dict`` with no parent yet is private for the
     *  same reason — there is no patcher name to prefix with until
     *  ``SetParent``.
     *
     *  ### What persists, and who restores it
     *
     *  The **contents**, through ``pObject::DumpState`` / ``RestoreState``, as
     *  a nested JSON object under the object's ``state`` key. Every bound
     *  ``.dict`` writes them and only the object that **created** the store
     *  reads them back — ``.coll``'s rule (#684), and for ``.coll``'s reasons:
     *  a single nominated writer would stop saving the day that object was
     *  deleted from the patch, and a patcher loaded into an engine where its
     *  name is already live must join the running dictionary rather than reset
     *  it under whatever is using it.
     *
     *  The serialisation goes through the layer ``DumpJSON`` / ``ParseJSON``
     *  actually runs on, which is **nlohmann::json** — the same
     *  ``nlohmann::json::value_type`` every other object's state hook takes.
     *  (#550 names ``YseEngine/json/cJSON.cpp``; that file is vendored for the
     *  engine's own use and the patcher does not go through it. Routing dict
     *  state through cJSON would add the second JSON implementation the issue
     *  asks not to add, not avoid it.)
     *
     *  ### Real-time behaviour
     *
     *  ``Calculate()`` does nothing. The object is driven by its inlet, and an
     *  emitting ``Calculate()`` would re-send on every DSP tick — the rule
     *  ``.value``, ``.coll``, ``.route`` and ``.sel`` establish.
     *
     *  No message path allocates, locks or blocks. The name is resolved on the
     *  control thread; a store is a bounded ``assign`` into a pre-reserved
     *  string; a fetch copies out under the guard into a scratch reserved at
     *  construction and sends after releasing it; the reference message is
     *  built once per rebind, so a bang is a ``SendList`` of a string the object
     *  already owns.
     */
    PATCHER_CLASS(gDict, YSE::OBJ::G_DICT)
    _NO_MESSAGES
    _NO_CALCULATE

    _BANG_IN(BangIn)
    _LIST_IN(ListIn)

    _PARM_CLEAR
    _PARM_PARSE

    /** @brief Most entries the dictionary holds — ``dictStore::MAX_ENTRIES``. */
    static constexpr std::size_t MAX_ENTRIES = dictStore::MAX_ENTRIES;

    /** @brief Longest key path, in characters. */
    static constexpr std::size_t KEY_CAPACITY = dictStore::KEY_CAPACITY;

    /** @brief Longest stored value, in characters. */
    static constexpr std::size_t VALUE_CAPACITY = dictStore::VALUE_CAPACITY;

    /** @brief The shared name, or empty when this ``.dict`` has a store of its
     *         own. The first creation argument. */
    const std::string& DictName() const {
      return dictName;
    }

    /** @brief The address the store is registered under —
     *         ``"<patcherName>.<name>"`` — or empty while it is private. */
    const std::string& Address() const {
      return boundAddress;
    }

    /** @brief Whether this object's store is shared by name rather than
     *         private. */
    bool IsShared() const {
      return !boundAddress.empty();
    }

    /** @brief The message a bang emits out outlet 1 — ``"dictionary <name>"``,
     *         or empty for an unnamed dictionary, which has no name to pass on. */
    const std::string& Reference() const {
      return reference;
    }

    /** @brief How many entries the dictionary holds. */
    std::size_t Count() const {
      return store->count;
    }

    /** @brief The value at @p path, or empty when there is none. Diagnostics
     *         and tests; allocates, so control thread only. */
    std::string Lookup(const std::string& path) const;

    /** @brief The key path at storage position @p position, or empty.
     *         Diagnostics and tests; control thread only. */
    std::string KeyAt(std::size_t position) const;

    /**
     *  @brief Messages refused so far — a full table, an over-long key or
     *         value, or a lost try-lock.
     *
     *  A counter rather than a log line because the refusing thread may be the
     *  audio callback; ``.zl``'s ``Dropped()``, for ``.zl``'s reason.
     */
    std::uint64_t Dropped() const {
      return dropped.load(std::memory_order_relaxed);
    }

    // Bind to the store the moment the patcher name is known, so a message
    // never resolves a name. gValue::SetParent's rule.
    void SetParent(pObject* parent) override;

    // Re-bind after a patcher rename: the address prefix moved, so the object
    // now addresses a different dictionary. Called from
    // patcherImplementation::SetName alongside gValue::RefreshBinding and
    // gColl::RefreshBinding.
    void RefreshBinding();

    // The contents, as a nested JSON object. See the class notes on who writes
    // and who restores. Control thread only, both of them.
    void DumpState(nlohmann::json::value_type& json) override;
    void RestoreState(const nlohmann::json::value_type& json) override;

  private:
    // Point at the store the current name and parent address, creating it if
    // this is the first .dict to name it. Control thread only (SetParams /
    // SetParent / SetName). A no-op when the address has not changed, so the
    // contents survive a re-parse that leaves the name alone.
    void Rebind();

    // The message handlers, each taking the guard itself and each releasing it
    // before any send.
    void HandleSet(const std::string& text, std::size_t argOffset);
    void HandleGet(const std::string& text, std::size_t argOffset, YSE::THREAD thread);
    void HandleDelete(const std::string& text, std::size_t argOffset);
    void HandleClear();
    void HandleGetSize(YSE::THREAD thread);
    void HandleGetKeys(YSE::THREAD thread);

    // Rebuild `reference` from the current name. Control thread only.
    void RefreshReference();

    void Refuse() {
      dropped.fetch_add(1, std::memory_order_relaxed);
    }

    // The shared name. First creation argument; empty means a private store.
    std::string dictName;

    // The address `store` is registered under, or empty while it is private.
    // Also the "has the binding changed?" key Rebind() compares against.
    std::string boundAddress;

    // The dictionary. Never null once the object has been constructed, so no
    // message handler needs a null check.
    std::shared_ptr<dictStore> store;

    // Whether this object brought `store` into existence — what decides whether
    // it restores saved contents into it. `.coll`'s rule.
    bool createdStore = true;

    // "dictionary <name>", built once per rebind so a bang is a send of a
    // string the object already owns rather than a concatenation on whichever
    // thread the bang arrived on.
    std::string reference;

    // Where a fetched value is copied to while the guard is held, so the send
    // can happen after the guard is released. A plain array rather than a
    // string because it is written from inside the critical section, where an
    // assign that had to grow would be the one thing that must not happen.
    char fetched[dictStore::VALUE_CAPACITY + 1] = {};
    std::size_t fetchedLength = 0;

    // Render buffer for the outlet, reserved to AtomList::RENDER_CAPACITY at
    // construction — wide enough for a fetched value and for the whole `getkeys`
    // list, so neither send allocates.
    std::string emitScratch;

    // Working list for `getkeys`, which de-duplicates the first path segment of
    // every entry. An AtomList rather than a string because the de-duplication
    // needs token boundaries, and it reserves its own text at construction.
    AtomList keyScratch;

    // Refusals, published for tests and diagnostics.
    std::atomic<std::uint64_t> dropped{0};
  };
} // namespace PATCHER
} // namespace YSE
