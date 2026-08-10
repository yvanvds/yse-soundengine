#pragma once
#include "../pObject.h"
#include "../time/clockBridge.h"
#include "../time/messageScheduler.h"
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief Shared body for ``.speedlim`` and ``.qlim`` — limit the rate of
     *         message throughput (issues #508, #728).
     *
     *  Both objects pass a message straight through when enough time has
     *  elapsed since the last one they let out, and both do something else when
     *  it has not. *What* that something else is is the whole difference
     *  between them, and it is the only thing the two subclasses below
     *  implement:
     *
     *  - ``.speedlim`` **drops** the message that arrived too soon. The stream
     *    is thinned to at most one message per window, and what comes out is
     *    the *first* message of each burst, at the moment it arrived.
     *  - ``.qlim`` **holds** it and sends it when the window opens. Nothing is
     *    lost except the values a newer one replaced, and what comes out is the
     *    *last* message of each burst, one window after the previous output —
     *    or, with ``usurp 0``, *every* message of the burst, one per window
     *    (#728).
     *
     *  Rate-limiting for control data is what both are for. A gesture source —
     *  a fader, a sensor, a ``.drunk`` on a fast ``.metro`` — emits far faster
     *  than anything downstream needs, and unbounded control traffic is a real
     *  hazard here rather than a theoretical one: the patcher's cross-thread
     *  value queue (#225) and its deferred-message scheduler (#628) are both
     *  bounded, and a flood spends that budget on messages nobody will look at.
     *  Which of the two objects to reach for is the question of whether a
     *  dropped value matters. For "redraw at most 20 times a second" it does
     *  not and ``.speedlim`` is cheaper — it never defers anything at all. For
     *  a fader driving a filter cutoff it does: dropping the last value of a
     *  gesture leaves the filter wherever the last message that happened to fit
     *  the window put it, and only ``.qlim`` guarantees that where the fader
     *  stopped is where the cutoff ends up.
     *
     *  ### The departure from Max, stated plainly
     *
     *  Max ships both names and its two reference pages describe the *same*
     *  throughput rule for each: "the message is passed out the outlet, provided
     *  that a certain minimum time has elapsed since the previous output.
     *  Otherwise, the message is held until that amount of time has passed (or
     *  until it is overwritten by another incoming message)." Both objects then
     *  carry an ``usurp`` attribute (default on) for the overwrite half of that
     *  sentence. What actually separates Max's pair is *scheduler priority*:
     *  ``qlim`` exists because "the speedlim object unfortunately places
     *  messages back in the scheduler for execution", which is unsafe for
     *  Jitter matrix traffic at interrupt, and ``qlim`` is "an interrupt safe
     *  replacement".
     *
     *  That distinction has no meaning in this patcher. There is one dispatch
     *  model, every deferred message comes back through the same
     *  ``messageScheduler`` inside the same audio-block dispatch (#628), and
     *  there is no low-priority queue to defer into and no drawing thread to
     *  protect. Reproducing Max faithfully would therefore give two objects
     *  that are byte-for-byte identical, which is worse than useless — a patch
     *  reading ``.speedlim`` next to ``.qlim`` would reasonably expect them to
     *  differ. So issue #508 assigns the two names the two policies a headless
     *  patcher *can* tell apart: ``.qlim`` is Max's shared behaviour with usurp
     *  on, and ``.speedlim`` is the drop policy, which is both the thing many
     *  patchers assume ``speedlim`` already does and the only other sensible
     *  answer to a message that arrived too soon. Getting the two backwards is
     *  the classic mistake, so each object's own description says which it is in
     *  its first sentence.
     *
     *  That split is what decides where Max's ``usurp`` lives here. ``usurp``
     *  is a statement about a message that is *waiting*, and only ``.qlim``
     *  ever has one, so the attribute is ``.qlim``'s alone — see ``gQlim``.
     *  Max's ``defer`` attribute stays out for the same reason the priority
     *  split does: it selects the low-priority queue, and there is one.
     *
     *  ### Where the time comes from, and what "elapsed" means
     *
     *  The patcher's block counter, read through ``messageScheduler::Now()`` and
     *  converted with ``messageScheduler::MillisForBlocks``. This is the clock
     *  ``.mtr`` and ``.seq`` already *measure* on, and it is chosen for the
     *  reason those two chose it: an object that measures an interval must use
     *  the same clock as the scheduler that waits one out, or a window computed
     *  here and a wait armed there would drift apart. It is also the only clock
     *  in the patcher that stops when the engine does, so a paused patch does
     *  not silently accumulate a window's worth of credit while nothing is
     *  rendering.
     *
     *  Its resolution is one audio block, which is the honest limit of both
     *  objects: two messages arriving in the same dispatch are zero milliseconds
     *  apart no matter how much wall-clock time separates them, and an interval
     *  shorter than a block cannot thin anything. ``steady_clock`` would read
     *  finer, and ``.clocker`` uses it for exactly that reason — but ``.clocker``
     *  only *reports* a number, whereas ``.qlim`` has to arm a wait on the block
     *  clock afterwards, and mixing the two would let a value be released before
     *  the interval it was measured against had elapsed.
     *
     *  An interval of 0 — Max's default, "if there is no argument, the minimum
     *  time is 0 milliseconds" — limits nothing: every message passes, on both
     *  objects. That is deliberately *not* the same reading the scheduler gives
     *  a delay of 0 (which defers one block); a rate limiter set to no limit is
     *  a wire, and a wire does not defer.
     *
     *  ### …or a domain clock (issue #728)
     *
     *  Max's ``speedlim`` says the time "can be specified in milliseconds or
     *  using a tempo-relative interval", and its ``threshold`` and ``quantize``
     *  attributes take the same time-value syntax. Since #728 both objects read
     *  the tempo-relative half of it, exactly the way ``.delay`` (#705) and
     *  ``.metro`` do and through the same two pieces:
     *
     *  - **``clock <name>``** in the left inlet binds a ``CLOCK::domainClock``
     *    through ``PATCHER::clockBridge``, and a bare ``clock`` takes it away.
     *    Max's own ``setclock`` method, verbatim in shape.
     *  - **A note value (``4n``, ``4nd``, ``8nt``) or a tick count
     *    (``1440 ticks``)** as the interval, the threshold or the quantize grid
     *    is that time *in beats* on the bound clock. The arithmetic is shared
     *    in ``time/timeValue.h`` rather than copied, for ``pSelector``'s reason
     *    (#680): two objects that read the same syntax must read it the same
     *    way. A plain number is milliseconds and puts the value back on
     *    milliseconds, which is Max's model — the unit travels with the value,
     *    and ``clock`` only decides which clock a beat is counted on.
     *
     *  What a beat window buys is what milliseconds cannot: a limiter that
     *  follows the domain's tempo changes and ramps, that stays in step with
     *  every ``YSE::clip`` on that domain, and that holds where it stands when
     *  the domain pauses.
     *
     *  ``bars.beats.units`` stays out, and cannot come in: a bar needs a meter
     *  and ``CLOCK::domainClock`` is a bare beat accumulator. See
     *  ``time/timeValue.h``.
     *
     *  **A tempo-relative time with no clock bound is inert, not fatal.**
     *  ``.delay`` answers this dead end by arming nothing, and that is right
     *  for an object that holds one bang: the loss is bounded. It would be
     *  wrong here. A limiter whose window never opens is a black hole that
     *  swallows *every* message from then on, so an interval given in beats
     *  with no clock to measure them on simply limits nothing — the same answer
     *  a standalone object already gets, and the one that cannot lose a stream.
     *  A beat threshold with no clock does not bite either, and a quantize grid
     *  with no clock does not quantize. Binding a clock afterwards makes all
     *  three live; the messages may arrive in either order.
     *
     *  ### ``threshold`` — "only one message may pass" (issue #728)
     *
     *  Max, on both objects: "Time threshold under which only one message may
     *  pass." It is a second, independent window measured from the last output,
     *  and inside it a message is **dropped outright** — not held, not queued,
     *  on either object. Max's wording is terse and this is its literal
     *  reading: within a threshold of an output, one message has passed and no
     *  other one may.
     *
     *  With the default of 0 it does nothing, which is why every #508 property
     *  still holds unchanged. Turned on it composes with the interval rather
     *  than replacing it, and the composition is the point:
     *
     *  - On ``.speedlim`` the effective drop window becomes the longer of the
     *    two, which is a rate cap that a live ``interval`` cannot be dropped
     *    below.
     *  - On ``.qlim`` it carves out the zone where a message is not even worth
     *    holding — and with ``usurp 0`` that is what keeps a flood from filling
     *    the queue, since a burst tighter than the threshold contributes one
     *    message rather than one per message.
     *
     *  ### ``quantize`` — output on the grid (issue #728)
     *
     *  Max: "Send output only on the specified time-boundary if appropriate.
     *  This is achieved by making internal adjustments to the times used for
     *  sending output." The grid is a beat grid on the bound clock — a note
     *  value or a tick count — and it moves the *release*, never the arrival:
     *
     *  - A message ``.qlim`` holds is released on the first grid line at or
     *    after the moment the interval is up, rather than the moment itself.
     *    When the interval is milliseconds, the beat that moment falls on is
     *    read from the clock's tempo at arm time; the line the release lands on
     *    is exact in beats either way, so a tempo change during the wait bends
     *    the wait rather than the grid.
     *  - A message that arrives when the interval is already up passes only if
     *    the grid has advanced past the line the last output sat on. That is
     *    Max's "if appropriate": a limiter cannot manufacture an output at a
     *    line nothing arrived on, so what a grid can promise is at most one
     *    output per line, and never one before the line.
     *
     *  On ``.speedlim``, which never defers, the second rule is the whole
     *  effect: one message per grid line, dropped in between.
     *
     *  ### Two honest edges
     *
     *  - **No patcher at all.** A standalone object has no scheduler and so no
     *    clock; "since the previous output" has no referent, and every message
     *    passes straight through. This is ``.pipe``'s and ``.mtr``'s answer to
     *    the same dead end and the one that keeps a standalone object testable
     *    rather than a black hole that eats every message after the first.
     *  - **Text longer than ``TEXT_CAPACITY``** cannot be *held*, so ``.qlim``
     *    refuses it and counts it rather than allocating on the arming thread,
     *    which is routinely the audio callback. It passes through fine when the
     *    window is open, and ``.speedlim`` never holds anything, so neither
     *    object has a length limit on the pass path.
     *
     *  ### One departure shared with ``.pipe``
     *
     *  A single-token numeric list arrives as the number it spells. A ``.m 5``
     *  wired into this inlet is a list message carrying "5", and a limiter that
     *  re-emitted it as a list would not reach the ``.i`` on the far side —
     *  there being no coercion at an inlet here. So the leading-token test
     *  ``.bondo``, ``.trigger`` and ``.pipe`` already use decides it: one token
     *  that is wholly a number is passed on as an int or a float by its
     *  spelling, and everything else travels as text. The message that comes out
     *  is the message that went in, which is the property that matters.
     *
     *  ### The one cost of the attributes: four command words
     *
     *  ``clock``, ``threshold``, ``quantize`` and (on ``.qlim``) ``usurp`` are
     *  matched as command words in the left inlet, so a list whose first token
     *  is one of them configures the object instead of travelling through it.
     *  Max has the same collision — they are attribute messages there — and it
     *  is the price of putting them where Max puts them. Everything else in the
     *  left inlet is still data.
     *
     *  ### Real-time behaviour
     *
     *  ``Calculate()`` does nothing: both objects are driven by their inlets and
     *  (for ``.qlim``) by the scheduler, and one that emitted would send a
     *  message on every DSP tick from a stimulus no patch sent. No path
     *  allocates, locks or blocks — the window test is a handful of atomic loads
     *  and some floating-point arithmetic, reading a beat or a tempo is two
     *  acquire loads through the bridge, binding a clock is the bridge's
     *  wait-free claim, ``.speedlim``'s refusal is one atomic increment, and
     *  ``.qlim``'s hold is one CAS plus an assign into a string the constructor
     *  reserved.
     */
    class gRateLimitBase : public pObject {
    public:
      gRateLimitBase();

      void SetMessage(const std::string&, float) override {}
      void Calculate(YSE::THREAD) override {}

      /**
       *  @brief The interval Max gives a limiter with no creation argument, in
       *         milliseconds — "if there is no argument, the minimum time is 0
       *         milliseconds", which is no limiting at all.
       */
      static constexpr int DEFAULT_INTERVAL = 0;

      /**
       *  @brief Longest text ``.qlim`` can hold inline — the same bound the
       *         scheduler and the #225 value queue accept. Longer text refuses
       *         the hold rather than truncating it or allocating.
       */
      static constexpr std::size_t TEXT_CAPACITY = 256;

      /**
       *  @brief The minimum time between outputs in milliseconds, as the next
       *         message to arrive will see it: the stored parameter with
       *         negatives and NaN clamped away.
       *
       *  Clamped on read rather than on write, which is ``.metro``'s,
       *  ``.delay``'s and ``.pipe``'s arrangement and for their reason — a live
       *  ``SetParams`` re-parse stores straight into the field from the audio
       *  thread and notifies nobody, so a clamp applied at the inlet would not
       *  cover that route.
       *
       *  Ignored while ``IntervalBeats()`` is positive: the unit travels with
       *  the value, so a tempo-relative interval replaces the millisecond one
       *  rather than adding to it (issue #728).
       */
      int Interval() const;

      /**
       *  @brief The tempo-relative interval in beats, or 0 when the interval is
       *         milliseconds (issue #728).
       *
       *  Set by a note value or a tick count and cleared by any plain number,
       *  which is Max's "the number is stored as the minimum amount of time, in
       *  milliseconds". Clamped on read like ``Interval()``.
       */
      double IntervalBeats() const;

      /**
       *  @brief Max's ``threshold`` in milliseconds — "time threshold under
       *         which only one message may pass" (issue #728). 0, the default,
       *         is off. Ignored while ``ThresholdBeats()`` is positive.
       */
      int Threshold() const;

      /** @brief The tempo-relative threshold in beats, or 0 when the threshold
       *         is milliseconds (issue #728). */
      double ThresholdBeats() const;

      /**
       *  @brief Max's ``quantize`` grid in beats, or 0 for no quantizing
       *         (issue #728) — "send output only on the specified time-boundary
       *         if appropriate".
       *
       *  Beats only: a grid is a musical grid, and ``bars.beats.units`` needs a
       *  meter no domain clock has. Inert until a clock is bound.
       */
      double Quantize() const;

      /** @brief Whether ``clock <name>`` has bound a domain clock for the
       *         tempo-relative times to be measured on (issue #728). False for
       *         a fresh object, after a bare ``clock``, and for a standalone
       *         object, which has no bridge to bind through. */
      bool OnClock() const {
        return binding.load(std::memory_order_relaxed) != 0;
      }

      /** @brief The clock name the object is bound to, or ``""``. The storage
       *         belongs to the patcher's bridge and never changes, so this is
       *         safe from any thread. */
      const char* ClockName() const;

      /** @brief Whether the object has let anything out yet. The first message
       *         always passes, there being no "previous output" to measure
       *         against. */
      bool HasOutput() const {
        return hasOutput.load(std::memory_order_relaxed);
      }

      /**
       *  @brief Milliseconds since the last message went out, on the patcher's
       *         block clock, or -1 before the first output and for a standalone
       *         object. Diagnostics / tests.
       */
      int SinceLastOutput() const;

      /**
       *  @brief Messages refused so far: dropped by ``.speedlim`` because they
       *         arrived too soon, dropped by either object because they landed
       *         inside the ``threshold`` window, or refused by ``.qlim`` because
       *         the scheduler was full, its queue was full, the text was
       *         over-long, or another thread held the slot.
       *
       *  Monotonic, readable from any thread, and the object's overflow report —
       *  a counter rather than a log line because the refusing thread may be the
       *  audio callback, and a log line there would allocate. Note that a
       *  ``.qlim`` value replaced by a newer one while waiting is *not* counted:
       *  that is usurp working as intended, not a refusal.
       */
      std::uint64_t Dropped() const {
        return dropped.load(std::memory_order_relaxed);
      }

      void BangIn(int inlet, YSE::THREAD thread);
      void IntIn(int value, int inlet, YSE::THREAD thread);
      void FloatIn(float value, int inlet, YSE::THREAD thread);
      void ListIn(const std::string& value, int inlet, YSE::THREAD thread);

    protected:
      // What a message is. There is no NONE: this names the kind of a message
      // that exists, either on its way through or held by `.qlim`.
      enum class Held : std::uint8_t {
        BANG,
        INT,
        FLOAT,
        LIST,
      };

      // What the window says about a message arriving now. DROP is the base's
      // own verdict — the `threshold` window, where neither object may hold
      // anything — and DEFER is the one the policy answers.
      enum class Verdict : std::uint8_t {
        PASS,
        DROP,
        DEFER,
      };

      // How long a message that arrived too soon has to wait, and on which
      // clock. `binding` 0 means the patcher's block clock and `millis`;
      // anything else is a clockBridge handle and `beats` on it.
      struct Wait {
        int millis = 0;
        double beats = 0.0;
        clockBridge::Handle binding = 0;
      };

      // Fills in the pieces of documentation that differ per policy; the base
      // constructor already set the category and built the ports. Called from
      // the subclass constructor before any PARAM_DOC of its own — parameter
      // docs are positional. RT-cold — constructor use only.
      void Document(const char* summary, const char* inletDoc, const char* outletDoc,
                    const char* paramDoc);

      // Send a message and open a fresh window from now. `text` is only read for
      // Held::LIST and may be null for every other kind.
      void Emit(Held kind, int intValue, float floatValue, const std::string* text,
                YSE::THREAD thread);

      // The policy: what happens to a message that arrived before the window
      // opened. `wait` is how much of the interval is left and on which clock,
      // which is what `.qlim` arms its wait with and `.speedlim` ignores.
      virtual void Blocked(Held kind, int intValue, float floatValue, const std::string* text,
                           const Wait& wait, YSE::THREAD thread) = 0;

      // A command word rather than data on the left inlet, matched against the
      // leading token spanning [begin, end). True when it was consumed. The
      // base knows `clock`, `threshold` and `quantize`; a subclass that
      // overrides this must call the base first.
      virtual bool Command(const std::string& value, std::size_t begin, std::size_t end);

      // How long until the window is open again, measured from an output that
      // just happened — what a message still queued behind it has to sit out.
      // All-zero means "no wait at all", which the scheduler's one-block floor
      // turns into the next block; a queue therefore never drains synchronously.
      Wait ReopenWait() const;

      // The patcher's block counter, or 0 for a standalone object.
      std::uint64_t NowBlock() const;

      // One refusal, on the counter `Dropped()` reports.
      void CountDrop() {
        dropped.fetch_add(1, std::memory_order_relaxed);
      }

    private:
      // Offer one arriving message to the window: through it when the interval
      // has elapsed, dropped when the `threshold` window says only one message
      // may pass, and to Blocked() otherwise.
      void Offer(Held kind, int intValue, float floatValue, const std::string* text,
                 YSE::THREAD thread);

      // Where the window stands for a message arriving now, and how long it
      // would have to wait. The whole of the timing rule, shared by both
      // objects and by ReopenWait().
      Verdict Test(Wait& wait) const;

      // The shared window arithmetic, given how long ago the last output was.
      // `onClock` says whether `beatNow` and `lastBeat` mean anything, which is
      // false whenever no clock is bound or its binding has not resolved.
      // `applyThreshold` is false for ReopenWait: the threshold gates arrivals,
      // and a message already waiting was accepted when it arrived.
      Verdict Evaluate(int elapsedMs, double elapsedBeats, bool onClock, double beatNow,
                       double lastBeatValue, clockBridge::Handle bound, bool applyThreshold,
                       Wait& wait) const;

      // How many beats `millis` lasts at the bound clock's current tempo, or 0
      // when the bridge cannot answer. Two acquire loads; no allocation.
      double BeatsForMillis(int millis, clockBridge::Handle bound) const;

      // Max's `clock <name>` / bare `clock`, through the patcher's bridge.
      // Binds wait-free on whichever thread the message arrived on; a name that
      // does not fit, a bridge that is full, or a standalone object all leave
      // the object where it was, silently, since this may be the audio thread.
      void SetClock(const char* name, std::size_t length);

      // Read one Max time value out of [begin, end of `value`) into the
      // millisecond / beat pair a time parameter is stored as. False when it is
      // not a time at all, which leaves both where they were.
      static bool ReadTime(const std::string& value, std::size_t begin, int& millis, double& beats);

      // Max's minimum time in milliseconds, and the creation argument. Read on
      // every arrival and written by the right inlet and by a live SetParams
      // re-parse, so atomic; unclamped, since Interval() is where the range is
      // applied.
      aInt interval;
      // The same interval in beats, or 0 for milliseconds (issue #728). Atomic
      // and unclamped for `interval`'s reasons.
      aFlt intervalbeats;
      // Max's `threshold`, in milliseconds and in beats (issue #728). 0 is off.
      aInt threshold;
      aFlt thresholdbeats;
      // Max's `quantize` grid in beats, 0 for none (issue #728).
      aFlt quantize;

      // The domain clock `clock <name>` bound, or 0 for the block clock (issue
      // #728). A patcher-owned binding handle rather than a name: the bridge
      // never releases one, so it stays valid for the life of the patcher and
      // costs nothing to carry. Atomic because a `clock` message and an arrival
      // are not on the same thread; relaxed, since neither publishes anything
      // through it. `.delay`'s arrangement, for `.delay`'s reason.
      std::atomic<clockBridge::Handle> binding{0};

      // The block the last output happened on, and whether there has been one.
      // Written before the send rather than after, so a message the send loops
      // back into this object measures against the window that is now open
      // rather than the one that just closed.
      std::atomic<std::uint64_t> lastBlock{0};
      // The same instant on the bound domain clock, or 0 when there was none.
      // What a beat window and a quantize grid are measured from.
      std::atomic<double> lastBeat{0.0};
      std::atomic<bool> hasOutput{false};
      std::atomic<std::uint64_t> dropped{0};
    };

    /**
     *  @brief Limit message throughput by dropping what arrives too soon —
     *         ``.speedlim`` (issues #508, #728).
     *
     *  The thinning half of the pair. A message that arrives before the interval
     *  has elapsed since the previous output is discarded and counted; nothing
     *  is stored, nothing is deferred, and the object never touches the
     *  patcher-wide scheduler budget. What comes out of a burst is its first
     *  message, at the moment it arrived — the "leading edge" throttle.
     *
     *  Read ``gRateLimitBase`` for the window, the clock, Max's ``threshold``
     *  and ``quantize``, and why this policy is attached to Max's ``speedlim``
     *  name rather than reproducing Max's own ``speedlim``/``qlim`` split, which
     *  is about scheduler priority and has no analogue in a headless patcher.
     *  ``usurp`` is not here for the same reason: it says what happens to a
     *  message that is waiting, and this object never has one.
     */
    class gSpeedlim : public gRateLimitBase {
    public:
      gSpeedlim();
      const char* Type() const override {
        return YSE::OBJ::G_SPEEDLIM;
      }
      CREATE(gSpeedlim)

    protected:
      // Drop it, and count it. No storage and no deferral: see the class notes.
      void Blocked(Held kind, int intValue, float floatValue, const std::string* text,
                   const Wait& wait, YSE::THREAD thread) override;
    };

    /**
     *  @brief Limit message throughput by holding what arrives too soon —
     *         ``.qlim`` (issues #508, #728).
     *
     *  The lossless half of the pair, and Max's own documented behaviour for
     *  both names. A message that arrives before the interval has elapsed is
     *  held, and goes out when the window opens. What happens to a *second* one
     *  arriving while the first waits is Max's ``usurp`` attribute, and it is
     *  the object's one real switch:
     *
     *  - **``usurp 1``**, the default: "the most recently received message
     *    replaces any currently queued message." One message waits however long
     *    the burst is, so what comes out of a burst is its last message, one
     *    interval after the previous output, and the value a gesture ended on is
     *    never the one thrown away.
     *  - **``usurp 0``** (issue #728): "when usurp is disabled, all messages
     *    received will be sent out." The object becomes a *queue* — every
     *    message eventually leaves, one per window, in the order it arrived —
     *    which is the right shape whenever the messages are events rather than
     *    samples of one value: notes to be spaced out, cues to be paced, a
     *    stream to be replayed at a rate something downstream can take.
     *
     *  ### The slots, and what a full queue does
     *
     *  ``CAPACITY`` messages may wait at once, each carrying up to
     *  ``TEXT_CAPACITY`` characters, all of it allocated with the object — so
     *  holding a message allocates nothing on whichever thread it arrived on,
     *  and that thread is routinely the audio callback. With ``usurp 1`` at most
     *  one slot is ever in use, because usurp says so; the rest exist for
     *  ``usurp 0``, and a ``usurp 0`` whose queue is full drops the new message
     *  and counts it rather than growing, spinning or discarding the history a
     *  patch is relying on arriving in order.
     *
     *  A slot's life is ``FREE -> CLAIMED -> ARMED -> FIRING -> FREE``, one
     *  atomic CAS per transition, which is ``.pipe``'s protocol with the
     *  generation left out. ``.pipe`` needs a generation because its ``clear``
     *  and ``flush`` can take a slot out from under a live scheduler message and
     *  re-arm it, so a stale tag could otherwise name a slot that has since been
     *  reused. Nothing here cancels: this object has no ``clear`` and no
     *  ``flush``, its scheduler messages carry no slot index at all — the
     *  release pops whichever slot is oldest when it runs — and ``Cancel`` is
     *  never called, so there is at most one scheduler message per object in
     *  existence and no tag can go stale. The CAS still earns its place: an
     *  arrival on the control thread and a delivery on the audio thread
     *  genuinely race for the same payload, and the CAS is what stops an usurp
     *  from rewriting a message that is already being sent.
     *
     *  **One scheduler message, not one per slot.** ``.pipe`` arms a deferral
     *  per value because its values have independent deadlines; here they do
     *  not — the queue drains at one message per window, so the only deadline
     *  that exists is the next one. The release re-arms itself for as long as
     *  the queue is non-empty, which is what keeps a hundred-message queue to a
     *  single slot of the patcher-wide budget.
     *
     *  A message that finds its slot CLAIMED or FIRING — another thread writing
     *  it, or a delivery mid-send — is dropped and counted rather than made to
     *  wait or spin. It is a narrow window and a bounded, honest loss, and
     *  spinning would be worse, this being a path the audio callback takes.
     *
     *  The scheduler refusing the arm (its patcher-wide 128 pending messages are
     *  spoken for) is the one case that costs more than the message in hand: the
     *  queue behind it would be left with no deadline to come out on, so it is
     *  dropped too and every lost message counted. Sending them early instead
     *  would break the object's one guarantee at exactly the moment the patch is
     *  at its resource limit, and doing it from inside a delivery would let an
     *  object wired back into its own inlet recurse on the audio thread.
     */
    class gQlim : public gRateLimitBase {
    public:
      gQlim();
      const char* Type() const override {
        return YSE::OBJ::G_QLIM;
      }
      CREATE(gQlim)

      /**
       *  @brief Messages that may wait at once — the ``usurp 0`` queue (issue
       *         #728). Half ``.pipe``'s, and it needs no more: the queue costs
       *         one slot of the patcher-wide scheduler however long it is, so
       *         the bound here is about the object's own memory rather than
       *         about sharing the budget.
       */
      static constexpr std::size_t CAPACITY = 32;

      /** @brief Max's ``usurp``, default 1: whether a new message replaces the
       *         one waiting (1) or joins a queue behind it (0). */
      bool Usurp() const;

      /** @brief Whether at least one message is waiting for the window to open
       *         right now. Diagnostics / tests. */
      bool IsHolding() const {
        return Waiting() > 0;
      }

      /** @brief How many messages are waiting. Never more than 1 with usurp on.
       *         Diagnostics / tests. */
      std::size_t Waiting() const;

      // The scheduler coming back when the window has opened.
      void DeliverDeferred(const deferredMessage& msg, YSE::THREAD thread) override;

    protected:
      // Hold it — replacing whatever was already waiting when usurp is on, and
      // queueing behind it when it is off. See the class notes.
      void Blocked(Held kind, int intValue, float floatValue, const std::string* text,
                   const Wait& wait, YSE::THREAD thread) override;
      bool Command(const std::string& value, std::size_t begin, std::size_t end) override;

    private:
      //   FREE --arrival--> CLAIMED --publish--> ARMED --release--> FIRING --> FREE
      //                        ^                   |
      //                        \-------usurp-------/
      static constexpr std::uint8_t STATE_FREE = 0;
      static constexpr std::uint8_t STATE_CLAIMED = 1;
      static constexpr std::uint8_t STATE_ARMED = 2;
      static constexpr std::uint8_t STATE_FIRING = 3;

      struct Slot {
        std::atomic<std::uint8_t> state{STATE_FREE};
        // Arrival order, which is what the release pops by. Read speculatively
        // by that scan — between seeing ARMED and winning the CAS the slot may
        // have been freed and reused — so atomic; a lost race is caught by the
        // CAS and the stale value discarded.
        std::atomic<std::uint64_t> seq{0};
        // Written only under CLAIMED and read only after the ARMED->FIRING CAS,
        // so never touched concurrently.
        Held held = Held::BANG;
        int intValue = 0;
        float floatValue = 0.f;
        // Reserved to TEXT_CAPACITY by the constructor, so holding text costs
        // no allocation.
        std::string text;
      };

      // Copy a payload into a slot the caller has CLAIMED.
      static void Store(Slot& slot, Held kind, int intValue, float floatValue,
                        const std::string* text);

      // Arm the one release this object ever has in flight, unless one already
      // is. Frees the whole queue and counts it when the scheduler refuses; see
      // the class notes.
      void EnsureArmed(const Wait& wait);
      // The raw arm. False when the patcher-wide pending set is full.
      bool Arm(const Wait& wait);
      // Free every waiting slot, counting each as a refusal. Bounded walk; no
      // allocation.
      void DropQueue();

      Slot slots[CAPACITY];
      // Arrival tickets; what "oldest waiting" is ordered by.
      std::atomic<std::uint64_t> nextSeq{1};
      // Whether a release is in flight. Exactly one may be, ever — this is what
      // makes cancellation unnecessary and a generation counter with it. Ordered
      // seq_cst against the slot publish so an arrival and a release that has
      // just emptied the queue cannot both decide the other will arm.
      std::atomic<std::uint8_t> armed{0};

      aInt usurp;
    };

  } // namespace PATCHER
} // namespace YSE
