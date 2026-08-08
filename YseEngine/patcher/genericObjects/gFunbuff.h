#pragma once
#include "../pObject.h"
#include <atomic>
#include <cstddef>
#include <string>
#include <vector>

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief A sorted store of x,y pairs with lookup and interpolation —
     *         ``.funbuff`` (issue #497).
     *
     *  Max's ``funbuff``, "stores, manages, and recalls pairs of numbers". A
     *  **sparse function**: a handful of (x, y) points, and a way to ask what
     *  ``y`` is at an ``x`` that was never stored. Breakpoint curves, tuning
     *  tables, velocity maps and step sequences are all the same object — the
     *  data structure behind every hand-drawn control curve.
     *
     *  It is the third store in the family and the first that is **ordered by
     *  its own key**. ``.coll`` (#494) is keyed but keeps *storage* order, which
     *  is what makes its ``dump`` replay a sequence; ``.bag`` (#495) has no keys
     *  at all; ``.capture`` (#496) is a tape. This keeps its pairs sorted
     *  ascending by x at all times, because every question it answers — the
     *  lookup, ``next``, ``dump``, ``interp`` — is a question about *where an x
     *  sits among the others*. Sorting is not a convenience here; it is the
     *  object.
     *
     *  ### The two inlets, and the one-shot y
     *
     *  Inlet 0 is the **x** and is hot; inlet 1 is the **y** and is cold. Max,
     *  on the left inlet: "If a y value was previously received in the right
     *  inlet, the pair is stored. Otherwise, outputs the corresponding y value."
     *  So the same inlet both writes and reads, and which one happens is decided
     *  by whether a y is waiting.
     *
     *  The waiting y is consumed by exactly one x — Max's "y value to be paired
     *  with the **next** x value received in the left inlet" — and this matters:
     *  a y that stayed behind would turn every later lookup into a store, and
     *  the object could never be read at all. A float is converted to an int on
     *  either inlet, which is Max's rule and the reason the stored pairs are
     *  integer pairs.
     *
     *  A two-number list on inlet 0 is Max's ordinary right-to-left list
     *  distribution — the second item behaves as though it had been sent to
     *  inlet 1 and the first as though it had been sent to inlet 0 — so
     *  ``60 100`` stores the pair (60, 100). That is the idiom every Max patch
     *  writes pairs with, and it is ``.bag``'s list rule with Max's own operand
     *  order.
     *
     *  ### The lookup is a *floor* lookup, not an interpolation
     *
     *  The one place where issue #497's summary and Max part company, and it is
     *  worth being exact about. The issue says the object supports "interpolated
     *  lookup"; Max's plain lookup interpolates nothing. Max: "If there is no
     *  stored x value which matches the number received, ``funbuff`` uses the
     *  **closest x value which is less than** the number received, and sends out
     *  the corresponding y value." That is a step function — a held value — and
     *  it is the right answer for the things a plain lookup is used for
     *  (presets, zone maps, which-region-am-I-in).
     *
     *  Interpolation is a **separate message**, ``interp``, and keeping the two
     *  apart is the whole reason both exist. A patch that wanted a stepped map
     *  and got a ramp would be silently wrong in a way nothing downstream could
     *  detect. Max is followed here, and the issue's shorthand is not.
     *
     *  An x below every stored x has no lesser neighbour to fall back to, so
     *  nothing is sent — the family's rule that an object with no answer stays
     *  quiet rather than inventing one.
     *
     *  ### ``interp``, and the one type Max does not state
     *
     *  Max: "the word ``interp``, followed by a number, uses that number as an x
     *  value, measures its position between its two neighboring x values in the
     *  funbuff, and then sends — out the left outlet — the y value that holds a
     *  corresponding position between the two neighboring y values." Straight
     *  linear interpolation between the bracketing pair.
     *
     *  Max describes only the bracketed case, so the three edges are decided
     *  here and stated plainly: an x below the lowest stored x answers with the
     *  lowest y, an x above the highest answers with the highest, and a store
     *  holding a single pair answers with that pair's y. That is clamping, which
     *  is what every breakpoint curve does at its ends and the only reading that
     *  keeps ``interp`` total over its input.
     *
     *  The **output type** is the one thing Max's reference does not state, and
     *  it is a real choice rather than a detail. It leaves as a **float** here.
     *  Every other message on this outlet reports a y that is *stored*, and a
     *  stored y is an int by construction; ``interp`` alone reports a value that
     *  was never stored, and "the y value that holds a corresponding position
     *  between the two neighboring y values" is a fractional quantity by
     *  definition. Truncating it would quantise every curve this object exists
     *  to draw — the issue's own use case, "breakpoint curves, tuning tables,
     *  and any mapping defined by a handful of points" — and the loss is not
     *  recoverable downstream, while a patch that wants the int can round one.
     *  The query x is likewise used at full precision rather than truncated: a
     *  patch asking ``interp 5.5`` is asking a different question from
     *  ``interp 5``, and Max's "converted to int" is a rule about the *inlets*,
     *  not about this message's argument.
     *
     *  ### The pointer, and what ``next`` sends where
     *
     *  Three outlets, Max's three. Outlet 0 is the y and everything else that
     *  answers a question; outlet 1 is the x — the *difference* between x values
     *  under ``next``, and the x itself under ``dump``; outlet 2 bangs when
     *  ``next`` has run off the end.
     *
     *  Max, on ``next``: "finds the x value pointed to by the pointer (or, if
     *  the pointer points to a number not yet stored as an x value, to the next
     *  greater x value), and sends the corresponding y value out the left
     *  outlet ... calculates the difference between that x value and the value
     *  previously pointed to." So the middle outlet is an **inter-onset
     *  interval**, which is what makes a ``.funbuff`` full of time-stamped
     *  events drivable by a ``.metro`` or a ``.line``: the x column is the
     *  schedule and outlet 1 hands out the gap to the next event.
     *
     *  The difference is measured from the last x ``next`` reported, and both
     *  that and the pointer start at 0 — so on a fresh object the first ``next``
     *  reports the first x itself, its distance from the origin, which is the
     *  delta a sequencer wants before its first event. ``goto`` moves both, so a
     *  ``goto`` followed by ``next`` measures from where the patch jumped to
     *  rather than from wherever the playhead had wandered.
     *
     *  When no stored x is at or past the pointer the pointer has run off the
     *  end: outlet 2 bangs and nothing else is sent. The pointer is left there
     *  rather than wrapped, so a repeated ``next`` keeps banging and a patch
     *  restarts with ``goto``. ``.coll``'s ``next`` wraps because a collection is
     *  a ring of entries; this one is a *traversal of a function*, and a
     *  sequence that silently looped would be a different musical object.
     *
     *  Outlet ordering is Max's right to left, the order ``.coll`` and
     *  ``.trigger`` already fire in: outlet 1 before outlet 0, so a patch has
     *  the x in hand before the y it belongs to arrives.
     *
     *  ### Bursts are captured whole, which ``.coll``'s dump could not be
     *
     *  ``dump`` and ``find`` copy everything they are going to send into a stack
     *  array under a single acquisition of the guard, release it, and only then
     *  emit — ``.bag``'s capture rather than ``.coll``'s take-the-guard-per-item
     *  walk. The difference is forced by the sort. ``.coll``'s table only ever
     *  *appends*, so a re-entrant store during a dump cannot move an entry the
     *  walk has not reached yet and re-reading the bounds each step is enough.
     *  This table is **sorted**, so a re-entrant ``set`` can insert a pair in
     *  front of the cursor and shift every later pair up by one — which would
     *  make the walk emit a pair twice and skip another. Capturing the whole
     *  burst first is what makes a dump a consistent snapshot of the function
     *  rather than of a table being edited underneath it.
     *
     *  Everything else is the model ``.coll`` established and for ``.coll``'s
     *  reason: a fixed table of ``MAX_PAIRS`` allocated whole at construction and
     *  never resized, plus ``.value``'s non-blocking ``busy`` guard, claimed with
     *  a single ``exchange`` by a loser that **drops** rather than spinning.
     *  Issue #497 asks for the other model — "mutation on the control thread
     *  only; ``Calculate()`` reads a published snapshot" — and that is a
     *  copy-on-write ``GraphState`` publish, which assumes the writer *is* the
     *  control thread. A ``.funbuff`` is written by whichever thread its message
     *  arrived on, and in-patcher delivery dispatches on **T_DSP**, so a ``set``
     *  reaching this object from a rendering graph would have to allocate a
     *  replacement table on the audio thread. Bounded storage plus the
     *  non-blocking guard gives what the mandate is actually after — no
     *  allocation, no lock, no I/O on any path — without that. A store past the
     *  capacity is refused whole and silently.
     *
     *  ``Calculate()`` does nothing, for the same reason it does nothing in
     *  ``.route``, ``.sel``, ``.value``, ``.coll``, ``.bag`` and ``.capture``:
     *  the object is driven by its inlets, and one that emitted from
     *  ``Calculate()`` would re-send on every DSP tick.
     *
     *  ### What persists — and why the answer is neither ``.coll``'s nor
     *      ``.bag``'s
     *
     *  ``.coll`` saves its contents unconditionally; ``.bag`` and ``.capture``
     *  deliberately save none. This object is the third answer, because Max
     *  gives it a third rule: ``embed``. Max, verbatim — "the word ``embed``,
     *  followed by a non-zero number, causes the ``funbuff`` data to be stored
     *  inside the patcher" — so persistence is **opt-in per object**, off until
     *  a patch asks for it, where ``coll``'s "save data with patcher" is on by
     *  default.
     *
     *  So the contents ride ``pObject::DumpState`` / ``RestoreState`` exactly as
     *  ``.coll``'s do, but only when ``embed`` is on, and the flag is written
     *  alongside them so a reloaded object still knows to save itself next time.
     *  With ``embed`` off nothing at all is written and the serialised object is
     *  byte for byte what it would have been without the hook — which is the
     *  contract that hook was added under. That satisfies issue #497's "should
     *  survive the JSON round trip" the way Max spells it: it survives when the
     *  patch says it should.
     *
     *  The pointer is not saved. It is run-time position — ``.coll``'s pointer,
     *  ``.cycle``'s ``thresh``, ``.bucket``'s ``freeze`` — and a reloaded patch
     *  starts its traversal at the beginning.
     *
     *  ### Deliberately not here
     *
     *  ``read`` and ``write``, which are file I/O on a path that may be the
     *  audio thread, and with them the creation argument, Max's "filename ...
     *  specifies a file to load when the patch opens". The argument is accepted
     *  and ignored so that a ``.funbuff mydata`` brought across from Max still
     *  builds the object it names — ``.capture``'s treatment of Max's display
     *  format argument.
     *
     *  ``copy``, ``cut``, ``paste``, ``select`` and ``undo``, which are one
     *  feature: an editor selection plus a **global** funbuff clipboard shared
     *  between every ``funbuff`` in the application. That is the same shared-name
     *  context ``.coll`` deferred and ``.bag`` deferred after it, and it needs
     *  process-wide mutable state reachable from a message path.
     *
     *  ``interptab``, which interpolates through a Max ``table`` object by name —
     *  the patcher has no ``table`` and no name context to find one through.
     *
     *  ``print``, and Max's ``bang``, which both write diagnostics to the Max
     *  Console. They are consumed rather than left to fall through, because Max
     *  dispatches on the selector and a ``funbuff`` in Max cannot store the
     *  symbol ``print`` either; neither sends anything out any outlet in Max, so
     *  consuming them silently reproduces Max's *observable* behaviour exactly.
     */
    PATCHER_CLASS(gFunbuff, YSE::OBJ::G_FUNBUFF)
    _NO_MESSAGES
    _NO_CALCULATE

    _INT_IN(IntIn)
    _FLOAT_IN(FloatIn)
    _LIST_IN(ListIn)

    /**
     *  @brief Most pairs the function can hold — 256.
     *
     *  The patcher's own bound: ``.coll``'s entry count, ``.bag``'s capacity and
     *  the patcher's value queue (``kValueListCap``) are all this number, so
     *  anything that can reach this object through a patch also fits in it. Max
     *  documents no limit; a store past this one is refused rather than growing
     *  the table, since growing it would allocate on whichever thread the
     *  message arrived on. Also the size of the stack array ``dump`` and ``find``
     *  capture into, which is why it is a compile-time constant.
     */
    static constexpr std::size_t MAX_PAIRS = 256;

    /** @brief How many pairs are stored. */
    std::size_t Count() const;

    /** @brief The x of the pair at @p position in ascending x order, or 0.
     *         Diagnostics and tests: control thread only. */
    int XAt(std::size_t position) const;

    /** @brief The y of the pair at @p position in ascending x order, or 0. Same
     *         contract as ``XAt``. */
    int YAt(std::size_t position) const;

    /** @brief The y stored at exactly @p x, or 0 when nothing is — @p found says
     *         which. An exact lookup, not the floor lookup the inlet does. Same
     *         contract as ``XAt``. */
    int Lookup(int x, bool& found) const;

    /** @brief Whether ``embed`` has been turned on, so the contents are written
     *         into a saved patch. Off until a patch asks. */
    bool Embeds() const;

    // The contents, into the object's "state" key of a DumpJSON — but only when
    // `embed` is on, which is Max's rule for this object. Control thread —
    // patcherImplementation::DumpJSON holds mtx — but the guard is still taken,
    // because a message may be arriving from a rendering graph while the patch
    // is being saved.
    void DumpState(nlohmann::json::value_type& json) override;

    // The other half: called from ParseJSON on the control thread, on a freshly
    // built object the audio thread cannot see yet.
    void RestoreState(const nlohmann::json::value_type& json) override;

  private:
    // One point of the function. Both ints: Max converts a float to an int on
    // either inlet, so the stored function is an integer function.
    struct Pair {
      int x = 0;
      int y = 0;
    };

    /**
     *  @brief Non-blocking exclusive access to the store.
     *
     *  ``Held()`` is false when another thread had it — the caller then does
     *  nothing at all. Never waits, never allocates. ``.coll``'s ``storeGuard``
     *  and ``.value``'s ``valueSlotGuard``, for the reason both give: this
     *  object is reachable from the control thread and from a rendering graph
     *  alike, a mutex is out on the second of those, and there is no single
     *  writer to build a seqlock around.
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

    // Position of the first pair whose x is >= `x`, or `count` when there is
    // none. The one primitive the whole object is built on: an exact lookup, a
    // floor lookup, an insert position and `next`'s search are all this. Binary
    // search, so a full table costs eight comparisons rather than 256 on a path
    // that may be the audio thread. Guard held.
    std::size_t LowerBound(int x) const;

    // The same search over a fractional query: position of the first pair whose
    // x is >= `x`, or `count` when there is none. Kept separate from the int
    // version rather than folded into it, because that one has to stay exact —
    // an int beyond 2^24 does not survive a round trip through a float, and
    // every store and delete goes through it.
    std::size_t LowerBoundF(float x) const;

    // Store `y` at `x`, replacing the pair already there or inserting a new one
    // so the table stays sorted. False when the table is full. Guard held.
    bool StorePair(int x, int y);

    // Drop the pair at `position`, closing the gap. Guard held.
    void EraseAt(std::size_t position);

    // Apply one x on inlet 0: stores the pair when a y is waiting from inlet 1
    // and consumes that y, otherwise sends the floor lookup out outlet 0. The
    // guard is taken here and released before anything is sent.
    void ApplyX(int x, YSE::THREAD thread);

    // Max's next: the y of the first pair at or past the pointer out outlet 0,
    // its distance from the previously reported x out outlet 1, and a bang out
    // outlet 2 when there is no such pair.
    void Next(YSE::THREAD thread);

    // Max's interp, clamped at both ends — see the class documentation.
    void Interp(float x, YSE::THREAD thread);

    // The command half of inlet 0. Returns false when `text` is none of them,
    // leaving the caller to read the message as numbers.
    bool HandleCommand(const char* text, std::size_t length, const std::string& message,
                       std::size_t argOffset, YSE::THREAD thread);

    // Claimed with a single exchange by readers and writers alike; the loser
    // drops. Mutable so the const diagnostic accessors can take it.
    mutable std::atomic<bool> busy{false};

    // The function. Sized to MAX_PAIRS at construction and never resized; only
    // the first `count` pairs are live, and they are always sorted ascending by
    // x — every question this object answers depends on that.
    std::vector<Pair> pairs;
    std::size_t count = 0;

    // Inlet 1's waiting y, consumed by the next x on inlet 0 — Max's "paired
    // with the next x value received in the left inlet". Guarded rather than
    // atomic: the flag and the value have to be read and cleared together, and
    // two atomics could be seen half-updated by the hot inlet.
    int pendingY = 0;
    bool hasPendingY = false;

    // The traversal. `pointer` is the x `next` searches from and `lastX` the x
    // it last reported, which is what outlet 1's difference is measured against.
    // Run-time position, so neither survives a save.
    int pointer = 0;
    int lastX = 0;

    // Max's embed flag: off until a patch turns it on, and the gate on whether
    // the contents are written into a saved patch at all. Saved alongside them,
    // so a reloaded object still knows to embed itself next time.
    bool embed = false;

    // The creation argument, Max's filename. Control thread only, and read for
    // nothing at all — there is no file I/O here. Held so that a `.funbuff
    // mydata` brought across from Max builds, and so the argument survives a
    // save unchanged.
    std::string filename;
  };

} // namespace PATCHER
} // namespace YSE
