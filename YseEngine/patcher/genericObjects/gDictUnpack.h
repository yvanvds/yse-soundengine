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
     *  @brief Output a dictionary's values on separate outlets — Max's
     *         ``dict.unpack`` on the name-addressed value model ``.dict``
     *         settled (issue #781).
     *
     *  The dictionary counterpart of ``.unpack``, and ``.dict.pack``'s
     *  inverse: a structure arriving as one reference becomes several
     *  separate values a patch can wire individually. The natural consumer
     *  for every other object in the ``dict.*`` family.
     *
     *  ### The arguments are the name, then the shape
     *
     *  A dictionary never travels down a cord — an ``OUT_TYPE`` carries a
     *  value, not an identity (see gDict.h for the whole argument) — so the
     *  dictionary is **bound from the first creation argument**, on the
     *  control thread: ``.dict.unpack <name> <path> [<path> ...]`` holds one
     *  ``shared_ptr<dictStore>`` resolved in ``SetParent`` / ``PARM_PARSE``
     *  / ``RefreshBinding``, exactly as the rest of the family resolves
     *  theirs.
     *
     *  Every argument after the name is a **key path** — ``.dict.unpack in
     *  freq gain voice::1::freq`` — and the outlet count comes from that
     *  list: one outlet per path, each outlet reading one path. The
     *  conventions are ``.dict.pack``'s exactly, read in the opposite
     *  direction, so the pair reads as a pair: paths nest with ``::``,
     *  Max's own separator; a path past ``KEY_CAPACITY`` (128) characters
     *  declares no outlet and is logged — parameter parsing is
     *  control-thread only, so a refusal can be *said* here where a message
     *  path could only count it; so is a key list past ``MAX_ENTRIES``
     *  (256), which is clamped. A duplicate path is two outlets reading one
     *  entry, and both fire. With no key arguments at all the object has no
     *  outlets and a trigger reads nothing — there is no default key to
     *  invent, ``.dict.route``'s reasoning.
     *
     *  ### What arrives, and what leaves
     *
     *  - **A bang unpacks**: each key path is looked up in the bound
     *    dictionary and its value leaves its outlet, **right to left** —
     *    ``gUnpack``'s ordering, Max's universal order, each send completing
     *    in full (the whole subgraph behind that outlet) before the next one
     *    starts, which is what lets the right-hand values land in cold
     *    inlets before the leftmost one sets the result off.
     *  - **``dictionary <name>`` on the inlet unpacks too** — the message a
     *    ``.dict``'s reference outlet emits on a bang and ``.dict.pack``
     *    emits on a pack, so wiring either here gives the pair gesture:
     *    pack on one side, and the values pop out the other. Honoured only
     *    when it names the dictionary already bound — ``DictReferenceNames``,
     *    the bounded compare — and refused and counted otherwise, because
     *    resolving an unrecognised name means the registry's mutex on
     *    whatever thread the message arrived on. Re-pointing at a different
     *    name from a message is not portable, for the reason gDict.h gives
     *    for not porting ``refer``.
     *
     *  ### A miss sends nothing
     *
     *  A path the dictionary does not hold sends nothing on that outlet
     *  rather than a zero — ``.dict``'s miss rule: a fabricated 0 would read
     *  downstream as a value the dictionary held. An entry holding nothing
     *  (``set <path>`` stored no value) also sends nothing, ``.dict``'s own
     *  rule for a ``get`` of an empty value — an object with nothing to say
     *  says nothing. Only the outlets whose paths the dictionary holds with
     *  a value fire, and they still fire right to left among themselves.
     *
     *  ### What leaves is a value, typed by its spelling
     *
     *  Each outlet sends through the family's shared ``SendAtom``: a stored
     *  value spelling a single number leaves as the **int** or **float** it
     *  spells, and anything else — a symbol, or a whole list, since a
     *  dictionary value is list text — leaves as list text, verbatim. That
     *  is ``.dict.pack``'s "a list is a value, not a spread" read backwards:
     *  the array ``gain`` holds leaves the ``gain`` outlet as one list
     *  message, not as a scatter.
     *
     *  ### The read is snapshotted
     *
     *  Each send runs the whole downstream subgraph before the next one
     *  starts, and that subgraph may well ``set`` into this very dictionary
     *  — the store cannot be guarded across an outlet send (see
     *  ``dictStoreGuard``). So the trigger copies every path's value out
     *  under the store's guard into slots the object sized at parse time,
     *  releases, and sends from the slots: what leaves is the dictionary as
     *  it stood at the trigger, every outlet consistent with every other,
     *  and no two dict guards ever nest. ``gDictIter``'s snapshot, for
     *  ``gDictIter``'s reason. A trigger that loses the store's try-lock is
     *  refused whole and counted — no outlet fires with half a snapshot.
     *
     *  ### Re-entrancy
     *
     *  The object emits in a loop, so a cord from an outlet back to its
     *  inlet — directly or round a chain — re-enters the handler from
     *  inside the walk, and letting it through would rewrite the very
     *  slots being sent. A single test-and-set guard is held across
     *  snapshot and sends, and a trigger that finds it taken — the
     *  loop-back, or another thread — is refused and counted rather than
     *  made to spin, the family's guard for the family's reason.
     *
     *  ### Live SetParams (#234)
     *
     *  The name and the key paths are STRING/LIST parameters, so
     *  ``Parameters::NeedsRebuild()`` is true and a live re-parse takes the
     *  structural-replacement route. Right rather than a limitation: the
     *  arguments *are* the outlet count and the entry shape, so re-typing
     *  them is re-typing the object — ``.dict.pack``'s reasoning.
     *
     *  ### Real-time behaviour
     *
     *  ``Calculate()`` does nothing — the object is driven by its inlet,
     *  the rule ``.value``, ``.coll`` and ``.dict`` establish. No message
     *  path allocates, locks or blocks: the name is resolved on the control
     *  thread, the snapshot is a bounded lookup and copy per path into
     *  slots sized at parse time, and each send renders through ``SendAtom``
     *  into a buffer reserved at construction.
     */
    PATCHER_CLASS(gDictUnpack, YSE::OBJ::G_DICT_UNPACK)
    _NO_MESSAGES
    _NO_CALCULATE

    _BANG_IN(BangIn)
    _LIST_IN(ListIn)

    _PARM_CLEAR
    _PARM_PARSE

    /** @brief Most key paths — and so most outlets — the object will build:
     *         ``dictStore::MAX_ENTRIES``, because each outlet reads one
     *         entry. A longer argument list is clamped, which the control
     *         thread logs. ``.dict.pack``'s bound, so the pair stays a
     *         pair. */
    static constexpr std::size_t MAX_KEYS = dictStore::MAX_ENTRIES;

    /** @brief Longest key path, in characters. */
    static constexpr std::size_t KEY_CAPACITY = dictStore::KEY_CAPACITY;

    /** @brief Longest stored value, in characters. */
    static constexpr std::size_t VALUE_CAPACITY = dictStore::VALUE_CAPACITY;

    /** @brief The dictionary's shared name — the first creation argument,
     *         or empty for a private (empty) dictionary. */
    const std::string& DictName() const {
      return dictName;
    }

    /** @brief The address the store is registered under —
     *         ``"<patcherName>.<name>"`` — or empty while it is private. */
    const std::string& Address() const {
      return boundAddress;
    }

    /** @brief How many key paths — and outlets — the object built. */
    std::size_t KeyCount() const {
      return keys.size();
    }

    /** @brief The key path of outlet @p index, or empty out of range.
     *         Diagnostics and tests; control thread only. */
    std::string KeyAt(std::size_t index) const;

    /** @brief Triggers that completed a snapshot and sent what it held.
     *         Diagnostics and tests. */
    std::uint64_t Unpacked() const {
      return unpacked.load(std::memory_order_relaxed);
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
    // object now reads a different dictionary. Called from
    // patcherImplementation::SetName alongside gDict::RefreshBinding.
    void RefreshBinding();

  private:
    // One value slot per key path — where the snapshot lands. A fixed
    // array rather than a string because it is written on the message path,
    // where an assign that had to grow would be the one thing that must not
    // happen; the vector itself is sized by ShapePorts, on the control
    // thread, before the object is wired or published. gDictPack's slot,
    // plus the hit flag a reader needs: a miss and an empty value both send
    // nothing, but only the former is a path the dictionary does not hold.
    struct fetchSlot {
      char text[dictStore::VALUE_CAPACITY + 1] = {};
      std::size_t length = 0;
      bool present = false;
    };

    // Rebuild outlets, validated key list and value slots from the creation
    // arguments. Control thread only: the constructor and the two parameter
    // callbacks, all of which run before the object is wired or published —
    // a *live* SetParams never reaches here on a published object, because
    // registering the callbacks makes ParamsNeedRebuild() true and #234
    // replaces the object instead. gDictPack::ShapePorts' rule.
    void ShapePorts();

    // Point the store at the current name and parent address. Control
    // thread only (SetParams / SetParent / SetName). A no-op when the
    // address has not changed.
    void Rebind();

    // The unpack itself: snapshot every path's value under the store's
    // guard, release, send the hits right to left through SendAtom.
    // Refuses (counted) on a lost guard or a re-entrant trigger.
    void Unpack(YSE::THREAD thread);

    void Refuse() {
      dropped.fetch_add(1, std::memory_order_relaxed);
    }

    // The shared name. First creation argument; empty means a private,
    // empty dictionary.
    std::string dictName;

    // The key paths as typed — everything after the name. LIST parameter,
    // control thread only.
    std::vector<std::string> keyArgs;

    // The validated key paths, parallel to `slots` and to the outlets.
    // Built by ShapePorts and never resized by a message handler; a
    // message path only ever reads the characters.
    std::vector<std::string> keys;

    // Where the snapshot lands, one slot per key path.
    std::vector<fetchSlot> slots;

    // The address `store` is registered under, or empty while it is
    // private. Also the "has the binding changed?" key Rebind() compares
    // against.
    std::string boundAddress;

    // The dictionary the unpack reads. Never null once the object has been
    // constructed, so no message handler needs a null check.
    std::shared_ptr<dictStore> store;

    // Where a send is rendered on SendAtom's symbol path. Reserved by the
    // constructor to AtomList::RENDER_CAPACITY, so sending allocates
    // nothing.
    std::string render;

    // The re-entrancy guard, held across snapshot and sends: a trigger
    // looping back from an outlet, or arriving from another thread
    // mid-walk, would rewrite the slots being sent. The loser is dropped
    // and counted rather than made to spin — the family's guard, for the
    // family's reason.
    std::atomic<bool> busy{false};

    std::atomic<std::uint64_t> unpacked{0};
    std::atomic<std::uint64_t> dropped{0};
  };
} // namespace PATCHER
} // namespace YSE
