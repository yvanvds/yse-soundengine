#pragma once
#include "../pObject.h"
#include <atomic>
#include <cstddef>
#include <string>
#include <vector>

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief An unordered collection of numbers — ``.bag`` (issue #495).
     *
     *  Max's ``bag``, "store a collection of numbers": "stores and manages a
     *  collection of numbers. You can add to or delete an integer from a bag as
     *  well as report its contents."
     *
     *  ### What it is for
     *
     *  A **multiset with add/remove semantics**, and the classic use is the one
     *  nothing else in the patcher can spell: tracking which notes are currently
     *  held, so a patch can answer *"what is sounding right now"*. A note-on adds
     *  the note number, the matching note-off removes it, and a bang at any
     *  moment hands back exactly the notes still down. That pairs directly with
     *  YSE's synth voice model, where the set of live voices is the thing a patch
     *  has to reason about.
     *
     *  It is not ``.coll`` (#494), which it sits next to in the registry.
     *  ``.coll`` is a **keyed** store: every entry has an address and the point is
     *  to look one up. This has no addresses at all — a value is either in the
     *  collection or it is not, and the only questions it answers are "how many"
     *  and "which ones". A patch that wants a note table wants ``.coll``; a patch
     *  that wants the set of notes currently down wants this. Nor is it
     *  ``.bucket``, which remembers the last N values *positionally*: this
     *  remembers a set whose size is whatever the patch has put in it and taken
     *  out again.
     *
     *  ### The two inlets, and which one is the flag
     *
     *  Max's ``bag`` has two, and the split is the object's whole interface.
     *  Inlet 0 is the **value** and is hot; inlet 1 is the **flag** and is cold.
     *  Max, on ``int``: "In left inlet: The number is either added to or deleted
     *  from the collection of numbers stored in the bag object, depending on the
     *  number in the right inlet. In right inlet: The number is stored as an
     *  indicator of whether to include or delete the next number received in the
     *  left inlet. If non-zero, the number received in the left inlet is added to
     *  the bag. If 0, the number is deleted from the collection."
     *
     *  So the list form is ``<value> <flag>`` and **not** ``<flag> <value>``.
     *  Issue #495's summary line has the two the other way round; Max's ``list``
     *  method settles it in as many words — "any list composed of two numbers
     *  behaves as though the first list item was sent to the left inlet and the
     *  second list item was sent to the right inlet. If the second element of the
     *  list is a non-zero number, the number is added to the collection" — and a
     *  patcher whose argument order is the reverse of Max's is a trap for every
     *  patch brought across, so Max's order is the one implemented here. ``60 1``
     *  adds 60 and ``60 0`` removes it.
     *
     *  A list sets the flag **before** it applies the value, which is what "as
     *  though the second item was sent to the right inlet" means under Max's
     *  right-to-left delivery, and the flag it leaves behind is the one a later
     *  bare number will use.
     *
     *  Max documents no initial value for the flag and this object starts it at
     *  **0**, the conservative choice: a value that arrives before any patch has
     *  said what to do with it does not silently join the collection. Every real
     *  patch sets the flag — through the list form or through inlet 1 — before
     *  values flow, so the initial value is very nearly unobservable either way.
     *
     *  A ``float`` on either inlet is Max's "converted to int", so ``0.5`` on the
     *  flag inlet is a 0 and means *remove*.
     *
     *  ### Duplicates, from the creation argument
     *
     *  Max: "bag with any argument maintains multiple entries with the same item;
     *  otherwise it holds only one of each", and the argument itself is "the
     *  presence of any symbol argument causes the bag to store duplicate values".
     *  So the argument is read for its *presence* and not for its value, and any
     *  non-empty creation argument turns duplicates on.
     *
     *  Without it, adding a value already present is nothing at all — the entry
     *  keeps the position it already had rather than moving to the front, since
     *  "holds only one of each number at a time" is a statement about the
     *  contents and not about their order. With it, the same value can be in the
     *  collection as many times as it was added, which is what a note tracker
     *  wants when two keys sound the same pitch.
     *
     *  ### Order: what a bang sends, and what ``cut`` takes
     *
     *  Max, on the output: "all the numbers stored in bag are sent out one at a
     *  time, in **reverse order** from that in which they were stored", and
     *  ``cut`` "sends out the **oldest** (earliest received) number stored in the
     *  bag object, and deletes it from the bag". Between them the two ends of the
     *  collection are pinned: a bang is newest-first, ``cut`` is a pop from the
     *  old end.
     *
     *  Which *instance* a remove takes when duplicates are on is the one thing
     *  Max leaves open, and it is observable — a bag holding ``60 62 60`` dumps
     *  differently depending on which 60 goes. The **newest** matching instance is
     *  removed here, so that an add and a remove of the same value are an exact
     *  undo of each other and a held-note tracker unwinds in the order the keys
     *  came up. Nothing in Max contradicts it and nothing else makes add/remove
     *  pairs commute.
     *
     *  ``clear`` empties the collection, ``length`` reports its size out the
     *  outlet as an int, and Max's "no output is triggered by a number received in
     *  either inlet" holds: adding and removing are silent, and only ``bang``,
     *  ``cut`` and ``length`` ever send.
     *
     *  ### Real-time behaviour
     *
     *  The storage model is ``.coll``'s and for ``.coll``'s reason: a fixed table
     *  of ``MAX_ENTRIES`` allocated whole at construction and never resized, plus
     *  ``.value``'s non-blocking ``busy`` guard. A copy-on-write ``GraphState``
     *  publish would assume the writer is the control thread, and a ``.bag`` is
     *  written by whichever thread its message arrived on — in-patcher delivery
     *  dispatches on **T_DSP** — so a note-on reaching it from a rendering graph
     *  would have to allocate a replacement table there. An add past the capacity
     *  is refused whole and silently rather than growing the table.
     *
     *  The guard is claimed with a single ``exchange`` and whoever loses **drops**
     *  its operation rather than spinning, and it is never held across a send: a
     *  bang copies the whole collection into a stack array first, releases the
     *  guard, and only then emits, so a patch that wires the outlet back into the
     *  inlet finds the store free and the burst it interrupted goes on sending
     *  what it started with. That capture is ``.bucket``'s, and a stack array is
     *  what makes each burst independent — a member buffer would be overwritten by
     *  exactly the re-entrant burst the capture exists to protect against.
     *
     *  ``Calculate()`` does nothing: the object is driven by its inlets, and one
     *  that emitted would re-dump on every DSP tick — the rule ``.route``,
     *  ``.sel``, ``.value``, ``.bucket`` and ``.coll`` establish.
     *
     *  ### What persists, and what does not
     *
     *  The duplicate flag, because it is a creation argument. The **contents**
     *  deliberately do not: Max's ``bag`` has no "save data with patcher" flag —
     *  that is ``coll``'s, which is why ``.coll`` and not this object is what made
     *  ``pObject`` grow ``DumpState`` / ``RestoreState`` — and the contents of a
     *  bag are run-time state in the same sense as ``.cycle``'s ``thresh``,
     *  ``.bucket``'s ``freeze`` and ``.coll``'s pointer. A reloaded patch whose
     *  bag came back holding the notes that were down when it was saved would be
     *  holding notes nothing is sounding.
     *
     *  ### Deliberately not here
     *
     *  Max's ``send <receive-name>``, which redirects a bang's output to
     *  ``receive`` objects by name instead of out the outlet. It is the same
     *  name-context feature ``.coll`` deferred, it needs a name buffer written
     *  from a message path to stay allocation-free, and it is filed as #685
     *  rather than smuggled in here.
     */
    PATCHER_CLASS(gBag, YSE::OBJ::G_BAG)
    _NO_MESSAGES
    _NO_CALCULATE

    _BANG_IN(BangIn)
    _INT_IN(IntIn)
    _FLOAT_IN(FloatIn)
    _LIST_IN(ListIn)

    _PARM_CLEAR
    _PARM_PARSE

    /**
     *  @brief Most numbers the collection holds — 256.
     *
     *  The patcher's own bound: ``.coll``'s entry count, ``.bucket``'s port
     *  ceiling and the patcher's value queue (``kValueListCap``) are all this
     *  number, so anything that can reach this object through a patch also fits
     *  in it. Max documents no limit; an add past this one is refused rather than
     *  growing the table, since growing it would allocate on whichever thread the
     *  message arrived on. Also the size of the stack array a bang is captured
     *  into, which is why it is a compile-time constant.
     */
    static constexpr std::size_t MAX_ENTRIES = 256;

    /** @brief How many numbers the collection holds. */
    std::size_t Count() const {
      return count;
    }

    /** @brief The number at @p position in storage order — 0 the oldest, and
     *         ``Count() - 1`` the newest, which is the one a bang sends first.
     *         0 out of range. Diagnostics and tests: control thread only. */
    int ValueAt(std::size_t position) const;

    /** @brief Whether @p value is in the collection at all. Same contract as
     *         ``ValueAt``. */
    bool Contains(int value) const;

    /** @brief How many times @p value is in the collection — never more than one
     *         unless the object was created with an argument. Same contract as
     *         ``ValueAt``. */
    std::size_t CountOf(int value) const;

    /** @brief Whether the creation argument turned duplicate entries on. */
    bool StoresDuplicates() const {
      return duplicates;
    }

    /** @brief Whether inlet 1 currently says *add* rather than *remove*. Run-time
     *         state, so it does not survive a save. */
    bool AddMode() const {
      return addMode.load(std::memory_order_relaxed);
    }

  private:
    /**
     *  @brief Non-blocking exclusive access to the store.
     *
     *  ``Held()`` is false when another thread had it — the caller then does
     *  nothing at all. Never waits, never allocates. ``.coll``'s ``storeGuard``
     *  and ``.value``'s ``valueSlotGuard``, for the reason both give: this object
     *  is reachable from the control thread and from a rendering graph alike, a
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

    // Put `value` in. Refused when the collection is full, and nothing at all
    // when the value is already there and duplicates are off — Max's "holds only
    // one of each number at a time". Guard held.
    void Insert(int value);

    // Take the newest instance of `value` out, closing the gap so the rest keep
    // their order. Nothing when the value is not there. Guard held.
    void Remove(int value);

    // Apply one value with the flag inlet 1 currently holds. Silent: Max's "no
    // output is triggered by a number received in either inlet".
    void Apply(int value);

    // Copy the whole collection into `out` — which must hold MAX_ENTRIES — and
    // return how many were written, newest first, which is the order a bang
    // sends in. Takes the guard itself and returns 0 when it loses it, so the
    // caller can emit with the guard already released.
    std::size_t CaptureNewestFirst(int* out) const;

    // The command half of inlet 0. Returns false when `text` is none of them,
    // leaving the caller to read it as numbers.
    bool HandleCommand(const char* text, std::size_t length, YSE::THREAD thread);

    // Claimed with a single exchange by readers and writers alike; the loser
    // drops. Mutable so the const diagnostic accessors can take it.
    mutable std::atomic<bool> busy{false};

    // The table. Sized to MAX_ENTRIES at construction and never resized; only
    // the first `count` entries are live, oldest at 0.
    std::vector<int> entries;
    std::size_t count = 0;

    // Inlet 1's flag: true adds, false removes. Written from a message path and
    // read from another, so atomic — .gate's activeOutlet for the same reason.
    // Run-time state rather than a parameter, so it does not survive a save.
    std::atomic<bool> addMode{false};

    // The creation argument, read for its presence only. Control thread: written
    // by Parameters::Set, read by ParseParams(), never by a message handler.
    std::string duplicateArg;

    // Max's duplicate mode. A creation argument, so it survives a save; a live
    // SetParams that changes it takes the structural rebuild path (a STRING
    // parameter makes NeedsRebuild() true), so no message path ever sees it
    // change under itself.
    bool duplicates = false;
  };

} // namespace PATCHER
} // namespace YSE
