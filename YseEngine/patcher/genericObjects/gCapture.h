#pragma once
#include "../pObject.h"
#include <atomic>
#include <cstddef>
#include <string>
#include <vector>

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief A rolling record of the values that went past — ``.capture``
     *         (issue #496).
     *
     *  Max's ``capture``, "store values to view or edit": "stores items in the
     *  order they are received for viewing, editing, and saving."
     *
     *  ### What it is for
     *
     *  The patcher's debugging instrument. When a graph misbehaves the question
     *  is always *what actually flowed through it*, and until this object there
     *  was no way to ask it from inside a patch: ``.print`` says what is passing
     *  **now**, one line at a time, and by the time a patch has gone wrong the
     *  interesting values are already gone. This keeps the last N of them, so a
     *  patch can be run, stopped, and only then asked what it saw.
     *
     *  It is not ``.coll`` (#494) and not ``.bag`` (#495), the two stores it
     *  sits next to. Both of those are *written by intent*: a patch decides what
     *  goes in and at what address, and the contents are the patch's data.
     *  Nothing decides what goes in here — every value that reaches the inlet is
     *  recorded, in arrival order, and the oldest falls off the end. It is a
     *  tape, not a table. Nor is it ``.bucket`` (#478), which also remembers the
     *  last N values but hands them to N *outlets* as a shift register: this has
     *  one outlet and hands its contents back only when asked.
     *
     *  ### One item is one atom
     *
     *  Max's ``int``, ``float``, ``list`` and ``anything`` methods all store
     *  "numbers or symbols ... in the order in which they are received", and for
     *  a list "all numbers and/or symbols in the list are stored in order from
     *  first to last". So a three-element list is **three** items and not one,
     *  and ``dump`` hands them back one at a time. That is the whole reason the
     *  object is useful as a trace: a patch that sent ``60 100`` and then ``64``
     *  gets ``60 100 64`` back, in the order it sent them, with no framing to
     *  unpick.
     *
     *  Each item leaves as the kind it arrived as — ``.route``'s rule, which
     *  ``.bucket`` and ``.coll`` already follow — so a stored ``60`` comes back
     *  as the int 60 and a stored ``60.5`` as that float. A stored symbol leaves
     *  as a one-element list, because the patcher has no symbol atom; Max
     *  prefixes it with the word ``symbol``, which is Max's way of restoring an
     *  atom type this patcher does not have, and inventing a word here would put
     *  a token in the trace that nothing put into it.
     *
     *  ### The ring, and the creation argument
     *
     *  Max: "the first argument sets a maximum number of items to store. If
     *  there is no argument, ``capture`` will store up to 512 items. Once the
     *  maximum has been exceeded, the earliest stored item is dropped as each
     *  new item is received." So it wraps rather than refusing, which is the
     *  opposite of ``.coll`` and ``.bag`` — and it is right for a trace, where
     *  the *recent* past is the interesting part and a full buffer that stopped
     *  recording would be a trace of the wrong moment.
     *
     *  512 is Max's default and also this object's ceiling: the table is
     *  allocated whole at ``MAX_ITEMS`` at construction and a smaller creation
     *  argument only lowers how much of it is used, so no capacity a patch can
     *  ask for ever allocates. A larger one is clamped rather than honoured, the
     *  way ``.bucket`` clamps its outlet count.
     *
     *  ### ``count`` counts arrivals, not contents
     *
     *  The one message whose name invites a wrong guess. Max: "sends the number
     *  of items collected **since the last count message** out the right outlet
     *  ... upon receipt of the ``count`` message, the object's internal count
     *  will be reset to 0 unless ``flag`` is set." It is a *since-you-last-asked*
     *  meter and not a fill level, so it counts every item that arrived —
     *  including the ones the ring has already dropped — and it can therefore
     *  report far more than the object is holding. ``count 1`` reads it without
     *  resetting it; a bare ``count``, or ``count 0``, reads and zeroes it.
     *
     *  ``.bag``'s ``length`` is the message that reports a fill level, and it is
     *  deliberately *not* spelled here: Max does not give ``capture`` one, and
     *  giving this object a ``length`` that answered a different question from
     *  ``count`` on the same inlet would be a trap. ``dump`` is how a patch finds
     *  out how much is in there.
     *
     *  ### Reserved words, and the three that do nothing
     *
     *  ``clear``, ``dump`` and ``count`` are commands when they lead a message,
     *  and so — deliberately — are ``open``, ``wclose`` and ``write``, which are
     *  accepted and ignored. Reserving a word on an inlet that carries arbitrary
     *  text is the thing the ``.prepend`` / ``.atoi`` discipline warns against,
     *  and ``.coll``'s exemption (its inlet is a command inlet) does not apply
     *  here: this inlet is a **data** inlet, whose whole job is to swallow
     *  whatever it is given.
     *
     *  Max settles it anyway. Max dispatches on the selector, so a ``capture`` in
     *  Max cannot store the symbol ``open`` either — sending it opens a window
     *  instead. An object that stored it would produce a *different trace* from
     *  Max's for the same patch, which is the one thing a debugging instrument
     *  must not do. So all six words are consumed, and the three with no headless
     *  meaning are consumed silently: there is no window to open or close, and
     *  ``write`` is file I/O on a path that may be the audio thread.
     *
     *  ### Real-time behaviour
     *
     *  ``.coll``'s storage model and for ``.coll``'s reason: a fixed table
     *  allocated whole at construction and never resized, plus ``.value``'s
     *  non-blocking ``busy`` guard. A copy-on-write ``GraphState`` publish
     *  assumes the writer is the control thread, and this object is written by
     *  whichever thread its message arrived on — in-patcher delivery dispatches
     *  on **T_DSP** — so a value reaching it from a rendering graph would have to
     *  allocate a replacement table there. Recording a value is a copy into
     *  storage that already exists; a symbol longer than ``ITEM_CAPACITY`` is
     *  refused whole rather than truncated, since half a symbol is a different
     *  symbol and a trace that quietly rewrote what it saw would be worse than
     *  one with a hole in it.
     *
     *  The guard is claimed with a single ``exchange`` and whoever loses **drops**
     *  its operation rather than spinning, and it is never held across a send.
     *  ``dump`` takes it once per item rather than once for the whole walk —
     *  ``.coll``'s dump exactly — so a patch whose dump target writes back into
     *  this object is not locked out of its own store, and the bounds are re-read
     *  every step so a wrap that happened mid-dump cannot walk off the end.
     *  ``count`` reads and zeroes the meter under the guard and sends after
     *  releasing it, so a value recorded by a re-entrant send is counted rather
     *  than swallowed by the reset.
     *
     *  ``Calculate()`` does nothing: the object is driven by its inlet, and one
     *  that emitted would re-dump on every DSP tick — the rule ``.route``,
     *  ``.sel``, ``.value``, ``.bucket``, ``.coll`` and ``.bag`` establish.
     *
     *  ### What persists, and what does not
     *
     *  The capacity, because it is a creation argument. The **contents**
     *  deliberately do not, which is ``.bag``'s answer and for a stronger reason:
     *  Max gives ``coll`` a "save data with patcher" flag — the feature that made
     *  ``pObject`` grow ``DumpState`` / ``RestoreState`` — and gives ``capture``
     *  none. Max's ``write`` message is how a ``capture``'s contents are saved,
     *  to a text file, on demand. And a trace is by definition the record of a
     *  *run*: a reloaded patch that came back holding the values from the session
     *  it was saved in would be showing a debugging tool's answer to a question
     *  nobody had asked yet.
     *
     *  ### Deliberately not here
     *
     *  The editor window and everything that addresses it — ``open``, ``wclose``,
     *  the double-click, the ``precision`` attribute, and the second creation
     *  argument (Max's ``a`` / ``x`` / ``m`` display format), which changes only
     *  how numbers are *drawn* in that window. The argument is accepted and
     *  ignored so that a ``.capture 512 x`` brought across from Max still builds
     *  the object it names. Also ``write`` (file I/O), and the ``listout`` and
     *  ``size`` attributes: the patcher has no attribute mechanism, and ``size``
     *  in particular carries a re-capacity rule ("if the current capture position
     *  is already greater than the new size, it will be reset to 0") that is a
     *  feature of its own rather than a detail of this one.
     */
    PATCHER_CLASS(gCapture, YSE::OBJ::G_CAPTURE)
    _NO_MESSAGES
    _NO_CALCULATE

    _INT_IN(IntIn)
    _FLOAT_IN(FloatIn)
    _LIST_IN(ListIn)

    _PARM_CLEAR
    _PARM_PARSE

    /**
     *  @brief Most items the record can hold — 512.
     *
     *  Max's documented default ("capture will store up to 512 items"), and so
     *  also the ceiling here: the table is allocated at this size once, and the
     *  creation argument only decides how much of it is used. A larger argument
     *  is clamped rather than honoured, since honouring it would mean allocating
     *  a table a message path might later have to grow.
     */
    static constexpr std::size_t MAX_ITEMS = 512;

    /** @brief Items with no creation argument — Max's "up to 512 items". */
    static constexpr std::size_t DEFAULT_ITEMS = 512;

    /** @brief Fewest items a creation argument can ask for. A zero-item record
     *         would silently discard everything, so 1 is the floor. */
    static constexpr std::size_t MIN_ITEMS = 1;

    /** @brief Longest symbol a single item can hold, in characters. ``.coll``'s
     *         address capacity: a token longer than this is refused rather than
     *         truncated. */
    static constexpr std::size_t ITEM_CAPACITY = 64;

    /** @brief How many items are stored right now — never more than
     *         ``Capacity()``. */
    std::size_t Count() const;

    /** @brief How many items this object will hold before the oldest starts
     *         falling off, from the creation argument. */
    std::size_t Capacity() const {
      return capacity;
    }

    /** @brief What a ``count`` message would report: items received since the
     *         last one reset the meter. Not a fill level — see the class
     *         documentation. */
    int Received() const;

    /** @brief Whether the item at @p position — 0 the oldest — is a symbol
     *         rather than a number. False out of range. Diagnostics and tests:
     *         control thread only. */
    bool IsSymbolAt(std::size_t position) const;

    /** @brief The number at @p position in arrival order, 0 the oldest. 0 out of
     *         range or on a symbol. Same contract as ``IsSymbolAt``. */
    float NumberAt(std::size_t position) const;

    /** @brief The symbol at @p position in arrival order, or ``""``. Returns a
     *         copy, so control thread only. */
    std::string SymbolAt(std::size_t position) const;

  private:
    // One recorded atom. The spelling travels with a number because the object
    // records rather than computes: an int that came back out as a float would
    // rewrite the type of everything the trace saw.
    struct Item {
      bool isSymbol = false;
      bool isFloat = false;
      float number = 0.f;
      // Reserved to ITEM_CAPACITY at construction and never grown, so writing
      // one is an assign() into storage that exists.
      std::string text;
    };

    /**
     *  @brief Non-blocking exclusive access to the record.
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

    // Physical slot of the item at `position` in arrival order. Guard held.
    std::size_t SlotOf(std::size_t position) const {
      return (first + position) % MAX_ITEMS;
    }

    // Record one item, dropping the oldest when the record is already full —
    // Max's "the earliest stored item is dropped as each new item is received".
    // Takes the guard itself and does nothing at all when it loses it.
    void Record(bool isSymbol, bool isFloat, float number, const char* text, std::size_t length);

    // Record one whitespace-separated token, as the number it spells or as the
    // symbol it is. A symbol longer than ITEM_CAPACITY is refused whole.
    void RecordToken(const char* token, std::size_t length);

    // Take the guard, copy item `position` into the send buffer, release it, and
    // send it out outlet 0 in the kind it was recorded as. False when there is no
    // such item, in which case nothing is sent — which is what stops the dump
    // walk. The guard is deliberately not held across the send.
    bool Output(std::size_t position, YSE::THREAD thread);

    // The command half of the inlet. Returns false when `text` is none of them,
    // leaving the caller to record the message as data.
    bool HandleCommand(const char* text, std::size_t length, const std::string& message,
                       std::size_t argOffset, YSE::THREAD thread);

    // Claimed with a single exchange by readers and writers alike; the loser
    // drops. Mutable so the const diagnostic accessors can take it.
    mutable std::atomic<bool> busy{false};

    // The ring. Sized to MAX_ITEMS at construction and never resized; `first` is
    // the physical slot of the oldest live item and `count` how many are live.
    std::vector<Item> items;
    std::size_t first = 0;
    std::size_t count = 0;

    // How much of the table the creation argument asked for, in [MIN_ITEMS,
    // MAX_ITEMS]. Never changes the table's size — only how full it gets before
    // the oldest item starts falling off.
    std::size_t capacity = DEFAULT_ITEMS;

    // Max's count meter: items received since the last `count` reset it, not
    // items held. Saturates at INT_MAX rather than overflowing, which is
    // undefined for a signed int.
    int received = 0;

    // What a send is made from, reserved at construction. A copy rather than the
    // item itself: the send path is synchronous, so handing an outlet the stored
    // string would let a patch that records into this object from downstream
    // mutate the very message still being fanned out.
    std::string sendText;

    // The creation arguments, as tokens. Control thread only: written by
    // Parameters::Set, read by ParseParams(), never by a message handler.
    std::vector<std::string> creationArgs;
  };

} // namespace PATCHER
} // namespace YSE
