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
     *  @brief Build a dictionary from a list of named inlets — Max's
     *         ``dict.pack`` on the name-addressed value model ``.dict``
     *         settled (issue #775).
     *
     *  The dictionary counterpart of ``.pack``: several separate values in a
     *  patch become one addressable structure, so a set of controls can be
     *  sent, saved or compared as a unit. The natural producer for every
     *  other object in the ``dict.*`` family.
     *
     *  ### The arguments are the name, then the shape
     *
     *  A dictionary never travels down a cord — an ``OUT_TYPE`` carries a
     *  value, not an identity (see gDict.h for the whole argument) — so what
     *  Max's ``dict.pack`` *outputs* has to be a dictionary this object is
     *  **bound to by name**: the first creation argument, resolved on the
     *  control thread in ``SetParent`` / ``PARM_PARSE`` / ``RefreshBinding``
     *  exactly as ``gDict`` and its siblings resolve theirs, because the
     *  result cannot go into a new anonymous dictionary — there is no way to
     *  hand a fresh dictionary's identity down a cord (``gDictGroup``'s
     *  rule).
     *
     *  Every argument after the name is a **key path** — ``.dict.pack out
     *  freq gain voice::1::freq`` — and the inlet count comes from that list,
     *  ``gPack``'s argument-driven shape: one inlet per path, each inlet
     *  writing one path. Paths nest with ``::``, Max's own separator, so the
     *  packed dictionary is as structured as its keys spell. A path past
     *  ``KEY_CAPACITY`` (128) characters declares no inlet and is logged —
     *  parameter parsing is control-thread only, so a clamp can be *said*
     *  here where a message path could only count it. So is a key list past
     *  ``MAX_ENTRIES`` (256), which is clamped. A duplicate path is two
     *  inlets writing one entry, and the later inlet wins the pack —
     *  ``DictStoreAt`` replaces an entry of the same path. With no key
     *  arguments at all the object is a bare trigger: a bang packs the
     *  empty dictionary and emits its reference.
     *
     *  ### One hot inlet, and what the cold ones are for
     *
     *  ``gPack``'s rule, exactly: only inlet 0 releases. A value arriving on
     *  any other inlet is stored in its slot and stays quiet, so a patch
     *  loads the right-hand values first and lets the leftmost one carry the
     *  finished dictionary out. A **bang** (inlet 0 only, where Max documents
     *  it) packs and emits without storing, and ``set <value>`` performs
     *  exactly the store the same message without the word would have
     *  performed while suppressing only the release — one storage rule, so
     *  the two paths cannot drift apart.
     *
     *  ### The pack, and what leaves
     *
     *  A trigger **replaces the bound dictionary whole**: one entry per key
     *  path, in argument order, each holding its slot's current value — a
     *  slot never written packs its path with an empty value, the shape the
     *  arguments declared rather than a gap. Replacing whole makes the pack
     *  idempotent and keeps a stale entry from an earlier run — or another
     *  writer — out of the packed unit, ``gDictJoin``'s rule for its target.
     *  Then the reference ``dictionary <name>`` leaves outlet 0, consistent
     *  with ``.dict``'s own reference outlet, so the rest of the family can
     *  pick the result up. Silent for an unnamed dictionary, which has no
     *  name to pass on — the pack still lands in the private store.
     *
     *  ### A list is a value, not a spread
     *
     *  Where ``.pack`` spreads a multi-item message rightwards one item per
     *  slot, here a whole list is **one value**: a stored value is list text
     *  (``dictEntry``'s note), so ``0.2 0.5 0.9`` arriving at the ``gain``
     *  inlet is the array ``gain`` holds, exactly as a list is a value to
     *  ``dict.pack`` in Max. Ints and floats are rendered through the
     *  patcher's one number formatter into a stack buffer and stored through
     *  the same path, verbatim — the arguments are key paths, not types, so
     *  there is no slot typing to enforce.
     *
     *  ### References on inlets
     *
     *  Max's ``dict.pack`` embeds a dictionary arriving at an inlet as a
     *  subdictionary. That needs a registry resolve on a message path — a
     *  mutex on whatever thread the message arrived on — so it is not
     *  portable (the ``refer`` argument in gDict.h). What remains is the
     *  bounded compare: ``dictionary <name>`` naming the dictionary already
     *  bound **triggers the pack on inlet 0** — the message a ``.dict``'s
     *  reference outlet emits on a bang, so wiring it here gives a
     *  re-emitting gesture — and is **acknowledged silently on a cold
     *  inlet**, so a reference wired across is not counted as an error. A
     *  reference naming anything else is refused and counted on every
     *  inlet: an identity is not a value, and storing the text would bury a
     *  mis-wire in the data.
     *
     *  ### Refusal, never truncation
     *
     *  A value past ``VALUE_CAPACITY`` (256) characters is refused whole and
     *  counted — the slot keeps what it had rather than a prefix nobody
     *  sent. A message that finds the object busy — another thread, or this
     *  object's outlet wired back into an inlet — is dropped and counted
     *  rather than made to spin, and the same test-and-set guard is what
     *  bounds that feedback loop. A pack that loses the store's try-lock is
     *  refused whole and counted. Counters rather than log lines, because
     *  the refusing thread may be the audio callback.
     *
     *  ### Live SetParams (#234)
     *
     *  The name and the key paths are STRING/LIST parameters, so
     *  ``Parameters::NeedsRebuild()`` is true and a live re-parse takes the
     *  structural-replacement route. Right rather than a limitation: the
     *  arguments *are* the inlet count and the entry shape, so re-typing
     *  them is re-typing the object — ``gPack``'s reasoning.
     *
     *  ### Real-time behaviour
     *
     *  ``Calculate()`` does nothing — the object is driven by its inlets,
     *  the rule ``.value``, ``.coll`` and ``.dict`` establish. No message
     *  path allocates, locks or blocks: the name is resolved on the control
     *  thread, a store is a bounded copy into a slot sized at parse time,
     *  the pack is bounded ``assign``s into the store's pre-reserved rows
     *  under its guard alone — released before the send, which runs the
     *  whole downstream graph — and the reference is built once per rebind
     *  so the send is of a string the object already owns.
     */
    PATCHER_CLASS(gDictPack, YSE::OBJ::G_DICT_PACK)
    _NO_MESSAGES
    _NO_CALCULATE

    _BANG_IN(BangIn)
    _INT_IN(IntIn)
    _FLOAT_IN(FloatIn)
    _LIST_IN(ListIn)

    _PARM_CLEAR
    _PARM_PARSE

    /** @brief Most key paths — and so most inlets — the object will build:
     *         ``dictStore::MAX_ENTRIES``, because the pack writes one entry
     *         per path. A longer argument list is clamped, which the control
     *         thread logs. */
    static constexpr std::size_t MAX_KEYS = dictStore::MAX_ENTRIES;

    /** @brief Longest key path, in characters. */
    static constexpr std::size_t KEY_CAPACITY = dictStore::KEY_CAPACITY;

    /** @brief Longest stored value, in characters. */
    static constexpr std::size_t VALUE_CAPACITY = dictStore::VALUE_CAPACITY;

    /** @brief The packed dictionary's shared name — the first creation
     *         argument, or empty for a private store. */
    const std::string& DictName() const {
      return dictName;
    }

    /** @brief The address the store is registered under —
     *         ``"<patcherName>.<name>"`` — or empty while it is private. */
    const std::string& Address() const {
      return boundAddress;
    }

    /** @brief How many key paths — and key inlets — the object built. */
    std::size_t KeyCount() const {
      return keys.size();
    }

    /** @brief The key path of slot @p index, or empty out of range.
     *         Diagnostics and tests; control thread only. */
    std::string KeyAt(std::size_t index) const;

    /** @brief The value slot @p index holds right now, or empty.
     *         Diagnostics and tests; allocates, so control thread only. */
    std::string ValueAt(std::size_t index) const;

    /** @brief The bound store's value at @p path, or empty when there is
     *         none — what the last pack wrote. Diagnostics and tests;
     *         allocates, so control thread only. */
    std::string Lookup(const std::string& path) const;

    /** @brief How many entries the bound store holds. Diagnostics and
     *         tests. */
    std::size_t Count() const {
      return store->count;
    }

    /** @brief The message a trigger emits — ``"dictionary <name>"``, or
     *         empty for an unnamed dictionary, which has no name to pass
     *         on. */
    const std::string& Reference() const {
      return reference;
    }

    /**
     *  @brief Messages and entries refused so far — an over-long value, a
     *         reference naming a dictionary this object is not bound to, a
     *         value arriving where no key inlet exists, a lost try-lock, or
     *         a message that found the object busy.
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
    // object now packs into a different dictionary. Called from
    // patcherImplementation::SetName alongside gDict::RefreshBinding.
    void RefreshBinding();

  private:
    // One value slot per key path. A fixed array rather than a string
    // because it is written on the message path, where an assign that had
    // to grow would be the one thing that must not happen; the vector
    // itself is sized by ShapePorts, on the control thread, before the
    // object is wired or published.
    struct packSlot {
      char text[dictStore::VALUE_CAPACITY + 1] = {};
      std::size_t length = 0;
    };

    // Rebuild inlets, outlet, validated key list and value slots from the
    // creation arguments. Control thread only: the constructor and the two
    // parameter callbacks, all of which run before the object is wired or
    // published — a *live* SetParams never reaches here on a published
    // object, because registering the callbacks makes ParamsNeedRebuild()
    // true and #234 replaces the object instead. gPack::ShapePorts' rule.
    void ShapePorts();

    // Put the stored documentation on the ports as they are now. Called by
    // ShapePorts, so a re-parse cannot leave a port undocumented.
    void ApplyDocs();

    // Point the store at the current name and parent address. Control
    // thread only (SetParams / SetParent / SetName). A no-op when the
    // address has not changed, so the contents survive a re-parse that
    // leaves the name alone.
    void Rebind();

    // Rebuild `reference` from the current name. Control thread only.
    void RefreshReference();

    // Take the guard, or count a drop and answer false. The loser of a race
    // and a feedback loop take the same route — gPack's Enter/Leave.
    bool Enter();
    void Leave();

    // Store one value arriving at `inlet`, and pack-and-emit when `emit`.
    // The one path every value handler funnels through, guard included.
    void Take(const char* text, std::size_t length, int inlet, bool emit, YSE::THREAD thread);

    // The pack itself, called with the object's guard held: replace the
    // bound store whole — one entry per key path, current slot values —
    // under its guard alone, then send the reference after it is released.
    void Pack(YSE::THREAD thread);

    void Refuse() {
      dropped.fetch_add(1, std::memory_order_relaxed);
    }

    // The shared name. First creation argument; empty means a private
    // store.
    std::string dictName;

    // The key paths as typed — everything after the name. LIST parameter,
    // control thread only.
    std::vector<std::string> keyArgs;

    // The validated key paths, parallel to `slots` and to the inlets. Built
    // by ShapePorts and never resized by a message handler; a message path
    // only ever reads the characters.
    std::vector<std::string> keys;

    // The values as they stand, one slot per key path.
    std::vector<packSlot> slots;

    // The address `store` is registered under, or empty while it is
    // private. Also the "has the binding changed?" key Rebind() compares
    // against.
    std::string boundAddress;

    // The dictionary the pack writes. Never null once the object has been
    // constructed, so no message handler needs a null check.
    std::shared_ptr<dictStore> store;

    // "dictionary <name>", built once per rebind so a trigger is a send of
    // a string the object already owns rather than a concatenation on
    // whichever thread it arrived on.
    std::string reference;

    // The guard. A message that finds it taken is dropped and counted
    // rather than made to spin, this being a path the audio callback takes;
    // it is also what stops an object wired back into its own inlet from
    // recursing on the audio thread. gPack's guard, for gPack's reason.
    std::atomic<bool> busy{false};

    // Refusals, published for tests and diagnostics.
    std::atomic<std::uint64_t> dropped{0};
  };
} // namespace PATCHER
} // namespace YSE
