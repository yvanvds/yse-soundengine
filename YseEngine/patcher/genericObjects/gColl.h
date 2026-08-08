#pragma once
#include "../pObject.h"
#include <atomic>
#include <cstddef>
#include <string>
#include <vector>

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief A keyed collection of messages — ``.coll`` (issue #494).
     *
     *  Max's ``coll``, "store and edit a collection of different messages". The
     *  general-purpose data store of Max patching: presets, note tables,
     *  mapping curves and sequences all live in one. Until this object the
     *  patcher could not hold **more than one value** anywhere — ``.value``,
     *  ``.f``, ``.i`` and ``.bucket`` each hold a single thing — so a patch that
     *  wanted a table had to spell it out as one object per entry.
     *
     *  ### The address model
     *
     *  An address is a **number** or a **symbol**, decided the way the whole
     *  patcher decides it: a token the strict ``ReadNumericToken`` reads as one
     *  whole finite number is numeric (truncated to an int, Max's "a float is
     *  converted to an int"), and anything else is a symbol. The two never
     *  collide — the address ``1`` and the address ``one`` are different
     *  entries — and a numeric address does not have to be contiguous or in
     *  order, exactly as in Max.
     *
     *  Entries keep **storage order**, which is what makes ``dump``, ``next``
     *  and ``prev`` mean anything: Max's ``dump`` sends them "in the order in
     *  which they are stored", not in address order, and a patch that stored a
     *  sequence gets that sequence back.
     *
     *  ### Reserved words, and why this object gets to have them
     *
     *  ``store``, ``insert``, ``append``, ``remove``, ``delete``, ``clear``,
     *  ``length``, ``goto``, ``start``, ``end``, ``next``, ``prev`` and ``dump``
     *  are read as commands when they are the first item of a message, so a
     *  symbol address spelled as one of them cannot be reached by writing it
     *  bare. That is Max's own contract for ``coll`` and not a shortcut here:
     *  ``coll`` is the one object in the family whose inlet is a *command*
     *  inlet rather than a data inlet, which is precisely why the
     *  ``.prepend`` / ``.atoi`` discipline — never reserve a word on an inlet
     *  that has to carry arbitrary text — does not apply to it. The data an
     *  entry holds is never parsed for commands, only the leading item of the
     *  incoming message is.
     *
     *  What a bare message means, when it is not a command:
     *
     *  - a **number** (int, float, or a one-token list) recalls the entry at
     *    that numeric address;
     *  - a **symbol** recalls the entry at that symbol address — Max's
     *    ``symbol`` and ``anything`` methods, which both "retrieve a message
     *    stored at the address named by the symbol". Items after the leading
     *    symbol are ignored, as they are in Max; ``store`` is how a symbol
     *    address is *written*;
     *  - a **list whose first item is a number** stores the rest of the list at
     *    that numeric address — Max's ``list`` method, "the first value is used
     *    as the address at which to store the remaining items";
     *  - a **bang** outputs the entry at the pointer.
     *
     *  ### The outlets
     *
     *  Max has four; this has the three that mean anything without file I/O.
     *  Outlet 0 is the data, outlet 1 the address, and outlet 2 bangs when a
     *  ``dump`` has finished — Max's first, second and fourth. Max's third
     *  ("sent out when coll has finished reading in a file") has nothing to
     *  fire here, since reading and writing collection files is deliberately
     *  out of scope, and when that lands its outlet will be **appended** rather
     *  than inserted in Max's position, so no saved patch's cords shift.
     *
     *  The address only leaves when Max says it does: on ``bang``, ``dump``,
     *  ``next`` and ``prev`` — "the address is sent out whenever a message out
     *  the 1st outlet is triggered by bang, dump, next, prev, or sub" — and not
     *  on a plain lookup, which answers with the data alone. Address before
     *  data, which is Max's right-to-left outlet order and the order
     *  ``.trigger`` and ``.bucket`` already fire in.
     *
     *  What leaves the data outlet is the stored message **in the kind it is**,
     *  the rule ``.route`` establishes: a stored ``60 100`` leaves as a list, a
     *  stored ``60`` as the int 60, a stored ``60.5`` as that float, and a
     *  stored lone symbol as a one-element list. Max instead prefixes a lone
     *  symbol with the word ``symbol``; that is Max's way of restoring an atom
     *  type this patcher does not have, and inventing a word here would put a
     *  token in the message that nothing downstream asked for.
     *
     *  ### Why the store is bounded, and why the mandate is not a COW swap
     *
     *  256 entries, each holding an address of at most 64 characters and a
     *  message of at most 256 — the same 256 that bounds ``.atoi`` / ``.itoa``
     *  and the patcher's own value queue
     *  (``patcherImplementation::kValueListCap``), so anything that can reach
     *  this object through a patch also fits in it. The whole table is
     *  allocated once, at construction, and never resized: a ``store`` writes
     *  into storage that already exists.
     *
     *  That bound is what makes the object real-time safe, and it is why the
     *  literal reading of issue #494's mandate — publish a new snapshot the way
     *  ``GraphState`` is published (#226/#227) — is the wrong model here. A
     *  copy-on-write publish assumes the *writer* is the control thread. A
     *  ``.coll`` is written by whichever thread the message arrived on, and
     *  in-patcher delivery dispatches on **T_DSP**, so a ``store`` reaching this
     *  object from a rendering graph would have to allocate a replacement table
     *  on the audio thread. Bounded storage plus non-blocking exclusion gives
     *  the property the mandate is actually after — no allocation, no lock, no
     *  I/O on any path — without that.
     *
     *  Exclusion is ``.value``'s: ``busy`` is claimed with a single
     *  ``exchange`` and whoever loses **drops** its operation rather than
     *  spinning. A ``std::mutex`` is out (nothing on an audio path blocks) and
     *  a seqlock cannot work for a writer. The guard is never held across a
     *  send: everything an outlet needs is copied into buffers reserved at
     *  construction, the guard is released, and only then does the message go
     *  out — so a patch that wires an outlet back into this object's inlet
     *  finds the store free rather than dropping its own message. ``dump``
     *  takes the guard once per entry for the same reason.
     *
     *  ``Calculate()`` does nothing: the object is driven by its inlet, and one
     *  that emitted would re-send on every DSP tick — the rule ``.route``,
     *  ``.sel``, ``.value`` and ``.atoi`` establish.
     *
     *  ### What persists
     *
     *  The **contents**, through ``DumpJSON`` / ``ParseJSON``. They cannot ride
     *  the parameter string, which is what an object was *created* with rather
     *  than what it has since been told, so ``.coll`` is the first object to use
     *  ``pObject::DumpState`` / ``RestoreState`` — a per-object state key that
     *  is written only when an object has state of its own, leaving every other
     *  object's serialised form byte for byte what it was. That is Max's "save
     *  data with patcher" flag, on by default, since a collection a patch
     *  cannot reload is a collection a patch has to rebuild by hand.
     *
     *  The pointer is not saved: it is run-time position, the way ``.cycle``'s
     *  ``thresh`` and ``.bucket``'s ``freeze`` are, and a reloaded patch starts
     *  at the first entry.
     *
     *  ### Deliberately not here
     *
     *  The shared ``name`` context (all ``coll`` objects of one name sharing
     *  their contents), file ``read`` / ``write``, the editor window, and the
     *  arithmetic and reordering messages ``sub`` / ``nsub`` / ``nth`` / ``min``
     *  / ``max`` / ``sort`` / ``swap`` / ``merge`` / ``separate`` /
     *  ``renumber`` / ``assoc`` / ``deassoc`` / ``nstore`` / ``subsym``. They
     *  are a follow-up; the store, its addresses and its traversal are the part
     *  everything else is built on.
     */
    PATCHER_CLASS(gColl, YSE::OBJ::G_COLL)
    _NO_MESSAGES
    _NO_CALCULATE

    _BANG_IN(BangIn)
    _INT_IN(IntIn)
    _FLOAT_IN(FloatIn)
    _LIST_IN(ListIn)

    /**
     *  @brief Most entries the collection holds — 256.
     *
     *  The patcher's own bound (``kValueListCap``, ``.atoi``'s character
     *  ceiling), and the size of the table allocated at construction. A
     *  ``store`` past it is refused rather than growing the table, since
     *  growing it would allocate on whichever thread the message arrived on.
     */
    static constexpr std::size_t MAX_ENTRIES = 256;

    /** @brief Longest address, in characters. */
    static constexpr std::size_t KEY_CAPACITY = 64;

    /** @brief Longest stored message, in characters — ``kValueListCap``. */
    static constexpr std::size_t VALUE_CAPACITY = 256;

    /** @brief How many entries the collection holds. */
    std::size_t Count() const {
      return count;
    }

    /** @brief Where ``bang`` / ``next`` / ``prev`` currently point, as a
     *         position in storage order. 0 on an empty collection. */
    std::size_t Pointer() const {
      return pointer < count ? pointer : 0;
    }

    /** @brief The address of entry @p position in storage order, or ``""``.
     *         Diagnostics and tests: it returns a copy, so control thread
     *         only. */
    std::string KeyAt(std::size_t position) const;

    /** @brief The message stored at entry @p position in storage order, or
     *         ``""``. Same contract as ``KeyAt``. */
    std::string ValueAt(std::size_t position) const;

    /** @brief The message stored at @p address, or ``""`` when nothing is.
     *         Same contract as ``KeyAt``. */
    std::string Lookup(const std::string& address) const;

    // The contents, into the object's "state" key of a DumpJSON (issue #494).
    // Control thread — patcherImplementation::DumpJSON holds mtx — but the
    // guard is still taken, because a message may be arriving from a rendering
    // graph while the patch is being saved.
    void DumpState(nlohmann::json::value_type& json) override;

    // The other half: called from ParseJSON on the control thread, on a
    // freshly built object the audio thread cannot see yet.
    void RestoreState(const nlohmann::json::value_type& json) override;

  private:
    // One resolved address. `numeric` decides which of the other fields means
    // anything, and a numeric address never matches a symbolic one. Points into
    // the caller's message rather than copying it, so resolving costs nothing.
    struct Address {
      const char* text = nullptr;
      std::size_t length = 0;
      bool numeric = false;
      int index = 0;
    };

    // One entry. Both strings are reserved to their capacity at construction
    // and never grow, so writing one is a memcpy into storage that exists.
    // `key` always holds the address as text — for a numeric entry the decimal
    // spelling of `index`, kept in step by SetNumericKey so that `delete`'s
    // renumbering cannot leave the two disagreeing.
    struct Entry {
      std::string key;
      std::string value;
      int index = 0;
      bool numeric = false;
    };

    /**
     *  @brief Non-blocking exclusive access to the store.
     *
     *  ``Held()`` is false when another thread had it — the caller then does
     *  nothing at all. Never waits, never allocates. The same shape as
     *  ``valueSlotGuard`` in gValue.h and for the same reason: this object is
     *  reachable from the control thread and from a rendering graph alike, a
     *  mutex is out on the second of those, and there is no single writer to
     *  build a seqlock around.
     */
    class storeGuard {
    public:
      explicit storeGuard(std::atomic<bool>& flag)
        : flag_(flag), held_(!flag.exchange(true, std::memory_order_acquire)) {}
      ~storeGuard() {
        if (held_) flag_.store(false, std::memory_order_release);
      }
      storeGuard(const storeGuard&) = delete;
      storeGuard& operator=(const storeGuard&) = delete;
      storeGuard(storeGuard&&) = delete;
      storeGuard& operator=(storeGuard&&) = delete;

      bool Held() const {
        return held_;
      }

    private:
      std::atomic<bool>& flag_;
      bool held_;
    };

    // Resolve the `length` characters at `text` into an address. False for an
    // empty token, or one longer than KEY_CAPACITY — an address that cannot be
    // stored cannot be looked up either, so both ends refuse the same things.
    static bool ReadAddress(const char* text, std::size_t length, Address& out);

    // Position of the entry at `address` in storage order, or -1. Guard held.
    int Find(const Address& address) const;

    // Write `length` characters of `value` into `entry`. False when the message
    // is longer than the entry can hold — refused rather than truncated, since
    // half a message is a different message. Guard held.
    static bool AssignValue(Entry& entry, const char* value, std::size_t length);

    // Point `entry` at numeric address `index`, keeping its key text in step.
    // Guard held.
    static void SetNumericKey(Entry& entry, int index);

    // Copy `src` over `dst` without allocating: both strings were reserved to
    // capacity at construction, so assign() reuses storage that exists. This is
    // what lets insert and delete shift the table around on the audio thread.
    // Guard held.
    static void CopyEntry(Entry& dst, const Entry& src);

    // Store `value` at `address`, replacing what was there or appending a new
    // entry at the end. False when the collection is full or the message does
    // not fit. Guard held.
    bool StoreAt(const Address& address, const char* value, std::size_t length);

    // Store `value` at numeric address `index`, taking that address off
    // whatever held it: every equal or greater numeric address goes up by one
    // and the new entry lands in front of them in storage order. False when the
    // collection is full or the message does not fit. Guard held.
    bool InsertAt(int index, const char* value, std::size_t length);

    // Drop the entry at `position`, closing the gap so storage order survives.
    // When `renumber`, every numeric address above the one removed comes down
    // by one — Max's `delete` as against its `remove`. Guard held.
    void Erase(std::size_t position, bool renumber);

    // Highest numeric address in use, or -1 when no entry has one. Guard held.
    int HighestIndex() const;

    // Take the guard, copy entry `position` into the send buffers, release it,
    // and send: the address out outlet 1 when `withAddress`, then the data out
    // outlet 0. False when there is no such entry, in which case nothing is
    // sent. The guard is deliberately not held across the send — see the class
    // documentation.
    bool Output(std::size_t position, bool withAddress, YSE::THREAD thread);

    // Send `text` out outlet `pin` in the kind it is: a list, or the int or
    // float it spells when it is a single number. `.route`'s rule, minus its
    // bang case — an entry holding nothing is still an entry, and a bang would
    // read downstream as "no data" rather than "empty data".
    void SendTyped(std::size_t pin, const std::string& text, YSE::THREAD thread);

    // The command half of the inlet. Returns false when `text` is not a
    // command at all, leaving the caller to read it as an address.
    bool HandleCommand(const char* text, std::size_t length, YSE::THREAD thread);

    // Recall the entry at `address` and send its data alone — no address
    // outlet, which is Max's rule for a plain lookup.
    void Recall(const Address& address, YSE::THREAD thread);

    // Claimed with a single exchange by readers and writers alike; the loser
    // drops. Mutable so the const diagnostic accessors can take it.
    mutable std::atomic<bool> busy{false};

    // The table. Sized to MAX_ENTRIES at construction and never resized; only
    // the first `count` entries are live.
    std::vector<Entry> entries;
    std::size_t count = 0;

    // Where bang / next / prev point, in storage order. Run-time state rather
    // than a parameter, so it does not survive a save — `.cycle`'s `thresh` and
    // `.bucket`'s `freeze` are the same kind of thing.
    std::size_t pointer = 0;

    // What a send is made from, reserved at construction. Copies rather than
    // the entry itself: the send path is synchronous, so handing an outlet the
    // stored string would let a patch that writes to this object from
    // downstream mutate the very message still being fanned out.
    std::string sendValue;
    std::string sendAddress;
    bool sendNumeric = false;
    int sendIndex = 0;
  };

} // namespace PATCHER
} // namespace YSE
