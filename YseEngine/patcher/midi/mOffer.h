#pragma once
// `.offer` (issue #544) — Max's `offer`, "store one-time number pairs".
//
// Not guarded on YSE_ENABLE_MIDI_DEVICE, for the reason `.flush`, `.sustain`,
// `.poly`, `.borax`, `.stripnote` and `.makenote` are not: this object opens no
// device and holds no port. It takes ints on ordinary cords and sends ints back
// out, which is as useful in front of a patcher-built voice on a platform with
// no MIDI hardware at all as it is in front of a hardware rack.
#include "../pObject.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief `.offer` — store two ints as an x,y pair and get the y back once,
     *         by x (issue #544). Max's `offer`.
     *
     *  ### What it is for
     *
     *  Max: "Store two ints as an x, y pair, and access them by x value. When a
     *  pair is retrieved, it is deleted from the collection." The digest is
     *  "store one-time number pairs", and *one-time* is the whole object: a pair
     *  answers exactly one question and is then gone.
     *
     *  Max's own Discussion says what it was built for, and it is the reason
     *  issue #544 files this under the MIDI layer rather than as another generic
     *  store: "`offer` was designed for use with algorithms that transform the
     *  pitch of an incoming note stream. By storing the original and transformed
     *  note together ... when the pitch of the note-on is changed, the
     *  transformed pitch can be retrieved when the note-off is received."
     *
     *  That is the bookkeeping problem every transposer, harmoniser and
     *  note-mangler has and that nothing else in the patcher spells: a note-on
     *  goes out at some pitch the patch computed, and when the note-off arrives
     *  it carries the pitch the *player* released, not the one the synth is
     *  sounding. Something has to remember the mapping, per note, and forget it
     *  again the moment it is used — because the next note-on for that key is a
     *  new mapping and the one before it must not still be lying around. Keyed
     *  store in, one answer out, entry deleted.
     *
     *  ### It is not `.coll`, `.funbuff`, `.table` or `.bag`
     *
     *  All four are in Max's own "See Also" for this object, and the difference
     *  is deletion-on-read:
     *
     *  - `.coll` (#494) and `.table` (#498) are keyed stores a patch reads *and
     *    keeps reading*. A lookup there is a question; here it is a withdrawal.
     *  - `.funbuff` (#496) is the closest of the four — it also stores x,y number
     *    pairs and looks a y up by x — and it is the one to reach for when the
     *    mapping is a *function* the patch keeps consulting: a curve, a scale
     *    table, a lookup that answers the same way every time it is asked. This
     *    object is for a mapping that is true exactly once.
     *  - `.bag` (#495) has no addresses at all; it answers "how many" and "which
     *    ones", never "which y went with this x".
     *
     *  ### The two inlets, and the one bit of state that decides everything
     *
     *  Max's `int`, left inlet: "The number specifies the x value of an x,y pair.
     *  If a y value has been received in the right inlet, the two numbers are
     *  stored together in `offer`; otherwise, `offer` looks for an x value that
     *  matches the incoming number, sends out the corresponding y value, then
     *  deletes the stored pair. If there is no x value stored in `offer` that
     *  matches the number received, `offer` does nothing."
     *
     *  Max's `int`, right inlet: "The number specifies a y value to be stored in
     *  `offer`. The next x value (int) received in the left inlet causes the two
     *  numbers to be stored together as an x,y pair."
     *
     *  So the right inlet does not store anything by itself — it *arms* the
     *  object with one y, and the very next x spends it. The same inlet 0 is a
     *  **store** when a y is waiting and a **query** when one is not, and which
     *  of the two it is depends on nothing else. A patch that wants to store
     *  sends y then x; a patch that wants to retrieve sends x alone.
     *
     *  That armed y is deliberately a flag and not "y is non-zero": 0 is a
     *  perfectly good y — it is the velocity a note-off carries — and an object
     *  that could not store `x 0` would be useless for exactly the note stream
     *  Max designed it for.
     *
     *  A `float` on either inlet is converted to an int, which is what Max does
     *  with a float sent to an int inlet; the object stores ints and nothing
     *  else.
     *
     *  ### The list form
     *
     *  Max's Discussion: the two numbers "may be sent in the right inlet,
     *  immediately followed by the original, or they can be sent as a list".
     *  `offer` has no `list` method of its own, so a list is Max's inlet
     *  distribution: the first element behaves as though it had been sent to the
     *  left inlet and the second to the right, and under Max's right-to-left
     *  delivery the *second* one lands first. So the order is ``<x> <y>`` — `60
     *  72` arms the object with 72 and then spends it on 60, storing the pair —
     *  and a one-element list is a bare x, which is what lets a `.m 60` reach
     *  this inlet as a query.
     *
     *  ### `bang` and `clear`
     *
     *  Max's `bang`: "bang will cause `offer` to output every y-value received
     *  since the last clear message was received (or since the last
     *  initialization)."
     *
     *  Read literally that would include the y values already retrieved, and it
     *  cannot mean that: the Description says in as many words that "when a pair
     *  is retrieved, it is deleted from the collection", and an object that also
     *  kept a second copy of everything it had ever been handed would need
     *  unbounded memory to contradict its own digest with. So a bang sends the y
     *  of every pair the object is **currently holding** — everything received
     *  since the last `clear` that has not since been withdrawn — one at a time,
     *  in the order the pairs were stored, oldest first. That is the order Max's
     *  sentence reads in ("every y-value received"), and it is the opposite of
     *  `.bag`'s newest-first bang, which is why the two are spelled out in both
     *  objects rather than left to be guessed at.
     *
     *  A bang is **not** a withdrawal: it sends the y values and leaves every
     *  pair in place. Max gives the bang no deletion sentence, and the one-time
     *  contract belongs to the x lookup — a bang that emptied the store would
     *  make "dump what you are holding" and "spend everything you are holding"
     *  the same gesture, and only one of them is recoverable.
     *
     *  Max's `clear`, left inlet: "Deletes the entire contents of `offer`." It
     *  sends nothing, and it also disarms a y that was waiting for its x. That
     *  second half is not in Max's sentence and follows from it: a `clear` is
     *  "forget everything you were told", and a y carried across it would make
     *  the first x after the clear disappear into a pair the patch had already
     *  written off.
     *
     *  ### Duplicate x values, and which pair a query takes
     *
     *  Max does not say what happens when a pair is stored under an x that is
     *  already there, and for a note tracker it is the case that matters most:
     *  the same key struck twice before the first note-off, which every sustained
     *  passage produces.
     *
     *  Both pairs are kept. Overwriting would be the shorter code and it would
     *  lose a note: the first mapping would vanish while the synth was still
     *  sounding it, and the second note-off would find nothing to withdraw. Two
     *  stored pairs mean two note-ons can be answered by two note-offs, which is
     *  what the patch actually did.
     *
     *  A query then takes the **newest** matching pair, which is `.bag`'s rule
     *  and for `.bag`'s reason: store and retrieve are an exact undo of each
     *  other, so a re-struck key unwinds in the order the keys came down.
     *
     *  ### Real-time behaviour
     *
     *  A message handler runs on whichever thread dispatched the message, and
     *  in-patcher delivery dispatches on `T_DSP` — routinely the audio callback.
     *  So the store is `.bag`'s and `.coll`'s: a fixed table of `MAX_PAIRS`
     *  pairs allocated whole at construction and never resized, with `.value`'s
     *  non-blocking guard around it. A store past the capacity is refused whole
     *  and silently rather than growing the table, and the y that was waiting is
     *  spent rather than left armed — a refused pair that kept the y would pair
     *  it with the *next* x instead, which is a wrong answer where dropping it is
     *  merely a missing one.
     *
     *  The guard is claimed with a single exchange and whoever loses **drops**
     *  its operation and counts it on `Dropped()` rather than spinning. The
     *  armed y and its flag live *under* that guard rather than beside it as
     *  `.sustain`'s pedal does, and the difference is deliberate: the pedal is an
     *  independent mode, while this flag is half of one transaction — it decides
     *  whether an x stores or withdraws, and it is cleared by the same store that
     *  reads it. Split across two atomics it could be read by one thread and
     *  cleared by another between the two halves, turning a store into a query.
     *
     *  The guard is never held across a send. A withdrawal copies its y out and
     *  releases the guard before touching the outlet, and a bang copies the whole
     *  set into a stack array first — `.bucket`'s and `.bag`'s trick, and a stack
     *  array rather than a member because a member buffer would be overwritten by
     *  exactly the re-entrant burst the capture exists to protect against. A
     *  patch that wires the outlet back into inlet 0 therefore works rather than
     *  deadlocking, and it cannot run away: every re-entrant query deletes the
     *  pair it answered, so the chain is bounded by how many pairs are stored.
     *
     *  `Calculate()` does nothing: the object is driven entirely by its inlets,
     *  and one that emitted would withdraw a pair on every DSP tick from a
     *  stimulus no patch sent — the rule `.bag`, `.coll`, `.value` and `.bucket`
     *  establish.
     *
     *  ### What persists, and what does not
     *
     *  Nothing. Max's `offer` takes no creation arguments — "Arguments: None" —
     *  so there is no parameter to carry, and the contents are run-time state in
     *  the same sense as `.bag`'s: Max gives `offer` no "save data with patcher"
     *  flag, that being `coll`'s, and a reloaded patch whose `.offer` came back
     *  holding the mappings for notes that were sounding when it was saved would
     *  be holding answers to note-offs that are never coming.
     *
     *  There is no `Teardown` hook either, and unlike `.sustain` and `.flush`
     *  this object needs none: it never swallows a message. Everything it holds
     *  it was *given* to hold, and the note-offs it exists to answer are still
     *  travelling through the patch on their own.
     */
    PATCHER_CLASS(mOffer, YSE::OBJ::M_OFFER)
    _NO_MESSAGES
    _NO_CALCULATE

    _BANG_IN(BangIn)
    _INT_IN(IntIn)
    _FLOAT_IN(FloatIn)
    _LIST_IN(ListIn)

    /**
     *  @brief Most pairs the object holds — 256.
     *
     *  The patcher's own bound: `.coll`'s entry count, `.bag`'s capacity and the
     *  patcher's value queue (`kValueListCap`) are all this number, so anything
     *  that can reach this object through a patch also fits in it. Max documents
     *  no limit; a store past this one is refused rather than growing the table,
     *  since growing it would allocate on whichever thread the message arrived
     *  on. Also the size of the stack array a bang is captured into, which is why
     *  it is a compile-time constant.
     */
    static constexpr std::size_t MAX_PAIRS = 256;

    /** @brief How many pairs are stored. Diagnostics and tests; a patch sees the
     *         same thing by banging. */
    std::size_t Count() const;

    /** @brief Whether a y is armed and waiting for the x that will spend it —
     *         Max's "if a y value has been received in the right inlet". Cleared
     *         by the next x, and by `clear`. */
    bool HasPendingY() const;

    /** @brief The armed y, or 0 when none is armed. `HasPendingY()` is what
     *         separates those two, 0 being a perfectly good y. */
    int PendingY() const;

    /** @brief The x of the pair at @p position in storage order — 0 the oldest.
     *         0 out of range. Diagnostics and tests. */
    int XAt(std::size_t position) const;

    /** @brief The y of the pair at @p position in storage order — the value a
     *         bang sends @p position'th. 0 out of range. Diagnostics and tests. */
    int YAt(std::size_t position) const;

    /** @brief Whether any stored pair has this x — whether an x sent to inlet 0
     *         would withdraw something. */
    bool Contains(int x) const;

    /** @brief How many stored pairs have this x. More than one only when the
     *         same x was stored again before the first was withdrawn. */
    std::size_t CountOf(int x) const;

    /** @brief Operations refused by the guard — a concurrent or re-entrant
     *         access. Monotonic, readable from any thread; diagnostics and tests
     *         only. */
    std::uint64_t Dropped() const {
      return dropped.load(std::memory_order_relaxed);
    }

  private:
    /** @brief One x,y pair. Two ints, stored by value in a table sized once. */
    struct Pair {
      int x = 0;
      int y = 0;
    };

    /**
     *  @brief Non-blocking exclusive access to the store.
     *
     *  `Held()` is false when another thread had it — the caller then does
     *  nothing at all and counts the loss. Never waits, never allocates.
     *  `.bag`'s `storeGuard` and `.value`'s `valueSlotGuard`, for the reason
     *  both give: this object is reachable from the control thread and from a
     *  rendering graph alike, a mutex is out on the second of those, and there is
     *  no single writer to build a seqlock around.
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

    // Max's left inlet, both halves of it: a store when a y is armed and a
    // withdrawal when one is not. Sends with the guard already released, so an
    // outlet wired back into this inlet finds the store free.
    void Apply(int x, YSE::THREAD thread);

    // Max's right inlet: arm one y. Stores nothing and sends nothing by itself.
    void ArmY(int y);

    // Max's `clear`, "deletes the entire contents of offer" — and disarms a
    // waiting y with it. See the class notes.
    void ClearStore();

    // Max's bang: copy every stored y into `out` — which must hold MAX_PAIRS —
    // oldest first, and return how many were written. Takes the guard itself and
    // returns 0 when it loses it, so the caller can emit with it released.
    std::size_t CaptureOldestFirst(int* out) const;

    // Put one pair in. Refused when the table is full; duplicates of an existing
    // x are kept, which is what lets the same key be struck twice. Guard held.
    void Insert(int x, int y);

    // Take the newest pair whose x matches, closing the gap so the rest keep
    // their order, and report its y in `y`. False when there is none — Max's "if
    // there is no x value stored in offer that matches the number received, offer
    // does nothing". Guard held.
    bool Withdraw(int x, int& y);

    // Claimed with a single exchange by readers and writers alike; the loser
    // drops. Mutable so the const diagnostic accessors and the const bang
    // capture can take it — and `dropped` with it, since the capture is the one
    // const path a patch can actually observe losing.
    mutable std::atomic<bool> busy{false};
    mutable std::atomic<std::uint64_t> dropped{0};

    // The table. Sized to MAX_PAIRS at construction and never resized; only the
    // first `count` entries are live, oldest at 0.
    std::vector<Pair> entries;
    std::size_t count = 0;

    // Max's "if a y value has been received in the right inlet". Under the guard
    // rather than beside it — see the class notes on why the flag and the store
    // are one transaction.
    int pendingY = 0;
    bool hasPendingY = false;
  };

} // namespace PATCHER
} // namespace YSE
