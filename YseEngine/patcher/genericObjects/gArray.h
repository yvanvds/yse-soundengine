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
     *  @brief The shared, ordered sequence behind ``.array`` — one per name, held
     *         by every object that addresses it (issue #548).
     *
     *  ``dictStore``'s shape and ``dictStore``'s justification (#550), with one
     *  deliberate difference stated below. The shape first, because it is what
     *  the whole ``array.*`` family will be written against:
     *
     *  - **Allocated whole in the constructor.** The table is sized to
     *    ``MAX_ELEMENTS`` and every string inside it reserved to
     *    ``ELEMENT_CAPACITY``, on the control thread, once. Nothing on a message
     *    path resizes the vector or grows a string inside it, so a ``push``
     *    arriving from a rendering graph is a bounded ``assign`` into storage
     *    that already exists.
     *  - **Guarded by a try-lock, not a mutex.** An ``.array`` is written from
     *    whichever thread the message arrived on, and in-patcher delivery
     *    dispatches on ``T_DSP`` — "the audio thread pushes onto an array" is the
     *    normal case. ``std::mutex`` is out by the engine's standing rule, and a
     *    seqlock is out because several objects on one name means there is no
     *    single writer. ``busy`` is claimed with one ``exchange`` and the loser
     *    **drops** its operation rather than spinning. ``.value``'s guard, for
     *    ``.value``'s reason.
     *  - **Refusal, never truncation.** An element past its capacity, an index
     *    past the end, or a push onto a full array is refused whole and counted
     *    (``gArray::Dropped``) rather than logged — formatting a log line builds
     *    a ``std::string`` on whichever thread the message arrived on.
     *
     *  ### An element is one atom, and that is the difference from ``dictStore``
     *
     *  A ``dictEntry``'s value is **list text**: ``set chord 0 4 7`` stores one
     *  value holding three tokens, because a dictionary value is a whole payload
     *  reached by a name and nothing about the storage has to say where it ends.
     *  An array element is **one atom** — a single int, float or symbol — and the
     *  reason is the property that makes an array an array rather than a keyed
     *  table: it is a *sequence*, and the sequence's transport form is list text,
     *  where the boundary between elements is a space.
     *
     *  Allow list text per element and that transport becomes ambiguous. The
     *  arrays ``["0", "4 7"]`` and ``["0 4", "7"]`` render to the identical
     *  characters ``0 4 7``, so ``getvalue`` would be lossy, ``array.tolist``
     *  could not round-trip, and ``array.length`` would answer a different number
     *  than the ``.zl len`` of the list the array just emitted. One atom per
     *  element keeps the array and the list it spells the same object seen twice,
     *  which is the whole reason a patch reaches for an array. A value that
     *  genuinely has internal structure belongs in a ``.dict`` under a key, or in
     *  an array of its own.
     *
     *  That is also why ``ELEMENT_CAPACITY`` is 64 rather than ``dictStore``'s
     *  256: an element is a token, and ``NUMBER_TEXT_MAX`` (63) is already the
     *  widest token the patcher will read as a number.
     *
     *  ### An index is a position, and out of range is a miss
     *
     *  Indices are zero-based, and an index is refused rather than wrapped,
     *  clamped or counted from the end. Negative indices are refused too. That is
     *  decided here, once, rather than per operation, because forty-odd
     *  ``array.*`` objects are about to take an index argument and a family where
     *  half the objects wrap and half clamp is worse than either rule.
     *  ``array.wrap`` and ``array.rotate`` are the objects that exist to *provide*
     *  the other behaviours; everything else refuses.
     */
    struct arrayStore {
      /**
       *  @brief Most elements an array holds — 256.
       *
       *  The patcher's own bound: ``AtomList::MAX_ATOMS``,
       *  ``collStore::MAX_ENTRIES``, ``dictStore::MAX_ENTRIES``,
       *  ``patcherImplementation::kValueListCap``. Also Max's own default maximum
       *  list length, so the number a patch author already expects. A push past
       *  it is refused rather than growing the table, since growing it would
       *  allocate on whichever thread the message arrived on.
       */
      static constexpr std::size_t MAX_ELEMENTS = 256;

      /**
       *  @brief Longest element, in characters — 64.
       *
       *  A quarter of ``dictStore::VALUE_CAPACITY``, and for the reason stated
       *  above rather than for economy: an element is one atom, and
       *  ``NUMBER_TEXT_MAX`` (63) is the widest token ``ReadNumericToken`` will
       *  read as a number at all.
       */
      static constexpr std::size_t ELEMENT_CAPACITY = 64;

      arrayStore();

      // Claimed with a single exchange by readers and writers alike; the loser
      // drops. `.value`'s guard, for `.value`'s reason.
      std::atomic<bool> busy{false};

      // The table. Sized to MAX_ELEMENTS when the store is built and never
      // resized; only the first `count` elements are live.
      std::vector<std::string> elements;
      std::size_t count = 0;
    };

    /**
     *  @brief Non-blocking exclusive access to an ``arrayStore``.
     *
     *  ``Held()`` is false when another thread had the table — the caller then
     *  does nothing at all. Never waits, never allocates. Do not hold one across
     *  an outlet send: the send runs the whole downstream graph, which may well
     *  push onto this same array, and inside the guard that push would be the one
     *  thing the try-lock drops.
     *
     *  A class of its own rather than a shared one, which is the arrangement
     *  ``gValue``'s ``valueSlotGuard``, ``gColl``'s ``storeGuard`` and
     *  ``gDict``'s ``dictStoreGuard`` already established: each store type names
     *  its own guard so a caller cannot take the wrong table's flag, and the
     *  bodies are four lines each.
     */
    class arrayStoreGuard {
    public:
      explicit arrayStoreGuard(std::atomic<bool>& flag)
        : flag_(flag), held_(!flag.exchange(true, std::memory_order_acquire)) {}
      ~arrayStoreGuard() {
        if (held_) flag_.store(false, std::memory_order_release);
      }
      arrayStoreGuard(const arrayStoreGuard&) = delete;
      arrayStoreGuard& operator=(const arrayStoreGuard&) = delete;
      arrayStoreGuard(arrayStoreGuard&&) = delete;
      arrayStoreGuard& operator=(arrayStoreGuard&&) = delete;

      bool Held() const {
        return held_;
      }

    private:
      std::atomic<bool>& flag_;
      bool held_;
    };

    /** @brief The message word an array reference travels as, matching Max's
     *         ``array <name>`` and ``.dict``'s ``dictionary <name>``. */
    constexpr char kArrayReferenceWord[] = "array";

    /**
     *  @brief True when the @p length characters at @p text are the message
     *         ``array <name>`` naming exactly @p name.
     *
     *  **This is the whole of what an ``array.*`` object may do with a name that
     *  arrives on a cord**, and it is a shared function rather than a habit
     *  because forty-odd objects are about to need it. A bounded comparison: no
     *  allocation, no lock, no registry lookup, so it is safe on whichever thread
     *  the message was dispatched on. ``DictReferenceNames``' twin, for
     *  ``DictReferenceNames``' reason — see the addressing note on ``gArray``
     *  for why resolving an *unrecognised* name here is not available.
     */
    bool ArrayReferenceNames(const char* text, std::size_t length, const std::string& name);

    /** @brief ``ArrayReferenceNames`` over a whole ``std::string`` message. */
    inline bool ArrayReferenceNames(const std::string& text, const std::string& name) {
      return ArrayReferenceNames(text.c_str(), text.size(), name);
    }

    /**
     *  @brief Position of the first element equal to @p element, or
     *         ``store.count`` when there is none.
     *
     *  **The caller holds the store's guard.** Allocation-free and lock-free, so
     *  it is safe on whichever thread a message was dispatched on. A linear scan,
     *  which is what a sequence offers: an array has no key to index by, and the
     *  table is bounded at ``MAX_ELEMENTS``. ``array.indexof``, ``array.unique``
     *  and ``array.sect`` are each written on this.
     */
    std::size_t ArrayFind(const arrayStore& store, const char* element, std::size_t length);

    /**
     *  @brief Replace the element at @p index.
     *
     *  **The caller holds the store's guard.** Allocation-free: the string is
     *  reserved to ``ELEMENT_CAPACITY`` when the store is built.
     *
     *  False, changing nothing, when @p index names no live element or the text
     *  is empty or longer than ``ELEMENT_CAPACITY`` — refused rather than
     *  truncated, and silently, since this may be the audio thread. Reporting the
     *  refusal is the caller's job (``gArray::Dropped``).
     */
    bool ArraySetAt(arrayStore& store, std::size_t index, const char* element, std::size_t length);

    /**
     *  @brief Insert @p element at @p index, shifting the elements above it up.
     *
     *  **The caller holds the store's guard.** Allocation-free: the rows above
     *  move up by ``assign`` into storage they already own, and an @p index equal
     *  to ``store.count`` appends without moving anything.
     *
     *  False, changing nothing, when @p index is past the end, the array is full,
     *  or the text is empty or longer than ``ELEMENT_CAPACITY``.
     */
    bool ArrayInsertAt(arrayStore& store, std::size_t index, const char* element,
                       std::size_t length);

    /** @brief ``ArrayInsertAt`` at the end — the push every "add one" operation
     *         comes down to. The caller holds the guard. */
    inline bool ArrayAppend(arrayStore& store, const char* element, std::size_t length) {
      return ArrayInsertAt(store, store.count, element, length);
    }

    /**
     *  @brief Drop the element at @p index, closing the gap.
     *
     *  **The caller holds the store's guard.** Allocation-free: the rows above
     *  move down by ``assign`` into storage they already own. Out of range is a
     *  no-op. Positions above @p index all move, which is exactly what an ordered
     *  sequence means and the thing a keyed store never has to do.
     */
    void ArrayEraseAt(arrayStore& store, std::size_t index);

    /**
     *  @brief Write @p store into @p out as a JSON array of typed elements.
     *
     *  **Control thread only** — it allocates, by construction. The caller holds
     *  the store's guard.
     *
     *  A free function rather than a member so that ``array.serialize`` and the
     *  ``DumpState`` hook below share one implementation. Elements are typed on
     *  the way out by the patcher's own classifier, so an array of numbers is a
     *  JSON array of numbers and not of strings: a single numeric token becomes a
     *  JSON number spelled the way it was stored (``120`` an integer, ``120.`` a
     *  real) and anything else becomes a string.
     *
     *  This is the whole of the JSON story, and it is shorter than ``.dict``'s on
     *  purpose: a dictionary had to make a flat table nested again, where an array
     *  *is* a JSON array. The nesting question does not arise.
     */
    void ArrayToJson(const arrayStore& store, nlohmann::json::value_type& out);

    /**
     *  @brief Fill @p store from a JSON array, replacing whatever it held.
     *
     *  ``ArrayToJson``'s inverse and the reader half ``array.deserialize`` will
     *  use. **Control thread only**, and the caller holds the guard. Every JSON
     *  scalar becomes the atom that spells it — a boolean becomes ``1`` / ``0``
     *  and null is skipped.
     *
     *  A nested array or object inside the document is **skipped**, because an
     *  element is one atom (see the store's notes) and there is no atom that
     *  spells a sub-document. Elements past ``ELEMENT_CAPACITY`` and past
     *  ``MAX_ELEMENTS`` are dropped and the rest of the document still loads —
     *  ``.coll``'s rule for an over-long record, for ``.coll``'s reason.
     */
    void ArrayFromJson(const nlohmann::json::value_type& in, arrayStore& store);

    /**
     *  @brief An ordered, index-addressed sequence shared by name — ``.array``
     *         (issue #548), the second type built on the patcher's
     *         name-addressed value model.
     *
     *  Max's ``array``: a mutable collection with a length, a position for every
     *  element, and forty-odd operations over it.
     *
     *  ### The decision, which was settled at ``.dict`` and is inherited whole
     *
     *  The array (#548), string (#549) and dict (#550) epics were filed as one
     *  question — *how does a mutable, reference-passed value travel between
     *  patcher objects?* — and #550 answered it for all three: **it does not
     *  travel; its name does.**
     *
     *  A cord in this patcher is not a message object. ``OUT_TYPE`` is
     *  ``BANG / FLOAT / INT / BUFFER / LIST / ANY``, and a send is a direct
     *  synchronous call — ``SendInt(int)``, ``SendList(const std::string&)`` —
     *  with the payload passed by value or as a borrowed string the receiver must
     *  copy before it returns. There is no atom, no variant and no refcounted box
     *  anywhere in the transport, so an ``OUT_TYPE`` cannot carry an identity
     *  (``.textedit``, #560). A cord that carried a shared pointer would put
     *  reference-count traffic on ``outlet::Send*``, routinely the audio callback;
     *  it would make a value's lifetime span the ``GraphState`` swap
     *  (#226/#227), so an edit that deletes an object could free a value another
     *  object is mid-read on; and it would hand every one of the sixty-odd
     *  existing objects a payload kind they have no handler for.
     *
     *  So, exactly as ``gDict``:
     *
     *  - **Storage lives in the named registry.**
     *    ``AcquireNamedStore<arrayStore>`` (``patcher/namedStore.h``, #684) keys
     *    real storage by ``"<patcherName>.<name>"`` — the ``INTERNAL::NamedBus``
     *    address form, so an ``.array seq``, a ``.dict seq``, a ``.value seq`` and
     *    a ``.r seq`` in one patcher all speak about one word. One namespace per
     *    store type, so an ``.array notes`` and a ``.dict notes`` are different
     *    things, which is Max's arrangement too.
     *  - **The registry holds stores weakly, so ownership is exactly "whoever
     *    addresses the name".** An array lives as long as some object holds it and
     *    is freed when the last one goes. No owner object, no manual free, no way
     *    for a name a patch once spelled to leak for the life of the process.
     *  - **The name is resolved once, on the control thread**, in ``SetParent`` /
     *    ``PARM_PARSE`` / ``RefreshBinding``. ``AcquireNamedStore`` takes a mutex
     *    and may allocate, and a message handler runs on whichever thread
     *    dispatched it. After that the object holds a ``shared_ptr`` and every
     *    access is one pointer hop and one ``exchange``. **No message path ever
     *    copies or drops the ``shared_ptr``**, so no refcount operation lands on
     *    the audio thread.
     *  - **What a cord carries is the message ``array <name>``** — the *local*
     *    name, not the resolved address, because the receiving object prefixes it
     *    with its own patcher's name exactly as this one does. Out of outlet 1 on
     *    a bang, which is what feeds the ``array.*`` family.
     *  - **An ``array.*`` object binds from its creation argument.** An
     *    ``array <name>`` message on an inlet is honoured only when it names the
     *    array already bound (``ArrayReferenceNames``, a bounded compare) and is
     *    otherwise refused and counted. Re-pointing at a *different* name from a
     *    message is not portable, for the reason ``.coll`` gives for not porting
     *    Max's ``refer``: the lookup is a mutex. An operation that genuinely needs
     *    run-time rebinding uses ``clockBridge``'s shape (#688) — claim a slot
     *    from a fixed table with one CAS, copy the name inline, and let a
     *    pre-allocated background-pool job do the registry lookup and publish with
     *    a release store.
     *  - **The named bus stays a value bus and carries the name, never the
     *    contents.** ``INTERNAL::NamedBus`` hands a copy of a published payload to
     *    every subscriber, holds no storage and owns no lifetime, and its
     *    audio-path queue entry is a fixed POD that drops anything that is not an
     *    int or a float from ``T_DSP``. That is unchanged: a ``.s`` fed from this
     *    object's reference outlet publishes the symbol ``array <name>``, and
     *    whatever wants the contents binds that name on the control thread.
     *
     *  ### What this type adds that ``.dict`` did not
     *
     *  Order. A dictionary is reached by a key and a key is a whole address; an
     *  array is reached by a **position**, and a position moves when the elements
     *  in front of it move. Three consequences are decided here for the whole
     *  family rather than per object:
     *
     *  - **An element is one atom**, so that the array and the list text it
     *    spells are the same thing seen twice. The argument is on ``arrayStore``.
     *  - **An index is refused when it is out of range**, never wrapped, clamped
     *    or counted from the end; negative indices are refused. ``array.wrap`` and
     *    ``array.rotate`` are the objects that exist to provide the alternatives.
     *  - **``insert`` and ``delete`` renumber**, which is what makes an operation
     *    that walks an array while another object writes to it a real hazard where
     *    a dictionary walk is only a stale read. An object that iterates keeps its
     *    own cursor (``.coll``'s per-object pointer rule) and says on its own issue
     *    what a concurrent write does to the walk.
     *
     *  ### The base object's messages, and why they overlap the family
     *
     *  ``.array`` answers ``set``, ``get``, ``append``, ``insert``, ``delete``,
     *  ``clear``, ``getsize`` and ``getvalue``; a bang emits the reference. The
     *  growing and shrinking are here rather than left entirely to
     *  ``array.push`` / ``array.pop`` / ``array.insert`` / ``array.remove``
     *  because a container that cannot change length is a table, not an array:
     *  without them the type could not be exercised, or tested, at all. It is the
     *  same overlap Max itself has — ``dict`` answers ``set`` and ``get`` while a
     *  ``dict.*`` family exists — and the same one ``.dict`` landed with. The
     *  family objects are the *patching* form: they take a reference, act on a
     *  bang, and report through outlets of their own.
     *
     *  ### An unnamed ``.array`` is private
     *
     *  Not "shares the empty name". ``"<patcherName>."`` is a real, reachable
     *  address, so unnamed objects pooling on it would silently connect two arrays
     *  a patch author never wired together. ``.value``'s rule, for ``.value``'s
     *  reason. An ``.array`` with no parent yet is private for the same reason —
     *  there is no patcher name to prefix with until ``SetParent``.
     *
     *  ### What persists, and who restores it
     *
     *  The **contents**, through ``pObject::DumpState`` / ``RestoreState``, as a
     *  JSON array under the object's ``state`` key. Every bound ``.array`` writes
     *  them and only the object that **created** the store reads them back —
     *  ``.coll``'s rule (#684), and for ``.coll``'s reasons: a single nominated
     *  writer would stop saving the day that object was deleted from the patch,
     *  and a patcher loaded into an engine where its name is already live must
     *  join the running array rather than reset it under whatever is using it.
     *
     *  The serialisation goes through the layer ``DumpJSON`` / ``ParseJSON``
     *  actually runs on, which is **nlohmann::json** — the same
     *  ``nlohmann::json::value_type`` every other object's state hook takes.
     *  (#548 names ``YseEngine/json/cJSON.cpp``; that file is vendored for the
     *  engine's own use and the patcher does not go through it. Routing array
     *  state through cJSON would *add* the second JSON implementation the issue
     *  asks not to add, not avoid it.)
     *
     *  ### Real-time behaviour
     *
     *  ``Calculate()`` does nothing. The object is driven by its inlet, and an
     *  emitting ``Calculate()`` would re-send on every DSP tick — the rule
     *  ``.value``, ``.coll``, ``.dict``, ``.route`` and ``.sel`` establish.
     *
     *  No message path allocates, locks or blocks. The name is resolved on the
     *  control thread; a store is a bounded ``assign`` into a pre-reserved string;
     *  a fetch copies out under the guard into a scratch reserved at construction
     *  and sends after releasing it; ``getvalue`` collects into an ``AtomList``
     *  member under the guard and renders after releasing it; the reference
     *  message is built once per rebind, so a bang is a ``SendList`` of a string
     *  the object already owns.
     */
    PATCHER_CLASS(gArray, YSE::OBJ::G_ARRAY)
    _NO_MESSAGES
    _NO_CALCULATE

    _BANG_IN(BangIn)
    _LIST_IN(ListIn)

    _PARM_CLEAR
    _PARM_PARSE

    /** @brief Most elements the array holds — ``arrayStore::MAX_ELEMENTS``. */
    static constexpr std::size_t MAX_ELEMENTS = arrayStore::MAX_ELEMENTS;

    /** @brief Longest element, in characters. */
    static constexpr std::size_t ELEMENT_CAPACITY = arrayStore::ELEMENT_CAPACITY;

    /** @brief The shared name, or empty when this ``.array`` has a store of its
     *         own. The first creation argument. */
    const std::string& ArrayName() const {
      return arrayName;
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

    /** @brief The message a bang emits out outlet 1 — ``"array <name>"``, or
     *         empty for an unnamed array, which has no name to pass on. */
    const std::string& Reference() const {
      return reference;
    }

    /** @brief How many elements the array holds — its length. */
    std::size_t Count() const {
      return store->count;
    }

    /** @brief The element at @p index, or empty when there is none.
     *         Diagnostics and tests; allocates, so control thread only. */
    std::string ElementAt(std::size_t index) const;

    /**
     *  @brief Messages refused so far — an index out of range, an over-long
     *         element, a push onto a full array, or a lost try-lock.
     *
     *  A counter rather than a log line because the refusing thread may be the
     *  audio callback; ``.zl``'s ``Dropped()``, for ``.zl``'s reason.
     */
    std::uint64_t Dropped() const {
      return dropped.load(std::memory_order_relaxed);
    }

    // Bind to the store the moment the patcher name is known, so a message never
    // resolves a name. gValue::SetParent's rule.
    void SetParent(pObject* parent) override;

    // Re-bind after a patcher rename: the address prefix moved, so the object now
    // addresses a different array. Called from patcherImplementation::SetName
    // alongside gValue::RefreshBinding, gColl::RefreshBinding and
    // gDict::RefreshBinding.
    void RefreshBinding();

    // The contents, as a JSON array. See the class notes on who writes and who
    // restores. Control thread only, both of them.
    void DumpState(nlohmann::json::value_type& json) override;
    void RestoreState(const nlohmann::json::value_type& json) override;

  private:
    // Point at the store the current name and parent address, creating it if this
    // is the first .array to name it. Control thread only (SetParams / SetParent
    // / SetName). A no-op when the address has not changed, so the contents
    // survive a re-parse that leaves the name alone.
    void Rebind();

    // The message handlers, each taking the guard itself and each releasing it
    // before any send.
    void HandleSet(const std::string& text, std::size_t argOffset);
    void HandleGet(const std::string& text, std::size_t argOffset, YSE::THREAD thread);
    void HandleAppend(const std::string& text, std::size_t argOffset);
    void HandleInsert(const std::string& text, std::size_t argOffset);
    void HandleDelete(const std::string& text, std::size_t argOffset);
    void HandleClear();
    void HandleGetSize(YSE::THREAD thread);
    void HandleGetValue(YSE::THREAD thread);

    // Rebuild `reference` from the current name. Control thread only.
    void RefreshReference();

    void Refuse() {
      dropped.fetch_add(1, std::memory_order_relaxed);
    }

    // The shared name. First creation argument; empty means a private store.
    std::string arrayName;

    // The address `store` is registered under, or empty while it is private. Also
    // the "has the binding changed?" key Rebind() compares against.
    std::string boundAddress;

    // The array. Never null once the object has been constructed, so no message
    // handler needs a null check.
    std::shared_ptr<arrayStore> store;

    // Whether this object brought `store` into existence — what decides whether
    // it restores saved contents into it. `.coll`'s rule.
    bool createdStore = true;

    // "array <name>", built once per rebind so a bang is a send of a string the
    // object already owns rather than a concatenation on whichever thread the
    // bang arrived on.
    std::string reference;

    // Where a fetched element is copied to while the guard is held, so the send
    // can happen after the guard is released. A plain array rather than a string
    // because it is written from inside the critical section, where an assign
    // that had to grow would be the one thing that must not happen.
    char fetched[arrayStore::ELEMENT_CAPACITY + 1] = {};
    std::size_t fetchedLength = 0;

    // Render buffer for the outlet, reserved to AtomList::RENDER_CAPACITY at
    // construction.
    std::string emitScratch;

    // Working list for `getvalue`, filled under the guard and rendered after it
    // is released. An AtomList rather than a string because it carries the
    // patcher's own bound on how much list text may travel down a cord — an array
    // whose elements together outrun AtomList::TEXT_CAPACITY loses its tail and
    // the refusal is counted, rather than the send silently allocating.
    AtomList emitList;

    // Refusals, published for tests and diagnostics.
    std::atomic<std::uint64_t> dropped{0};
  };
} // namespace PATCHER
} // namespace YSE
