#pragma once
#include "../pObject.h"
#include "../time/messageScheduler.h"
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief Shared ramp core for the control-rate line family — the
     *         breakpoint list, the interpolation, the output typing and the
     *         two outlets (issue #510).
     *
     *  A ramp is one small idea repeated: stand at a value, be given a target
     *  and a distance to travel it in, and emit the values in between until the
     *  target is reached. What differs between the objects that do this is only
     *  **what moves the cursor**:
     *
     *  - ``.line`` (below) steps on a clock — a value every *grain*
     *    milliseconds until the ramp time is up. That is Max's ``line``, and it
     *    is what issue #510 asks for.
     *  - ``.bline`` (issue #511) steps on a **bang**, so the patch supplies the
     *    timebase. Max: "bline is similar to the Max line object, except that
     *    it is driven by bang messages sent to its left inlet", and its
     *    breakpoint pairs count bangs where ``line``'s count milliseconds.
     *
     *  Everything else is identical and lives here, which is the whole reason
     *  this class exists: the breakpoint queue, the ``stop`` / ``set`` words,
     *  the "a target with no time arrives immediately" rule, the interpolation,
     *  the int-or-float output typing, and the bang on the right outlet when the
     *  last segment lands. A subclass supplies a cursor
     *  (``AdvanceCursor``), a step size for the typing rule (``StepSize``), and
     *  whatever inlets its own time unit needs.
     *
     *  ### What ``~line`` is not
     *
     *  ``~line`` (``D_LINE``) already ramps, and it is the wrong object for
     *  anything that is not a signal: it writes a DSP buffer, so its output can
     *  only be read by an audio inlet. This family ramps **messages**, which is
     *  what a patch needs to sweep a ``.metro`` interval, a MIDI velocity, a
     *  ``.scale`` bound or anything else that is a number rather than a
     *  waveform. The two are siblings by name and unrelated by wiring.
     *
     *  ### The breakpoint list
     *
     *  ``CAPACITY`` segments, pre-allocated with the object, each a target and a
     *  span in the subclass's own unit. A list of pairs is a breakpoint
     *  function: ``1 1000 0 1000`` climbs to 1 over a second and comes back down
     *  over the next, each segment starting from where the previous one landed
     *  so the ramp has no discontinuity. A new target arriving mid-ramp
     *  **replaces** the whole list — Max's "a subsequent list, float, or int in
     *  the left inlet clears all ramps yet to be generated" — and starts from
     *  the value the object currently stands at, again for continuity.
     *
     *  A list longer than the queue holds is truncated to ``CAPACITY`` segments
     *  and counted (``Dropped()``) rather than allocated for or logged, for the
     *  reason every bounded structure in the patcher gives: the arriving thread
     *  is routinely the audio callback, and a log line there is a string format
     *  and an allocation. ``MAX_POINTS`` numbers are read out of one list, which
     *  is exactly Max's ``maxpoints`` default of 129.
     *
     *  ### Int or float on the way out
     *
     *  Max's rule, and it is not decoration — a ``line`` that emitted floats
     *  into an int inlet, or ints into a ramp from 0 to 1, would be useless in
     *  opposite directions. The creation argument decides the object: "an
     *  argument may be used to set the initial value to be stored and the output
     *  type for the object — if the first argument is an int, the object outputs
     *  integer values, and a float will set it to output floating point values.
     *  If there is no argument, the initial value is 0 and the output type is
     *  int." The spelling of the argument is what is read, the same
     *  ``TokenLooksLikeFloat`` test ``.trigger``, ``.match`` and ``.thresh``
     *  already use — ``.line 0`` is an int object and ``.line 0.`` is a float
     *  one.
     *
     *  On top of that sits Max's ``floatoutput`` default of 2, *auto*: "outputs
     *  float values if distance is <= 1, line does not have a float argument,
     *  and step size is < 0.4". So an int object ramping across less than one
     *  unit in steps too small for an int to show emits floats for that segment
     *  rather than emitting the same integer over and over. The decision is
     *  taken once per segment, when the segment begins, and it is the honest
     *  reading of an attribute whose other two modes (0 off, 1 on) are not
     *  ported: there is no attribute surface on a patcher object here, and auto
     *  is the mode a Max patch gets unless it says otherwise.
     *
     *  An int output truncates toward zero, through the range-checked
     *  ``ExprToInt``; the endpoints are exact, because a completed segment emits
     *  its stored target rather than an interpolation of it.
     *
     *  ### Real-time behaviour
     *
     *  ``Calculate()`` does nothing: the family is driven by its inlets and by
     *  whatever moves its cursor, and one that emitted would send a value on
     *  every DSP tick from a stimulus no patch sent. No path allocates, locks or
     *  blocks — a list is read into a stack array by ``ExprParseFloatList``, a
     *  segment is four doubles copied into storage the object already owns, and
     *  a step is one multiply and one send.
     *
     *  The queue and the cursor are guarded by a **non-blocking** exclusive flag
     *  rather than a mutex, which is ``.metro``'s, ``.qlist``'s and ``.seq``'s
     *  arrangement and for their reason: this object is reachable from the
     *  control thread and from a rendering graph alike, a mutex is out on the
     *  second of those, and there is no single writer to build a seqlock
     *  around. A caller that loses the flag does nothing and counts a drop
     *  rather than spinning. Sends always happen with the flag *released*, so an
     *  outlet wired back into this object's own inlet finds the ramp
     *  consistent rather than half-written.
     */
    class gLineBase : public pObject {
    public:
      gLineBase();

      void SetMessage(const std::string&, float) override {}
      void Calculate(YSE::THREAD) override {}

      /**
       *  @brief Breakpoint segments one object may hold — the bounded,
       *         pre-allocated ramp list.
       *
       *  64 segments is Max's ``maxpoints`` default of 129 numbers read as
       *  target/span pairs. A longer list keeps its first 64 segments and counts
       *  the rest; Max's ``maxpoints`` attribute, which raises the ceiling, is
       *  not ported — there is no attribute surface here, and a growable list
       *  would mean allocating on the arriving thread.
       */
      static constexpr std::size_t CAPACITY = 64;

      /** @brief Numbers read out of one list message — Max's ``maxpoints``
       *         default of 129, which is ``CAPACITY`` pairs plus the odd
       *         trailing element his parser ignores. */
      static constexpr int MAX_POINTS = 129;

      /** @brief The value the object stands at — Max's "currently stored
       *         value", which is where the next ramp starts from. */
      double Value() const {
        return current.load(std::memory_order_relaxed);
      }

      /** @brief Whether a ramp is in progress (including a paused one).
       *         Diagnostics / tests. */
      bool IsRunning() const {
        return running.load(std::memory_order_relaxed);
      }

      /** @brief Whether a running ramp is held by ``pause``. Diagnostics /
       *         tests. */
      bool IsPaused() const {
        return paused.load(std::memory_order_relaxed);
      }

      /** @brief Segments still queued behind the one being travelled.
       *         Diagnostics / tests. */
      std::size_t Pending() const {
        return pending.load(std::memory_order_relaxed);
      }

      /**
       *  @brief Refusals so far: list segments past ``CAPACITY``, a message that
       *         found the ramp held by another thread, and a wakeup the
       *         patcher-wide scheduler could not hold.
       *
       *  Monotonic, readable from any thread, and the object's overflow report —
       *  a counter rather than a log line because the refusing thread may be the
       *  audio callback.
       */
      std::uint64_t Dropped() const {
        return dropped.load(std::memory_order_relaxed);
      }

      /** @brief Whether the creation argument made this a float object, which
       *         is what makes every segment emit floats. */
      bool FloatObject() const {
        return floatObject;
      }

      void IntIn(int value, int inlet, YSE::THREAD thread);
      void FloatIn(float value, int inlet, YSE::THREAD thread);
      void ListIn(const std::string& value, int inlet, YSE::THREAD thread);

    protected:
      /**
       *  @brief Non-blocking exclusive access to the ramp.
       *
       *  ``Held()`` is false when another thread had it — the caller then does
       *  nothing at all and counts a drop. Never waits, never allocates.
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

      /** One target and how far away it is, in the subclass's own unit. */
      struct Point {
        double target = 0.0;
        double span = 0.0;
      };

      // Fills in the documentation the base owns — the description, the value
      // inlet, both outlets and the `initial` parameter. Called from the
      // subclass constructor once its own inlets exist, and before any
      // PARAM_DOC of its own, parameter docs being positional. RT-cold,
      // constructor use only.
      void Document(const char* summary, const char* inletDoc, const char* valueOutDoc,
                    const char* initialParamDoc);

      // One refusal, on the counter `Dropped()` reports.
      void CountDrop() {
        dropped.fetch_add(1, std::memory_order_relaxed);
      }

      // Replace the whole ramp with `count` segments read from `pairs`
      // (target, span, target, span, ...) and start travelling. Takes the guard
      // itself; a segment with no span is arrived at immediately, before this
      // returns.
      void StartRamp(const float* pairs, std::size_t count, YSE::THREAD thread);

      // Move the cursor and emit, then keep completing zero-span segments until
      // one has real distance or the list runs out. Takes and releases the guard
      // once per emission, so no send ever happens with it held. Bounded by
      // CAPACITY + 1 iterations.
      void Run(YSE::THREAD thread);

      // Max's `pause` and `resume`. Both take the guard themselves.
      void Pause();
      void Resume(YSE::THREAD thread);

      // Stop where we stand and forget every queued segment — Max's `stop`, and
      // half of his `set`. Guard held.
      void Halt();

      // Begin the next queued segment, or return false when the list is spent.
      // Guard held.
      bool BeginSegment();

      // ── the seam a subclass fills in ────────────────────────────────────────

      // Move the cursor one step and report where the current segment now
      // stands, in [0, 1]. Guard held, and only ever called with `segSpan`
      // positive. `.line` reads the block clock and arms the next grain;
      // `.bline` (#511) will count the bang that got here.
      virtual double AdvanceCursor() {
        return 1.0;
      }

      // How far one step moves the output, for the auto-float typing rule.
      // Guard held; the current segment is already set up.
      virtual double StepSize() const {
        return 0.0;
      }

      // A fresh segment has been set up. Somewhere to rebase a clock and arm the
      // first step. Guard held, and free to collapse `segSpan` to 0 when it has
      // no way to travel the segment at all — which is what makes a standalone
      // object arrive at its target rather than hang on a clock that does not
      // exist.
      virtual void OnSegmentBegin() {}

      // The ramp is over, by arrival or by `stop`. Guard held.
      virtual void OnRampStop() {}

      // The ramp is being held. Guard held; `segSpan` should be reduced to
      // what is left of the segment, since a resume begins it afresh.
      virtual void OnPause() {}

      // Max's third list element, "the third number, which is optional, sets the
      // grain". True when the subclass has a grain to set — `.line` does,
      // `.bline` has no such thing and reads a three-element list as one pair
      // with a trailing element ignored.
      virtual bool TakeGrain(double) {
        return false;
      }

      // The span a bare target gets: Max's "if no time has been specified since
      // the last target value, the time is considered 0 and line immediately
      // outputs the target value". Consuming, hence not const.
      virtual double TakeSpan() {
        return 0.0;
      }

      // A number on a time inlet. The base has no time inlets of its own.
      virtual void SetTime(int, int) {}

      // A message word rather than data on the left inlet, matched against the
      // leading token spanning [begin, end). True when it was consumed. The base
      // knows Max's `stop` and `set`.
      virtual bool Command(const std::string& value, std::size_t begin, std::size_t end,
                           YSE::THREAD thread);

      // The segment being travelled. Written only under the guard, so plain
      // members; `segSpan` is the one an OnSegmentBegin override may rewrite.
      double segFrom = 0.0;
      double segTo = 0.0;
      double segSpan = 0.0;
      // Whether this segment emits floats — Max's output typing, decided once
      // when the segment begins. See the class notes.
      bool segFloat = false;

    private:
      // Read a list of numbers as Max reads it: one number is a bare target, two
      // are a target and a span, three are a target, a span and a grain, and
      // four or more are breakpoint pairs with an odd trailing element ignored.
      void TakeList(const float* numbers, int count, YSE::THREAD thread);

      // A bare target, travelled over whatever span TakeSpan() hands back.
      void Target(double value, YSE::THREAD thread);

      // One value out of the left outlet, spelled as this segment's type.
      void Send(double value, bool isFloat, YSE::THREAD thread);

      void ClearParams();
      void ParseParams();

      // The queue, and the cursor into it. Guard held.
      Point points[CAPACITY];
      std::size_t nextPoint = 0;
      std::size_t pointCount = 0;

      std::atomic<bool> busy{false};
      std::atomic<double> current{0.0};
      std::atomic<bool> running{false};
      std::atomic<bool> paused{false};
      std::atomic<std::size_t> pending{0};
      std::atomic<std::uint64_t> dropped{0};

      // Max's `initial` creation argument, kept as the text it was typed as:
      // its *spelling* is what decides the output type, and a float cannot
      // remember whether it was written "0" or "0.".
      std::string initialArg;
      // Read on every segment and written only by the parameter callbacks,
      // which run on the control thread before the object is published.
      bool floatObject = false;
    };

    /**
     *  @brief Generate a timed ramp of control values toward a target —
     *         ``.line`` (issue #510).
     *
     *  Max's ``line``, "generate timed ramp": "generate ramps and line segments
     *  from one value to another within a specified amount of time."
     *
     *  Send it a target and it walks there, emitting a number every *grain*
     *  milliseconds and banging the right outlet when it arrives. That is the
     *  control-rate half of a pair whose audio-rate half — ``~line`` — this
     *  patcher already had, and the half a patch actually needs to sweep
     *  anything that is not a signal: a ``.metro`` interval, a filter cutoff
     *  behind a ``.mtof``, a MIDI controller value, a mix weight. Without it the
     *  only way to move a control value over time is a ``.metro`` driving an
     *  accumulator, which is four objects and gets the endpoint wrong.
     *
     *  ### The three inlets, and the one-shot time
     *
     *  A number in the left inlet is a target; the middle inlet is how long to
     *  take getting there; the right inlet is the grain. The middle one is
     *  **consumed**, which is Max's rule and easy to trip over: "if no time has
     *  been specified since the last target value, the time is considered 0 and
     *  line immediately outputs the target value". So ``500`` into the middle
     *  inlet followed by two targets ramps to the first and jumps to the second.
     *  A patch that wants every target ramped sends the pair as a list —
     *  ``target time`` — which is the idiom Max patches use for exactly this
     *  reason. The grain, by contrast, persists: "once grains are set in a list,
     *  they will override the default until manually reset".
     *
     *  ### Where the time comes from
     *
     *  The patcher's deferred-message scheduler (#628) and its block counter,
     *  the same clock ``.pipe``, ``.qlim``, ``.thresh`` and ``.delay`` wait on
     *  — and *not* the ``TimerThread`` the issue's notes suggest. That is a
     *  deliberate departure and it is the same one every timing object filed
     *  since #628 has made, for two reasons that apply here with particular
     *  force. ``TimerThread::Add`` takes a mutex and allocates a
     *  ``std::function``, and a ramp arms a wakeup *per grain* — on whichever
     *  thread the previous step ran on, which for an object stepping inside the
     *  patcher's own dispatch is the audio callback. And a timer callback fires
     *  outside any dispatch frame, so every intermediate value of a ramp would
     *  reach downstream objects as an unrelated stimulus rather than as one
     *  logical event; a ``.line`` feeding a ``.bondo`` or a ``.next`` would
     *  behave differently from the same value sent by hand.
     *
     *  The clock's two honest consequences: it **stops when the engine does**,
     *  so a paused patch holds a ramp where it stands rather than finding it
     *  finished on resume; and its resolution is **one audio block**, so a grain
     *  shorter than a block emits one value per block, which is as fine as a
     *  control ramp can be here. A grain below 1 ms is Max's own error case —
     *  "the minimum grain allowed is 1 millisecond; any number less than 1 will
     *  be set to 20" — and is read as the default 20 wherever it is set.
     *
     *  ### Not drifting
     *
     *  The value emitted at each step is computed from the **elapsed time since
     *  the segment began**, never accumulated step by step, and the next wakeup
     *  is armed at the absolute next grain boundary rather than one grain from
     *  now. Both halves matter and they are ``.metro``'s and ``.tempo``'s: a
     *  deferred message is delivered at the first audio block at or past its
     *  deadline, so asking for a fixed grain every time would hand back that
     *  overshoot on every step and run the ramp long, and counting steps rather
     *  than reading the clock would make a late wakeup stretch the ramp instead
     *  of skipping a value. A segment therefore lands on its target at the time
     *  it was given, to within one block — Max notes that his own arrives "in
     *  just under the amount of time specified (time minus grain)", and the last
     *  wakeup here is clamped to the end of the segment so it lands on time
     *  instead.
     *
     *  Drift is reset per segment: the next segment of a breakpoint list is
     *  measured from the block the previous one was noticed finishing on, which
     *  costs at most one block per segment and keeps each segment's own duration
     *  exact.
     *
     *  ### stop, pause, resume, set
     *
     *  All four of Max's words, on the left inlet.
     *
     *  - ``stop`` freezes at the current value and forgets every queued segment
     *    — "stops line from sending out numbers, until a new target value is
     *    received" — which is ``~line``'s convention too, and the one issue #510
     *    asks for. No arrival bang: the ramp did not arrive.
     *  - ``pause`` holds the ramp with its queue intact and ``resume`` starts it
     *    moving again over what was left of the segment. Max says a paused line
     *    "will continue outputting whatever value was its current value"; that is
     *    read here as *standing at* it rather than as re-sending it every grain,
     *    a paused object that kept emitting being noise rather than information.
     *  - ``set <number>`` moves the stored value without emitting and stops a
     *    ramp in progress — "makes that number the new starting value from which
     *    to proceed to the next received target value. The set message also
     *    stops line if it is in the process of sending out numbers."
     *
     *  ### Three departures from Max
     *
     *  - **No ``clock`` message.** Max's ``line`` accepts ``clock <name>`` to
     *    run on a ``setclock``. This object speaks the patcher's millisecond
     *    clock only; a ``clock`` message is not a command word here and is read
     *    as list data, which — having no numbers in it — does nothing. The
     *    tempo-relative time syntax is refused the same way ``.pipe``, ``.qlim``
     *    and ``.thresh`` refuse it: a note value (``4nd``) or tick count
     *    (``1440 ticks``) on a time inlet leaves the time where it was rather
     *    than being misread as the milliseconds it is not, which is the mistake
     *    ``.clocker`` made before #725.
     *  - **No comma lists.** Max's ``0, 1 1000 0 1000`` is message-box comma
     *    syntax — three separate messages — and this patcher has no commas. The
     *    same ramp is ``set 0`` followed by the list ``1 1000 0 1000``.
     *  - **No ``floatoutput`` or ``maxpoints`` attributes.** There is no
     *    attribute surface on a patcher object; the defaults of both are the
     *    behaviour, and the reasoning is in ``gLineBase``.
     */
    class gLine : public gLineBase {
    public:
      gLine();
      const char* Type() const override {
        return YSE::OBJ::G_LINE;
      }
      CREATE(gLine)

      /**
       *  @brief The grain Max gives a ``line`` with no second argument, in
       *         milliseconds — "if the grain is not specified, line outputs a
       *         number every 20 milliseconds", and also the value a grain below
       *         Max's 1 ms minimum falls back to.
       */
      static constexpr int DEFAULT_GRAIN = 20;

      /** @brief The interval between emitted values in milliseconds, as the
       *         next segment will see it: the stored parameter with Max's
       *         minimum applied. */
      int Grain() const;

      /** @brief The ramp time waiting to be consumed by the next bare target,
       *         in milliseconds. Diagnostics / tests. */
      int RampTime() const {
        return ramptime.load(std::memory_order_relaxed);
      }

      // The scheduler coming back when a grain has elapsed.
      void DeliverDeferred(const deferredMessage& msg, YSE::THREAD thread) override;

    protected:
      double AdvanceCursor() override;
      double StepSize() const override;
      void OnSegmentBegin() override;
      void OnRampStop() override;
      void OnPause() override;
      bool TakeGrain(double value) override;
      double TakeSpan() override;
      void SetTime(int inlet, int millis) override;
      bool Command(const std::string& value, std::size_t begin, std::size_t end,
                   YSE::THREAD thread) override;

    private:
      // The generation rides in the scheduler's `int` tag, so it has to stay a
      // positive int however many segments an object has travelled.
      static constexpr std::uint32_t TAG_MASK = 0x3FFFFFFFu;

      // Arm the next step `delayMs` from now, dropping whatever was armed
      // before. False when the patcher-wide pending set refused it, which
      // collapses the segment onto its target. Guard held.
      bool Arm(int delayMs);

      // Retire the pending step, so nothing armed for the segment that is going
      // away can act on the one that replaces it. Guard held.
      void Cancel();

      // Milliseconds since the current segment began, on the block clock, or 0
      // for a standalone object. Guard held.
      int Elapsed() const;

      // Max's grain in milliseconds, and the second creation argument. Read on
      // every step and written by the right inlet, by a three-element list and
      // by a live SetParams re-parse, so atomic; unclamped, since Grain() is
      // where Max's minimum is applied.
      aInt grain;

      // Max's one-shot ramp time. Not a creation argument — Max's line has
      // none — so not a parameter either; written by the middle inlet from any
      // thread and taken away by the next bare target.
      std::atomic<int> ramptime{0};

      // The block the current segment began on: what elapsed time is measured
      // from, and so what keeps the ramp from drifting. Guard held.
      std::uint64_t segBlock = 0;

      std::atomic<messageScheduler::Handle> handle{0};
      // Bumped every time a segment ends or a ramp is retimed: the ABA guard
      // that keeps a step armed for a segment already gone from moving the one
      // that replaced it.
      std::atomic<std::uint32_t> generation{0};
    };

  } // namespace PATCHER
} // namespace YSE
