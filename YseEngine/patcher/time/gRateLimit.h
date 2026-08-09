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
     *  @brief Shared body for ``.speedlim`` and ``.qlim`` — limit the rate of
     *         message throughput (issue #508).
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
     *    *last* message of each burst, one window after the previous output.
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
     *  Max's ``usurp 0`` (queue every message rather than replacing the pending
     *  one), its ``quantize`` and ``threshold`` attributes and its
     *  tempo-relative time syntax are all out of scope here and recorded as
     *  issue #728.
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
     *  ### Real-time behaviour
     *
     *  ``Calculate()`` does nothing: both objects are driven by their inlets and
     *  (for ``.qlim``) by the scheduler, and one that emitted would send a
     *  message on every DSP tick from a stimulus no patch sent. No path
     *  allocates, locks or blocks — the window test is two atomic loads and an
     *  integer division, ``.speedlim``'s refusal is one atomic increment, and
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
       */
      int Interval() const;

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
       *         arrived too soon, or refused by ``.qlim`` because the scheduler
       *         was full, the text was over-long, or another thread held the
       *         slot.
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

      // Fills in the pieces of documentation that differ per policy; the base
      // constructor already set the category and built the ports. RT-cold —
      // constructor use only.
      void Document(const char* summary, const char* inletDoc, const char* outletDoc,
                    const char* paramDoc);

      // Send a message and open a fresh window from now. `text` is only read for
      // Held::LIST and may be null for every other kind.
      void Emit(Held kind, int intValue, float floatValue, const std::string* text,
                YSE::THREAD thread);

      // The policy: what happens to a message that arrived before the window
      // opened. `waitMs` is how much of the interval is left, which is what
      // `.qlim` arms its wait for and `.speedlim` ignores.
      virtual void Blocked(Held kind, int intValue, float floatValue, const std::string* text,
                           int waitMs, YSE::THREAD thread) = 0;

      // The patcher's block counter, or 0 for a standalone object.
      std::uint64_t NowBlock() const;

      // One refusal, on the counter `Dropped()` reports.
      void CountDrop() {
        dropped.fetch_add(1, std::memory_order_relaxed);
      }

    private:
      // Offer one arriving message to the window: through it if the interval has
      // elapsed, to Blocked() if it has not.
      void Offer(Held kind, int intValue, float floatValue, const std::string* text,
                 YSE::THREAD thread);

      // Max's minimum time in milliseconds, and the creation argument. Read on
      // every arrival and written by the right inlet and by a live SetParams
      // re-parse, so atomic; unclamped, since Interval() is where the range is
      // applied.
      aInt interval;

      // The block the last output happened on, and whether there has been one.
      // Written before the send rather than after, so a message the send loops
      // back into this object measures against the window that is now open
      // rather than the one that just closed.
      std::atomic<std::uint64_t> lastBlock{0};
      std::atomic<bool> hasOutput{false};
      std::atomic<std::uint64_t> dropped{0};
    };

    /**
     *  @brief Limit message throughput by dropping what arrives too soon —
     *         ``.speedlim`` (issue #508).
     *
     *  The thinning half of the pair. A message that arrives before the interval
     *  has elapsed since the previous output is discarded and counted; nothing
     *  is stored, nothing is deferred, and the object never touches the
     *  patcher-wide scheduler budget. What comes out of a burst is its first
     *  message, at the moment it arrived — the "leading edge" throttle.
     *
     *  Read ``gRateLimitBase`` for the window, the clock, and why this policy is
     *  attached to Max's ``speedlim`` name rather than reproducing Max's own
     *  ``speedlim``/``qlim`` split, which is about scheduler priority and has no
     *  analogue in a headless patcher.
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
      void Blocked(Held kind, int intValue, float floatValue, const std::string* text, int waitMs,
                   YSE::THREAD thread) override;
    };

    /**
     *  @brief Limit message throughput by holding the most recent value —
     *         ``.qlim`` (issue #508).
     *
     *  The lossless half of the pair, and Max's own documented behaviour for
     *  both names. A message that arrives before the interval has elapsed is
     *  held, and goes out when the window opens; a newer one arriving while it
     *  waits *replaces* it, which is Max's ``usurp`` attribute in its default
     *  state — "the most recently received message replaces any currently queued
     *  message". So what comes out of a burst is its last message, one interval
     *  after the previous output, and the value a gesture ended on is never the
     *  one thrown away.
     *
     *  ### One held message, and the slot that holds it
     *
     *  Exactly one, because usurp says so: this object is not ``.pipe``, which
     *  queues every value and needs 64 slots to do it. The single slot is
     *  pre-allocated with the object — a reserved string for text and two
     *  scalars — so holding a message allocates nothing on whichever thread it
     *  arrived on, and that thread is routinely the audio callback.
     *
     *  Its life is ``FREE -> CLAIMED -> ARMED -> FIRING -> FREE``, one atomic
     *  CAS per transition, which is ``.pipe``'s protocol with the generation
     *  left out. ``.pipe`` needs a generation because its ``clear`` and
     *  ``flush`` can take a slot out from under a live scheduler message and
     *  re-arm it, so a stale tag could otherwise name a slot that has since been
     *  reused. This object has neither command and never cancels: a slot only
     *  ever leaves ARMED by being delivered, so there is at most one scheduler
     *  message per slot in existence and a tag cannot go stale. The CAS still
     *  earns its place — an arrival on the control thread and a delivery on the
     *  audio thread genuinely race for the same payload, and the CAS is what
     *  stops an usurp from rewriting a message that is already being sent.
     *
     *  A message that finds the slot CLAIMED or FIRING — another thread writing
     *  it, or a delivery mid-send — is dropped and counted rather than made to
     *  wait or spin. It is a narrow window and a bounded, honest loss: the value
     *  would have replaced a pending one anyway, and a limiter's contract is at
     *  most one message per window, not every message eventually. Spinning would
     *  be worse, this being a path the audio callback takes.
     *
     *  The scheduler refusing the arm (its patcher-wide 128 pending messages are
     *  spoken for) drops and counts too, for ``.pipe``'s reason: sending the
     *  message early instead would break the object's one guarantee at exactly
     *  the moment the patch is at its resource limit.
     */
    class gQlim : public gRateLimitBase {
    public:
      gQlim();
      const char* Type() const override {
        return YSE::OBJ::G_QLIM;
      }
      CREATE(gQlim)

      /** @brief Whether a message is waiting for the window to open right now.
       *         Diagnostics / tests. */
      bool IsHolding() const {
        return slot.load(std::memory_order_acquire) == STATE_ARMED;
      }

      // The scheduler coming back when the window has opened.
      void DeliverDeferred(const deferredMessage& msg, YSE::THREAD thread) override;

    protected:
      // Hold it — replacing whatever was already waiting, which is Max's usurp —
      // and arm the rest of the interval. See the class notes.
      void Blocked(Held kind, int intValue, float floatValue, const std::string* text, int waitMs,
                   YSE::THREAD thread) override;

    private:
      //   FREE --arrival--> CLAIMED --publish--> ARMED --delivery--> FIRING --> FREE
      //                        ^                   |
      //                        \-------usurp-------/
      static constexpr std::uint8_t STATE_FREE = 0;
      static constexpr std::uint8_t STATE_CLAIMED = 1;
      static constexpr std::uint8_t STATE_ARMED = 2;
      static constexpr std::uint8_t STATE_FIRING = 3;

      // Written only under CLAIMED and read only after the ARMED->FIRING CAS, so
      // never touched concurrently.
      Held held = Held::BANG;
      int intValue = 0;
      float floatValue = 0.f;
      // Reserved to TEXT_CAPACITY by the constructor, so holding text costs no
      // allocation.
      std::string text;

      std::atomic<std::uint8_t> slot{STATE_FREE};
    };

  } // namespace PATCHER
} // namespace YSE
