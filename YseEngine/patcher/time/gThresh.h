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
     *  @brief Shared body for ``.thresh`` and ``.quickthresh`` — gather the
     *         values that arrive close together into one list (issue #509).
     *
     *  Both objects collect what comes in the left inlet into a group and send
     *  the group out as a list when the group is over. *When a group is over*
     *  is the whole difference between them, and it is the only thing the two
     *  subclasses below implement:
     *
     *  - ``.thresh`` closes a group on a **gap**. Max: "Collects items into a
     *    list if they appear within a certain specifiable amount of time. Each
     *    time an item arrives, the time is reset." So the group runs for as
     *    long as values keep coming and ends the moment the input goes quiet
     *    for the threshold. A continuous stream never closes one.
     *  - ``.quickthresh`` closes a group on a **deadline**. The window starts
     *    at the first value of the group and lasts the threshold however many
     *    values arrive inside it, so the list comes out at a predictable time
     *    — which is why Max calls it "fast chord detection" and "a faster,
     *    low-latency alternative to thresh". A late straggler is caught by
     *    Max's fudge/extension pair rather than by restarting the clock.
     *
     *  Grouping is what stands between a patch and the fact that "at the same
     *  time" is not a thing a stream of messages can express. Three notes of a
     *  chord struck together arrive as three separate ints milliseconds apart,
     *  and nothing downstream — ``.chord``, a voice allocator, a scale mapper —
     *  can treat them as a chord until something has put them back together.
     *  That is this pair's job, and it is the reason ``.quickthresh`` exists at
     *  all: for a chord you want the list *soon* and at a known moment, not
     *  whenever the player happens to stop playing.
     *
     *  ### Where the time comes from
     *
     *  The patcher's deferred-message scheduler (#628) and its block counter,
     *  which is what ``.pipe``, ``.qlim`` and ``.delay`` already use and for
     *  their reasons: ``TimerThread``'s ``Add`` takes a mutex and allocates a
     *  ``std::function`` — a value may arrive on the audio callback, where
     *  neither is allowed — and its callback fires *outside* any dispatch
     *  frame, so the emitted list would reach downstream objects as an
     *  unrelated stimulus rather than as one logical event caused by the
     *  values that went into it. The whole point of a grouping object is that
     *  the group is one event.
     *
     *  Measuring and waiting therefore happen on the same clock, which is the
     *  property that matters: a gap measured against ``steady_clock`` and a
     *  wait armed on the block counter would drift apart. That clock stops
     *  when the engine does, so a paused patch holds an open group where it
     *  stands, and its resolution is one audio block — two values arriving in
     *  the same dispatch are zero milliseconds apart however far apart they
     *  really were. A threshold shorter than a block therefore still groups
     *  everything that arrived in the same block, and a threshold of 0 does
     *  too: the scheduler's deadline floor is one block, so the smallest group
     *  either object can close is "everything in this dispatch".
     *
     *  ### The buffer, its capacity, and what a full one does
     *
     *  One group at a time, up to ``TEXT_CAPACITY`` characters of it, in a
     *  string reserved when the object is built — that is the bounded,
     *  pre-allocated accumulation buffer issue #509 asks for. Appending costs
     *  no allocation however many values arrive, which matters because the
     *  arriving thread is routinely the audio callback.
     *
     *  A value that does not fit is **dropped and counted** (``Dropped()``)
     *  rather than logged or allowed to grow the buffer, for the reason every
     *  other bounded structure in the patcher gives: a log line is a string
     *  format and an allocation on whichever thread the value arrived on. The
     *  group already collected is *not* discarded — an over-long group loses
     *  its tail, not its head, which is the failure that costs a patch least.
     *
     *  The patcher-wide scheduler is the other ceiling: ``CAPACITY`` pending
     *  messages shared with ``.pipe``, ``.delay``, ``.qlist``, ``.mtr`` and
     *  ``.seq``. A group that cannot arm its deadline at all is refused at
     *  birth and counted, rather than opened with no way to ever close — and
     *  rather than emitted immediately, which would let an object wired back
     *  into its own inlet recurse on the audio thread, exactly the trap
     *  ``.pipe``'s header sets out. A *re-arm* that fails is different: the
     *  group exists and holds real values, so it is sent early and counted,
     *  because a group cut short is a smaller loss than a group that never
     *  comes out.
     *
     *  ### The buffer's lifecycle, and one honest loss
     *
     *  ``EMPTY -> BUSY -> OPEN -> FIRING -> EMPTY``, one atomic CAS per
     *  transition — ``.qlim``'s protocol, and it is here for ``.qlim``'s
     *  reason: a value arriving on the control thread and a deadline firing on
     *  the audio thread genuinely race for the same buffer, and the CAS is what
     *  stops an append from rewriting a list that is already being sent. A
     *  value that finds the buffer BUSY (another thread appending) or FIRING (a
     *  send in progress) is dropped and counted rather than made to spin, this
     *  being a path the audio callback takes. FIRING lasts exactly as long as
     *  the send, so the loss is a value that a patch fed back into the object
     *  from its own outlet mid-emit — narrow, bounded, and cheaper than the
     *  alternative.
     *
     *  A generation counter rides in the scheduler tag and is bumped every time
     *  a group closes, so a deadline that comes due for a group already sent —
     *  by ``.quickthresh``'s bang, or by a cancel that lost its race — finds a
     *  tag that no longer names anything and does nothing. The scheduler's own
     *  ``Cancel`` is asked as well, best effort, purely to hand the
     *  patcher-wide budget back early.
     *
     *  ### Two departures from Max
     *
     *  - **A group of one number comes out as that number.** Max's list of one
     *    atom *is* an int or a float message, so his ``thresh`` sending a
     *    single collected number reaches an int inlet downstream. This patcher
     *    has no coercion at an inlet, so a one-token group is emitted as the
     *    int or float it spells — the same leading-token test ``.bondo``,
     *    ``.trigger``, ``.pipe`` and ``.qlim`` already use. Anything longer
     *    travels as list text, which is what a list is here.
     *  - **No tempo-relative time.** The threshold inlets speak milliseconds
     *    only. A note value (``4nd``) or tick count (``1440 ticks``) is
     *    *refused* rather than misread as the milliseconds it is not — the
     *    mistake ``.clocker`` made before #725, and the one ``.pipe`` and
     *    ``.qlim`` answer the same way. Neither object has a clock to measure a
     *    beat against.
     *
     *  ### Real-time behaviour
     *
     *  ``Calculate()`` does nothing: both objects are driven by their inlets
     *  and by the scheduler, and one that emitted would send a list on every
     *  DSP tick from a stimulus no patch sent. No path allocates, locks or
     *  blocks — an append is one CAS and a copy into reserved storage, a number
     *  is rendered with ``ExprFormatValue`` into a stack buffer, closing a group
     *  is one CAS and one send of the buffer by reference, and the deadline
     *  test is two atomic loads and an integer division.
     */
    class gThreshBase : public pObject {
    public:
      gThreshBase();

      void SetMessage(const std::string&, float) override {}
      void Calculate(YSE::THREAD) override {}

      /**
       *  @brief Longest group either object collects, in characters — the
       *         bounded, pre-allocated accumulation buffer, and the same bound
       *         the scheduler and the #225 value queue accept.
       *
       *  A value that would take the group past it is refused and counted; the
       *  values already collected are kept and still come out.
       */
      static constexpr std::size_t TEXT_CAPACITY = 256;

      /**
       *  @brief The threshold in milliseconds, as the next value to arrive will
       *         see it: the stored parameter with negatives and NaN clamped
       *         away.
       *
       *  Clamped on read rather than on write, which is ``.metro``'s,
       *  ``.pipe``'s and ``.qlim``'s arrangement and for their reason — a live
       *  ``SetParams`` re-parse stores straight into the field from the audio
       *  thread and notifies nobody, so a clamp applied at the inlet would not
       *  cover that route.
       */
      int Threshold() const;

      /** @brief Whether a group is open right now — values collected and a
       *         deadline pending. Diagnostics / tests. */
      bool IsCollecting() const {
        return state.load(std::memory_order_acquire) != STATE_EMPTY;
      }

      /** @brief Items collected into the open group so far, counted as the
       *         atoms they will be spelled with. Diagnostics / tests. */
      std::size_t Items() const {
        return items.load(std::memory_order_relaxed);
      }

      /**
       *  @brief Values refused so far, plus groups sent early because the
       *         scheduler could not hold their deadline.
       *
       *  The group was full, the patcher-wide scheduler was full, or another
       *  thread held the buffer. Monotonic, readable from any thread, and the
       *  object's overflow report — a counter rather than a log line because
       *  the refusing thread may be the audio callback.
       */
      std::uint64_t Dropped() const {
        return dropped.load(std::memory_order_relaxed);
      }

      void IntIn(int value, int inlet, YSE::THREAD thread);
      void FloatIn(float value, int inlet, YSE::THREAD thread);
      void ListIn(const std::string& value, int inlet, YSE::THREAD thread);

      // The scheduler coming back when a group's deadline has passed.
      void DeliverDeferred(const deferredMessage& msg, YSE::THREAD thread) override;

    protected:
      //   EMPTY --arrival--> BUSY --publish--> OPEN --close--> FIRING --> EMPTY
      //                       ^                  |
      //                       \-----append-------/
      static constexpr std::uint8_t STATE_EMPTY = 0;
      static constexpr std::uint8_t STATE_BUSY = 1;
      static constexpr std::uint8_t STATE_OPEN = 2;
      static constexpr std::uint8_t STATE_FIRING = 3;

      // The generation rides in the scheduler's `int` tag, so it has to stay a
      // positive int however many groups an object has closed.
      static constexpr std::uint32_t TAG_MASK = 0x3FFFFFFFu;

      // Fills in the pieces of documentation that differ per object; the base
      // constructor already built inlet 0, the outlet and the threshold
      // parameter. Called from the subclass constructor once its own inlets
      // exist, and before any PARAM_DOC of its own — parameter docs are
      // positional. RT-cold, constructor use only.
      void Document(const char* summary, const char* inletDoc, const char* thresholdInletDoc,
                    const char* outletDoc, const char* thresholdDefault,
                    const char* thresholdParamDoc);

      // Close the open group and send it. No-op when there is no open group,
      // which is what makes it safe to call from a bang and from a deadline
      // that raced one.
      void Flush(YSE::THREAD thread);

      // Arm this group's deadline `delayMs` from now. False when the
      // patcher-wide pending set refused it.
      bool Arm(int delayMs);

      // Milliseconds between `block` and now on the patcher's block clock, or 0
      // for a standalone object.
      int MillisSince(std::uint64_t block) const;

      /** @brief The block the most recent value of the open group arrived on. */
      std::uint64_t LastBlock() const {
        return lastBlock.load(std::memory_order_relaxed);
      }

      // One refusal, on the counter `Dropped()` reports.
      void CountDrop() {
        dropped.fetch_add(1, std::memory_order_relaxed);
      }

      // A clamped time parameter: negatives and NaN read as 0.
      static int ClampedMillis(const aInt& value);

      // The group's deadline has come due and the group is still open. Decide
      // whether it is over — this is the whole difference between the two
      // objects.
      virtual void OnDeadline(YSE::THREAD thread) = 0;

      // A fresh group is being opened, under BUSY. Somewhere for a subclass to
      // clear per-group state; the base has none.
      virtual void ResetGroup() {}

      // Route a time inlet to the right parameter. The base knows only its own
      // threshold on inlet 1.
      virtual void SetTime(int inlet, int millis);

      // A message word rather than data on the left inlet, matched against the
      // leading token spanning [begin, end). True when it was consumed. The
      // base has no command words.
      virtual bool Command(const std::string& value, std::size_t begin, std::size_t end,
                           YSE::THREAD thread);

      // Max's minimum time in milliseconds, and the first creation argument.
      // Read on every arrival and written by an inlet and by a live SetParams
      // re-parse, so atomic; unclamped, since Threshold() applies the range.
      aInt threshold;

    private:
      // Append one already-trimmed item to the open group, opening one if there
      // is none. Sends it straight out for a standalone object, which has no
      // clock and so no "close together".
      void Collect(const char* text, std::size_t length, YSE::THREAD thread);
      // A number as the text it spells, rendered into a stack buffer by
      // ExprFormatValue so nothing on the arrival path allocates.
      void CollectInt(int value, YSE::THREAD thread);
      void CollectFloat(float value, YSE::THREAD thread);

      // Send the collected group, as the number it spells when it is one
      // numeric token and as list text otherwise. Reads `buffer` by reference,
      // so it allocates nothing.
      void EmitBuffer(YSE::THREAD thread);
      // The same decision for a standalone object, which has no buffer to send
      // from. The one place a std::string is built — and by construction not an
      // audio-thread path, there being no patcher to render.
      void EmitDirect(const char* text, std::size_t length, YSE::THREAD thread);

      // Written only under BUSY and read only under FIRING, so never touched
      // concurrently. Reserved to TEXT_CAPACITY by the constructor, so
      // collecting costs no allocation.
      std::string buffer;

      std::atomic<std::uint8_t> state{STATE_EMPTY};
      // Bumped every time a group closes: the ABA guard that keeps a deadline
      // armed for a group already sent from acting on the next one.
      std::atomic<std::uint32_t> generation{0};
      // The scheduler message the open group is waiting on, so closing early
      // can hand the patcher-wide budget back. Best effort: a stale handle
      // costs a pending message that comes due and finds nothing to do, never a
      // wrong send.
      std::atomic<messageScheduler::Handle> handle{0};
      // The block the most recent value arrived on — what a gap is measured
      // from.
      std::atomic<std::uint64_t> lastBlock{0};
      std::atomic<std::size_t> items{0};
      std::atomic<std::uint64_t> dropped{0};
    };

    /**
     *  @brief Combine values received close together into a list, closing the
     *         group on a gap — ``.thresh`` (issue #509).
     *
     *  Max's ``thresh``, "combine numbers, symbols and lists when received
     *  close together": "Collects items into a list if they appear within a
     *  certain specifiable amount of time. Each time an item arrives, the time
     *  is reset."
     *
     *  The gap is the object. Every value restarts the clock, so a group grows
     *  for as long as the input keeps coming and is sent the moment the input
     *  has been quiet for the threshold — which makes this the right object for
     *  "wait until they have stopped, then tell me what they sent". A stream
     *  that never pauses never closes a group, and that is not a bug: it is
     *  what "received close together" means when everything is close together.
     *  Reach for ``.quickthresh`` when the list has to come out at a known
     *  moment instead.
     *
     *  Max's threshold default is 10 ms: "If no argument is present, the initial
     *  value is 10 milliseconds."
     *
     *  Read ``gThreshBase`` for the buffer, the clock, and the two departures
     *  from Max that both objects share.
     */
    class gThresh : public gThreshBase {
    public:
      gThresh();
      const char* Type() const override {
        return YSE::OBJ::G_THRESH;
      }
      CREATE(gThresh)

      /**
       *  @brief The threshold Max gives a ``thresh`` with no creation argument,
       *         in milliseconds — "if no argument is present, the initial value
       *         is 10 milliseconds".
       */
      static constexpr int DEFAULT_THRESHOLD = 10;

    protected:
      // The gap: over if nothing has arrived for the threshold, and measured
      // again from the last arrival if something has. See the class notes.
      void OnDeadline(YSE::THREAD thread) override;
    };

    /**
     *  @brief Fast chord detection — collect values into a list on a fixed
     *         window from the first one — ``.quickthresh`` (issue #509).
     *
     *  Max's ``quickthresh``, "fast chord detection": "Combines numbers when
     *  they are received close together. quickthresh is a faster, low-latency
     *  alternative to thresh that is optimized for chord detection."
     *
     *  The window is the object. It opens on the first value of a group and
     *  runs for the threshold whatever arrives inside it, so the list comes out
     *  at a moment a patch can predict — which is exactly what ``.thresh``
     *  cannot promise, its clock being restarted by every value. For a chord
     *  that is the whole difference: the notes are wanted *soon* and together,
     *  not whenever the player next lifts their hands.
     *
     *  ### Fudge and extension
     *
     *  Sloppy playing is the reason Max's ``quickthresh`` has three arguments
     *  rather than one. If a value lands in the last ``fudge`` milliseconds of
     *  the window — "if any notes are played within this amount of time at the
     *  end of the base thresh time, the threshold is extended" — the window is
     *  extended by ``extension`` milliseconds to catch the note that was nearly
     *  in time. Defaults are Max's: 40 ms, 10 ms, 20 ms.
     *
     *  The extension happens **at most once per group**, which is Max's reading
     *  of it — "an additional time frame added to the first argument, if
     *  necessary" — and the one that keeps the object's promise: a group lasts
     *  at most ``threshold + extension``, so a continuous stream cannot push
     *  the list out indefinitely the way it can with ``.thresh``. That bound is
     *  the reason to reach for this object.
     *
     *  Note the resolution the block clock imposes on the fudge test: the
     *  window's last ``fudge`` milliseconds are measured in whole audio blocks,
     *  so at the default 10 ms and a 128-sample block only the last few blocks
     *  count, and a fudge shorter than one block means "a value arriving in the
     *  same dispatch the deadline fell in".
     *
     *  ### bang, and set
     *
     *  Max's two extras. A bang "will reset quickthresh and output the notes in
     *  its buffer" — the group is closed and sent now, whatever its window had
     *  left, which is how a patch says "the chord is over, I know something you
     *  don't". ``set <threshold> <fudge> <extension>`` writes all three times at
     *  once; it is a command word rather than data, as it is in Max, so it
     *  cannot be collected as text.
     *
     *  Read ``gThreshBase`` for the buffer, the clock, and the two departures
     *  from Max that both objects share.
     */
    class gQuickthresh : public gThreshBase {
    public:
      gQuickthresh();
      const char* Type() const override {
        return YSE::OBJ::G_QUICKTHRESH;
      }
      CREATE(gQuickthresh)

      /** @brief Max's base threshold default: "The default value for the base
       *         threshold is 40 ms." */
      static constexpr int DEFAULT_THRESHOLD = 40;
      /** @brief Max's fudge default: "If not provided, the default value is
       *         10 ms." */
      static constexpr int DEFAULT_FUDGE = 10;
      /** @brief Max's extension default: "The default value is 20 ms." */
      static constexpr int DEFAULT_EXTENSION = 20;

      /** @brief The fudge zone at the end of the window, in milliseconds, with
       *         negatives and NaN clamped away. */
      int Fudge() const;
      /** @brief How much the window is extended when a value lands in the fudge
       *         zone, in milliseconds, clamped the same way. */
      int Extension() const;

      /** @brief Whether the open group has already used its one extension.
       *         Diagnostics / tests. */
      bool Extended() const {
        return extended.load(std::memory_order_relaxed);
      }

      void BangIn(int inlet, YSE::THREAD thread);

    protected:
      // The window: over unless a value landed in the fudge zone and this group
      // has not been extended yet. See the class notes.
      void OnDeadline(YSE::THREAD thread) override;
      void ResetGroup() override;
      void SetTime(int inlet, int millis) override;
      bool Command(const std::string& value, std::size_t begin, std::size_t end,
                   YSE::THREAD thread) override;

    private:
      aInt fudge;
      aInt extension;
      // One extension per group, so a group lasts at most threshold+extension.
      // Cleared when a group opens.
      std::atomic<bool> extended{false};
    };

  } // namespace PATCHER
} // namespace YSE
