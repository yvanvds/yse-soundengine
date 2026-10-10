#pragma once
#include "../pObject.h"
#include <string>

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief Emit N bangs immediately, with a running index — ``.uzi``
     *         (issue #473).
     *
     *  Max's ``uzi``, whose summary line is "Send many bang messages":
     *  "Outputs a specified number of bang messages quickly. ``uzi`` is
     *  designed for rapid-fire output of a large number of ``bang`` messages."
     *
     *  This is the patcher's **loop**. Until now there was no way to iterate at
     *  all: every other object answers one message with one message, so filling
     *  a table, building a chord or spawning a burst of grains had to come from
     *  outside the patch. ``.uzi 16`` is sixteen iterations with an index, in
     *  one box, inside one stimulus.
     *
     *  ### Three outlets, and the order is the object
     *
     *  Max's shape, left to right: bang, carry, index. Per iteration the
     *  **index goes out first and the bang second**, which is Max's universal
     *  right-to-left outlet order and, here, the whole point — the canonical
     *  idiom wires the index into whatever holds the loop variable (a ``.i``, a
     *  ``.expr``'s cold inlet) and the bang into whatever reads it, so an index
     *  arriving *after* its bang would make every iteration read the previous
     *  one's number. Max's wording is "The number of each bang is sent out": the
     *  number and the bang are one pair, not two independent streams.
     *
     *  The carry is the exception to right-to-left, and Max states it
     *  explicitly rather than leaving it to the rule: "After the last bang is
     *  sent out its left outlet, ``uzi`` sends one bang out its middle outlet.
     *  This can be used as a signal that all the bang messages have been sent,
     *  much like the 'carry' outlet on the ``counter`` object." So ``.uzi 3``
     *  produces, in this exact order:
     *
     *      index 1, bang, index 2, bang, index 3, bang, carry
     *
     *  Each send completes in full — the whole subgraph hanging off that
     *  outlet, depth first — before the next one starts, for the reason
     *  ``.trigger`` and ``.bangbang`` give: ``outlet::Send*`` walks its target
     *  list calling the target inlet directly with no queue in between. That is
     *  what makes the loop a *loop* and not a scatter.
     *
     *  ### The index is 1-based
     *
     *  Max: "Numbering begins from 1 (or from the base value specified by the
     *  optional second argument)." The second creation argument moves it —
     *  ``.uzi 8 0`` is the 0-based loop a table index usually wants.
     *
     *  ### One logical event
     *
     *  Max's own reference for ``next`` cites this object as *the* example of
     *  many messages inside one event: "if you put ``bang, bang`` in a message
     *  box, or use the ``uzi`` object to send out two bangs in a row, these
     *  bangs are part of the same logical event." That holds here for free and
     *  for the right reason: the whole burst runs inside the call frame of the
     *  one ``inlet::Set*`` that started it, and ``CurrentMessageEvent()``
     *  (#471) hands out one id per *outermost* dispatch. Indices, bangs and the
     *  carry therefore all share one event, so ``.uzi`` into ``.next`` gives one
     *  bang out the separated outlet and the rest out the continued one,
     *  whatever the count. ``resume`` is a new stimulus and so a new event —
     *  correctly, since it is a new thing the patch did.
     *
     *  ### Bounding — the part that is not Max's
     *
     *  The send path is synchronous and re-entrant, so an unbounded ``.uzi`` is
     *  not a slow object, it is a hang. Three separate things could run away,
     *  and each is closed by construction rather than by the ``kMaxSendDepth``
     *  backstop in ``outlet.cpp``, which cannot help with any of them:
     *
     *  1. **A huge count.** The count is clamped to ``MAX_COUNT`` (4096), so one
     *     stimulus can never cost more than 4096 iterations *of this object*.
     *     4096 is the ceiling ``.urn`` already uses — Max's own documented
     *     ``1-4096`` for that object — so it is not a new number invented here,
     *     and it is far above the practical counts (a 1024-point table, 128
     *     MIDI notes, a few hundred grains). It is a bound on the *work*, not a
     *     promise the work fits an audio block: a long ``.uzi`` into a deep
     *     subgraph is expensive by construction, here as in Max. What the cap
     *     buys is that the cost is bounded and predictable rather than a
     *     function of a number the patch computed.
     *
     *     A count above the cap is **reported, not silently truncated**. On the
     *     creation argument, which is read on the control thread, it is logged.
     *     On a message, where logging would allocate on whatever thread the
     *     message arrived on, ``RequestedCount()`` keeps what was asked for
     *     next to ``Count()``, and ``CountWasClamped()`` says the two differ —
     *     so the truncation is observable from outside the object rather than
     *     invisible.
     *
     *  2. **``.uzi`` reaching its own start inlet.** A bang outlet wired back —
     *     directly, through a chain, or through a second ``.uzi`` — re-enters
     *     the start handler *inside* the send. Starting a nested run there is
     *     unbounded recursion, and not merely deep: with a count of N the tree
     *     has N children per level, so the send-depth ceiling of 64 is reached
     *     only after N^64 iterations. So a start that arrives while this object
     *     is running is **refused**, and counted in ``RefusedStarts()``. There
     *     is no sensible alternative: restarting would reset the counter under
     *     the running loop and never terminate, and running a second interleaved
     *     loop over one shared counter produces neither loop's output. Distinct
     *     objects nest normally — the guard is per object.
     *
     *  3. **The count moving under a running loop.** The subtle one. The right
     *     inlet sets the count *without* output, so it is not a start and the
     *     guard above never sees it — and a patch that wires the bang outlet
     *     through a ``.+ 1`` into the right inlet raises the loop bound once per
     *     iteration, forever. So the bound is **pinned at the moment the run
     *     starts**: ``runLength`` is a snapshot of ``count``, and a count (or
     *     ``offset``) written during a run applies to the *next* one. The loop
     *     is therefore bounded by ``MAX_COUNT`` iterations no matter what the
     *     patch does re-entrantly, which is the property that makes the
     *     recursion case terminate by design rather than by luck.
     *
     *  Note on ``.s``/``.r``: the patcher's value-command queue (#225) is 256
     *  deep, so a ``.uzi`` longer than that feeding a ``.s`` from the control
     *  thread will outrun it within a single burst and hit the queue's
     *  documented backpressure — drop and log. That is the queue's contract
     *  rather than something this object can fix; it is a reason to keep bursts
     *  that cross the send/receive boundary short, not a reason to cap ``.uzi``
     *  at 256 and lose the table-filling use the object exists for.
     *
     *  ### ``pause`` / ``resume``, and the escape hatch
     *
     *  Max: "``pause``: Causes ``uzi`` to stop in the midst of sending its
     *  output. (Since ``uzi`` sends its output as fast as possible, this message
     *  must be triggered in some way by the output of ``uzi`` itself.)" — so the
     *  reference is explicit that ``pause`` arrives *re-entrantly*, from inside
     *  the burst, which is exactly what the synchronous send path delivers. It
     *  is how a patch breaks out of a loop early: search a table with ``.uzi
     *  4096`` and ``pause`` the moment the value is found.
     *
     *  The flag is read at the **top** of each iteration, so an iteration is
     *  atomic: a ``pause`` raised part-way through never leaves an index
     *  without its bang. ``resume`` (Max's ``continue``, and ``break`` for
     *  ``pause``) picks the run up where it stopped — "If ``uzi`` is being
     *  restarted with a ``resume`` or ``continue`` message, numbering begins
     *  wherever it left off" — using the *pinned* bound, so the total across a
     *  paused-and-resumed run is still at most ``MAX_COUNT``. A paused run emits
     *  **no carry**; the carry means "all the bang messages have been sent", and
     *  after a ``pause`` they have not been.
     *
     *  ``pause`` outside a run is a no-op rather than arming a pause for later,
     *  since Max scopes the message to a run in progress and a stored pause
     *  would make the next ``resume`` fire a carry for a run nobody started.
     *
     *  ### ``offset``
     *
     *  Max: "The word ``offset`` followed by a number of bangs will set the
     *  object to output a count of bangs which is offset by the given number
     *  (the number is subtracted from the previously assigned number of bangs
     *  to equal the new total number of bangs)." So ``offset k`` skips the first
     *  *k* iterations: the run emits ``count - k`` bangs with indices
     *  ``base+k`` … ``base+count-1``. It persists until changed, and ``offset
     *  0`` restores the whole range.
     *
     *  ### What is *not* a trigger
     *
     *  Max lists no ``anything`` method for this object — only ``bang``,
     *  ``int``, the right inlet's ``int``, and the five words. So an
     *  unrecognised symbol is **ignored** rather than converted to a bang, which
     *  is the opposite of ``.bangbang`` and ``.onebang`` and deliberately so:
     *  there a stray message costs one bang, here it would cost up to 4096, and
     *  an object that fires a loop on any text that reaches it is a trap rather
     *  than a convenience.
     *
     *  Floats are accepted on both inlets and truncated towards zero, as Max
     *  converts a float to an int; the patcher has one numeric type and a count
     *  that ignored ``4.0`` would be worse than one that reads it as 4.
     *
     *  ### Real-time behaviour
     *
     *  ``Calculate()`` does nothing. The object is driven by its inlets, and an
     *  emitting ``Calculate()`` would run the whole loop again on every DSP
     *  block from a stimulus no patch sent — the family's rule, and here the
     *  most expensive possible way to break it.
     *
     *  Nothing on any path allocates, locks or blocks. The emit loop is integer
     *  arithmetic and two ``Send`` calls per iteration, neither of which
     *  converts or formats anything. The only text path is the message word
     *  test: two bare comparisons against fixed literals plus ``MatchWord`` /
     *  ``ReadIntArgAt`` from ``pListArgs.h``, none of which allocate, throw or
     *  read locale state. The creation arguments are read on the control thread
     *  by the parameter callbacks, before the object is wired or published.
     *
     *  The state fields are plain, not atomic, as ``.onebang``'s and
     *  ``.next``'s are: they describe one stimulus being delivered, and the
     *  patcher's contract is one stimulus at a time per object.
     */
    PATCHER_CLASS(gUzi, YSE::OBJ::G_UZI)
    _NO_MESSAGES
    _NO_CALCULATE

    _BANG_IN(SetBang)
    _INT_IN(SetInt)
    _FLOAT_IN(SetFloat)
    _LIST_IN(SetList)

    _PARM_CLEAR
    _PARM_PARSE

    _HAS_GUI

    /**
     *  @brief Fewest bangs a run will emit.
     *
     *  0. A negative or zero count is a loop that does not run, which is a
     *  shape a patch computing its count from a list length genuinely produces;
     *  it is not an error, and the carry still fires so the "and afterwards, do
     *  this" branch is not silently skipped.
     */
    static constexpr int MIN_COUNT = 0;

    /**
     *  @brief Most bangs a single run will emit — the documented cap.
     *
     *  4096. Max documents no limit at all, which is not a bound this patcher
     *  can adopt: the send path is synchronous, so the count is the number of
     *  subgraph traversals one message costs on whichever thread dispatched it.
     *  4096 is the ceiling ``.urn`` already uses (Max's own ``1-4096`` for that
     *  object), well above the practical iteration counts and low enough that
     *  one stimulus has a knowable worst case. A larger count is clamped and
     *  reported — see ``RequestedCount()`` and ``CountWasClamped()``.
     */
    static constexpr int MAX_COUNT = 4096;

    /**
     *  @brief What a bare ``.uzi`` emits.
     *
     *  1 — Max: "If no argument is present, ``uzi`` is initially set to send out
     *  one ``bang``."
     */
    static constexpr int DEFAULT_COUNT = 1;

    /**
     *  @brief Where the index starts when no second argument is given.
     *
     *  1 — Max: "The base value defaults to 1 when no second argument is
     *  given."
     */
    static constexpr int DEFAULT_BASE = 1;

    /**
     *  @brief How far from zero the index base may be set.
     *
     *  1e9. Not a musical limit — it is what makes ``base + progress`` total:
     *  with the count capped at 4096, a base inside this range can never carry
     *  the index past the ``int`` range, so the emit loop needs no overflow
     *  check on its hottest line.
     */
    static constexpr int BASE_LIMIT = 1000000000;

    /** @brief How many bangs the next run will emit — always in
     *         ``[MIN_COUNT, MAX_COUNT]``, before any ``offset``. */
    int Count() const {
      return count;
    }

    /**
     *  @brief The count as it was last asked for, before clamping.
     *
     *  The report half of the cap. Equal to ``Count()`` whenever nothing was
     *  truncated, so a patch or a host can tell "4096 bangs" from "4096 bangs
     *  because you asked for a million".
     */
    int RequestedCount() const {
      return requestedCount;
    }

    /** @brief Whether the last count set was clamped by the cap or the floor. */
    bool CountWasClamped() const {
      return requestedCount != count;
    }

    /** @brief The value the index starts from — Max's ``base``, default 1. */
    int Base() const {
      return base;
    }

    /** @brief How many leading iterations ``offset`` skips. */
    int Offset() const {
      return offset;
    }

    /**
     *  @brief How far the current or last run got.
     *
     *  Counted in iterations, not in index values, so it is comparable with
     *  ``Count()`` whatever the base. This is where a ``resume`` picks up.
     */
    int Progress() const {
      return progress;
    }

    /** @brief Whether a run is in progress on this object right now. True only
     *         inside the burst, which is where a re-entrant message sees it. */
    bool IsRunning() const {
      return running;
    }

    /** @brief Whether a run was interrupted by ``pause`` and is waiting for a
     *         ``resume``. */
    bool IsPaused() const {
      return paused;
    }

    /**
     *  @brief How many starts have been refused because a run was already in
     *         progress.
     *
     *  Exposed so the recursion guard is observable rather than silent: a patch
     *  whose ``.uzi`` reaches its own inlet is doing something it probably did
     *  not mean to, and "nothing happened" cannot be told from "it was refused"
     *  any other way.
     */
    int RefusedStarts() const {
      return refusedStarts;
    }

  private:
    // Begin a run from the top: pin the bound, seed the counter from `offset`,
    // and emit. Refused while a run is in progress — see the header.
    void Start(YSE::THREAD thread);

    // Continue a run that `pause` interrupted, from wherever it stopped.
    void Resume(YSE::THREAD thread);

    // The loop itself, and the only path that emits. Bounded by `runLength`,
    // which no message handler can move while it is running.
    void Emit(YSE::THREAD thread);

    // Set the count from a number that arrived on either inlet, clamping into
    // [MIN_COUNT, MAX_COUNT] and recording what was asked for. Never logs: this
    // runs on whichever thread sent the message.
    void ApplyCount(int value);

    // The creation arguments, as text. Control thread only: written by
    // Parameters::Set, read by ParseParams(), never by a message handler.
    std::string countArg;
    std::string baseArg;

    // How many bangs the *next* run emits. Written by the creation argument and
    // by either inlet; read by Start() and copied into `runLength` there, so
    // writing it during a run cannot move the running bound.
    int count = DEFAULT_COUNT;

    // What was asked for before clamping, for CountWasClamped().
    int requestedCount = DEFAULT_COUNT;

    // What the index counts from. Clamped to +/- BASE_LIMIT so `base +
    // progress` cannot overflow.
    int base = DEFAULT_BASE;

    // How many leading iterations to skip — Max's `offset` message. Applied at
    // the start of a run, so writing it during one takes effect at the next.
    int offset = 0;

    // The pinned bound of the run in progress (or of the last one). The whole
    // termination argument rests on this being a snapshot rather than a read of
    // `count`.
    int runLength = 0;

    // How many iterations of the current run have been started. Advanced before
    // each iteration's sends, so a `pause` raised from inside them resumes at
    // the *next* iteration rather than repeating this one.
    int progress = 0;

    // The re-entrancy guard. True only between the first send of a run and the
    // last, which is exactly when a looped-back message arrives.
    bool running = false;

    // Set by `pause` from inside a run; read at the top of each iteration.
    // Cleared when a run starts or resumes.
    bool paused = false;

    // How many starts the guard refused. Diagnostic only.
    int refusedStarts = 0;
  };
}
}
