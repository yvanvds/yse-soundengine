#pragma once
#include "../pObject.h"
#include "../time/messageScheduler.h"
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief A sequencer of raw MIDI bytes — ``.seq`` (issue #502).
     *
     *  Max's ``seq``: "seq is a sequencer of raw MIDI bytes. You can control the
     *  speed of playback (only at the time you start it), read and write from
     *  files, and record from live MIDI input."
     *
     *  ### What it is next to ``.mtr`` and ``.qlist``
     *
     *  The third object in the family that has a clock, and the one whose
     *  contents are not messages at all. ``.qlist`` (#500) plays a *written*
     *  score, ``.mtr`` (#501) plays a *recorded* tape of arbitrary patcher
     *  messages, and this one plays a recorded tape of **bytes** — the wire
     *  format, one MIDI byte per event, exactly as ``seq`` stores it. Max's own
     *  "See Also" puts ``mtr`` next to it for that reason: same tape machine,
     *  different tape.
     *
     *  Storing bytes rather than messages is Max's design and it is worth
     *  stating why it is kept. A raw byte stream is what a MIDI port hands over
     *  and what one accepts; anything richer would have to decide what a running
     *  status byte, a system-exclusive dump or a fourteen-bit pitch bend *means*
     *  before it could store it, and a sequencer that cannot record what it does
     *  not understand is not a sequencer. Max's answer is ``midiparse`` /
     *  ``midiformat`` on either side, and the patcher's ``.noteon``,
     *  ``.noteoff``, ``.controlchange`` and ``.midiout`` already speak the same
     *  raw bytes.
     *
     *  ### Where the time comes from
     *
     *  The patcher's deferred-message scheduler (#628), through the same pair
     *  ``.mtr`` uses: ``messageScheduler::Now`` to *measure* a gap while
     *  recording and ``BlocksForMillis`` to *wait* one out while playing. One
     *  clock for both halves is not a convenience — a gap measured on one clock
     *  and waited out on another does not come back the length it went in — and
     *  it is the patcher's block counter, which stops when the engine does, so a
     *  paused patch holds a recording where it stands. A standalone object has
     *  no patcher and so no clock at all: everything records at delta 0 and
     *  plays straight through.
     *
     *  ### Zero-delta events run out in one dispatch, and that is the point
     *
     *  ``.qlist`` documents the scheduler's one-block deadline floor as a
     *  feature: a cue list of zero delays advances one entry per block rather
     *  than spinning. Reproducing that here would be a bug. A note-on is three
     *  bytes recorded at the same instant, and three bytes delivered one audio
     *  block apart are not that note-on — they are a status byte followed, some
     *  milliseconds later, by two data bytes. So a playback step emits its byte
     *  and then keeps going for as long as the *next* event's scaled delta is
     *  zero, arming a wait only for a positive one. The walk is still bounded —
     *  by ``MAX_EVENTS``, the tape itself — so a patch cannot lock the audio
     *  thread up with it, which is the property the one-block floor was
     *  protecting. Max, whose clock is milliseconds, gets this for free: every
     *  event stamped at the same millisecond fires in the same tick.
     *
     *  ### Playing it
     *
     *  ``start`` and ``bang`` both play from the beginning. ``start <n>`` is
     *  Max's tempo multiplier: "The message ``start 1024`` indicates normal
     *  tempo. If the number is ``512``, seq plays the sequence at half the
     *  original recorded speed, ``start 2048`` plays it back at twice the
     *  original speed." So it divides every wait, and — Max's parenthesis in the
     *  object description — the speed is settable "only at the time you start
     *  it".
     *
     *  ``stop`` ends recording or playing. Max: "A stop message need not be
     *  received when switching directly from playing to recording, or
     *  vice-versa", so ``record`` and ``start`` each end whatever the other was
     *  doing.
     *
     *  The end of the sequence bangs **outlet 1**, and Max is unusually precise
     *  about when: "Indicates that seq has finished playing the current
     *  sequence. (The bang is sent out immediately before the final event of the
     *  sequence is played.)" That ordering is reproduced literally. It is
     *  strange enough that only a test keeps it, and it is genuinely useful —
     *  a patch that has to do something *with* the last event knows it is the
     *  last one before it arrives rather than after.
     *
     *  ### ``start -1``: the sequencer driven by ``tick``
     *
     *  Max: "The message ``start -1`` starts the sequencer, but rather than
     *  follow Max's millisecond clock, seq waits for a ``tick`` message to
     *  advance its clock", and "in order to play the sequence at its original
     *  recorded tempo, seq must receive 48 ``tick`` messages per second."
     *
     *  This is ported, and it is the one part of Max's surface that answers
     *  issue #502's design gate directly. The issue asks for playback bound to a
     *  domain clock so tempo changes bend it the way ``YSE::clip`` does; there
     *  is no patcher-to-domain-clock bridge at all today (#688). But ``tick`` is
     *  Max's own answer to the same question — an external timing source the
     *  patch supplies, one tick being a MIDI-clock tick at 24 per quarter note —
     *  and it needs no bridge, no scheduler slot and no clock of its own. When
     *  #688 lands, a domain clock driving ``tick`` is the whole of the work.
     *
     *  Tick time is accumulated as a tick *count* rather than as milliseconds,
     *  so 48 ticks is exactly one second however many of them have gone by;
     *  adding 1000/48 ms per tick would drift by a millisecond every few
     *  seconds. The tempo multiplier does not apply in tick mode, because in
     *  tick mode the ticks *are* the tempo.
     *
     *  ### Editing the tape
     *
     *  ``delay <ms>`` sets the first event's onset — Max: "sets the onset time,
     *  in milliseconds, of the first event in the recorded sequence. All events
     *  in the sequence are shifted so that the first event occurs at the
     *  specified onset time." With deltas stored rather than absolute times,
     *  that shift *is* writing the first delta. ``addeventdelay <ms>`` is the
     *  same edit made relative, and ``hook <f>`` scales every delta: "multiplies
     *  all the event times in the stored sequence by that number ...
     *  Multiplications can even be performed while the sequence is playing", so
     *  a hook mid-playback reaches every event from the next step onward, the
     *  one already armed keeping the wait it was armed with.
     *
     *  ``record`` starts a fresh take and ``append`` does not — Max: "Starts
     *  recording at the end of the stored sequence, without erasing the existing
     *  sequence" — which is the whole difference between them.
     *
     *  ### Storage model
     *
     *  The family's: a fixed table allocated whole at construction plus
     *  ``.value``'s non-blocking ``busy`` guard, claimed with a single
     *  ``exchange`` by a loser that **drops** rather than spinning. Not a
     *  copy-on-write ``GraphState`` publish, which assumes the writer is the
     *  control thread — this object is written by whichever thread its message
     *  arrived on, and in-patcher delivery dispatches on ``T_DSP``.
     *
     *  ``.mtr``'s arrangement inside the guard is copied exactly: a playback
     *  step reads its event, advances the cursor and arms the next step all
     *  under the guard, then releases it and only then sends, so a step needs
     *  the guard once rather than twice and cannot lose it half way through.
     *
     *  ``Calculate()`` does nothing, for the reason it does nothing in every
     *  other store: the object is driven by its inlet and by its own clock, and
     *  one that emitted would restart itself on every DSP tick.
     *
     *  ### The one place a resumed step disagrees with its own THREAD tag
     *
     *  ``.qlist``'s, filed as **#690**: ``messageScheduler::DeliverDue`` tags a
     *  delivery ``T_GUI``, which is the right reading for an outlet send — the
     *  block's own traversal renders what the delivery caused — while
     *  ``patcherImplementation::PassData`` reads the same tag as "the caller is
     *  the control thread" and answers it by taking ``mtx`` and building a log
     *  string, on the audio callback. This object never sends remotely, so like
     *  ``.mtr`` it only ever needs the outlet reading and passes the delivered
     *  tag straight through; the note is here because the sidestep ``.qlist``
     *  documents is what the next scheduler client that *does* send remotely
     *  will need.
     *
     *  ### What persists: nothing
     *
     *  The family rule is *save iff Max gives the object a save flag*, and this
     *  object has none. ``qlist`` says "The qlist object saves its cue-list with
     *  the patcher" and ``mtr`` has Max 8's ``embed``; ``seq`` has neither, for
     *  the same reason ``text`` has neither — its contents live in a file, and
     *  ``read`` / ``write`` *are* its persistence. So ``.seq`` saves like
     *  ``.textfile``: nothing, and the serialised object is byte for byte what
     *  it was.
     *
     *  The **filename** creation argument is a parameter rather than state and
     *  does survive, through the ordinary ``DumpJSON`` / ``ParseJSON`` parameter
     *  round trip. Max: "Specifies the name of a file to be read into seq
     *  automatically when the patch is loaded." Nothing is read from it yet, but
     *  keeping it means a patch brought across from Max still names the file it
     *  meant, and starts loading it the day the file half lands.
     *
     *  ### ``read``, ``write``, ``dump`` and ``print`` are consumed, not
     *      performed
     *
     *  ``.textfile``'s answer (#499), ``.qlist``'s (#500) and ``.mtr``'s (#501),
     *  for their reason. A patcher object cannot find out that it is off the
     *  audio thread: a handler runs on whichever thread dispatched the message,
     *  in-patcher delivery dispatches on ``T_DSP``, and ``THREAD`` is a
     *  *dispatch-semantics* tag rather than a thread identity. Opening a file
     *  there would block the audio callback. Doing it properly needs a
     *  background job, an object lifetime that outlives it, the host's ``IO()``
     *  layer and a completion delivered back into a real dispatch frame — the
     *  shared plumbing of issue **#683**, with **#692** tracking this object's
     *  half.
     *
     *  ``print`` is inert for a neighbouring reason rather than the same one:
     *  Max prints the first sixteen events to its console, and the patcher's
     *  ``LogImpl().emit`` builds a ``std::string`` and takes a lock, which is
     *  exactly what a handler that may be the audio callback must not do. The
     *  tape is readable through ``Count()`` and ``EventAt()`` instead, which is
     *  what the tests read it with.
     *
     *  ### Deliberately not here
     *
     *  The ``tempo``, ``sequencetempo`` and ``overridetempo`` attributes. All
     *  three describe the tempo *of a MIDI file*: Max's own wording is "if the
     *  seq has read a MIDI file with tempo information, the tempo attribute will
     *  reflect the current tempo at the current playback time", ``sequencetempo``
     *  is "the unmodified tempo of the sequence", and ``overridetempo`` decides
     *  which of the two wins. With no file to read there is no sequence tempo to
     *  reflect or override, and an attribute whose value could only ever be its
     *  default is a promise the object cannot keep. They belong with the file
     *  half (#692); Max's ``start <multiplier>`` and ``hook`` are the speed
     *  controls that work on a recorded sequence, and both are here.
     *
     *  Max's **meta** outlet, its rightmost, likewise: meta events are a
     *  MIDI-*file* construct and cannot appear in a recorded byte stream at all,
     *  so the outlet would be permanently silent. ``.qlist`` left off its
     *  file-read outlet for the same reason and on the same terms — when the
     *  file half lands it is **appended** as outlet 2, which is also Max's
     *  position for it, so no saved patch's cords shift either way.
     *
     *  And the editing window and everything addressing it (``dump``'s file
     *  dialog, the double-click), the patcher being headless.
     */
    PATCHER_CLASS(gSeq, YSE::OBJ::G_SEQ)
    _NO_MESSAGES
    _NO_CALCULATE

    _BANG_IN(BangIn)
    _INT_IN(IntIn)
    _FLOAT_IN(FloatIn)
    _LIST_IN(ListIn)

    _PARM_CLEAR
    _PARM_PARSE

    /**
     *  @brief Most events the tape holds — 4096.
     *
     *  ``.table``'s bound rather than the 256 the text stores share, and the
     *  difference is what an event costs. A ``.coll`` entry or a ``.mtr`` event
     *  is a string of up to 128-256 characters; one here is a delta and a single
     *  byte. A MIDI note takes three of them, so 256 would be 85 notes — not a
     *  sequence — while 4096 is around 1300 messages for 32 KB, allocated whole
     *  at construction and never resized. An event past it is dropped, since
     *  growing the tape would allocate on whichever thread the message arrived
     *  on.
     */
    static constexpr std::size_t MAX_EVENTS = 4096;

    /** @brief Max's normal playback speed — "the message ``start 1024``
     *         indicates normal tempo". The multiplier divides every wait, so
     *         512 is half speed and 2048 is double. */
    static constexpr int NORMAL_SPEED = 1024;

    /** @brief Max's ``start -1``: play on ``tick`` messages instead of on the
     *         clock. */
    static constexpr int TICK_SPEED = -1;

    /** @brief Ticks per second at the original recorded tempo — Max: "seq must
     *         receive 48 tick messages per second", which is 24 per quarter
     *         note (the MIDI-clock standard) at 120 BPM. */
    static constexpr int TICKS_PER_SECOND = 48;

    /** @brief How many bytes the tape holds. */
    std::size_t Count() const;

    /** @brief Event @p index as ``"<delta> <byte>"``, or ``""``. Diagnostics
     *         and tests: it returns a copy, so control thread only. */
    std::string EventAt(std::size_t index) const;

    /** @brief Where the next playback step will read. Equal to ``Count()`` once
     *         the sequence has run out. */
    std::size_t Position() const;

    /** @brief Whether bytes arriving in the inlet are being recorded. */
    bool IsRecording() const;

    /** @brief Whether a ``start`` / ``bang`` is still running. */
    bool IsPlaying() const;

    /** @brief The multiplier the current (or last) ``start`` was given.
     *         ``NORMAL_SPEED`` as recorded, ``TICK_SPEED`` in tick mode. */
    int Speed() const;

    /** @brief Whether playback is waiting on ``tick`` rather than on the
     *         patcher's clock — Max's ``start -1``. */
    bool IsTickDriven() const;

    /** @brief The creation argument: Max's "name of a file to be read into seq
     *         automatically". Stored, and inert until #692. */
    std::string Filename() const;

    // The scheduler coming back with the next byte of a running sequence.
    void DeliverDeferred(const deferredMessage& msg, YSE::THREAD thread) override;

  private:
    /** @brief What one playback step did. */
    enum class Step {
      OUTPUT, ///< A byte was taken; send it. `last` says whether it is the final
              ///< one and `armed` whether a clock is behind the next.
      END, ///< Nothing left, or stopped from elsewhere.
      DROP, ///< The guard was lost; this walk does nothing further.
    };

    /**
     *  @brief Non-blocking exclusive access to the tape.
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

    /** @brief One recorded byte: the gap before it, and the byte. */
    struct Event {
      int deltaMs = 0;
      unsigned char byte = 0;
    };

    // ── the clock ─────────────────────────────────────────────────────────

    // The scheduler's block counter, or 0 when there is no patcher. RT-safe on
    // any thread.
    std::uint64_t NowBlock() const;

    // Arm the next step `waitMs` out — already scaled, and only ever called
    // with a positive one. Guard held; false when there is no scheduler (a
    // standalone object) or the pending set was full, in which case the caller
    // plays on without waiting.
    bool ArmStep(int waitMs);

    // The wait `deltaMs` becomes at the current multiplier, in milliseconds.
    // Guard held, and never called in tick mode, where the ticks are the tempo.
    int ScaledMillis(int deltaMs) const;

    // Drop the pending step, if there is one. Guard held.
    void CancelStep();

    // ── recording ─────────────────────────────────────────────────────────

    // Store one MIDI byte with the gap since the previous one. Does nothing
    // when the object is not recording, when it loses the guard, when the tape
    // is full, or when `value` is not a byte.
    void Record(int value);

    // ── playing back ──────────────────────────────────────────────────────

    // Take the event under the cursor into `sendByte`, advance the cursor and
    // arm the next step — all under the guard, so a step takes it once.
    // `last` and `armed` come back for the caller, which sends with the guard
    // released.
    Step TakeStep(bool& last, bool& armed);

    // Walk from wherever the cursor is: one byte per armed step, and straight
    // on through every event whose scaled delta is zero, so the bytes of one
    // MIDI message never straddle a block boundary. Re-entered from
    // DeliverDeferred each time a wait elapses.
    void Resume(YSE::THREAD thread);

    // One `tick`: advance the tick clock and emit every event that has come
    // due on it. Max's `start -1` mode.
    void Tick(YSE::THREAD thread);

    // Take the next event if the tick clock has reached it. Guard held by the
    // caller's step, as in TakeStep.
    Step TakeTickStep(bool& last);

    // ── commands ──────────────────────────────────────────────────────────

    // Max's `record` (a fresh take) and `append` (carry on at the end).
    void StartRecording(bool keepContents);

    // Max's `start` / `bang`. `speed` is the multiplier, TICK_SPEED for the
    // tick-driven mode.
    void StartPlaying(int speed, YSE::THREAD thread);

    // Max's `stop`, and what a `record` or a `start` does to the other mode.
    void StopAll();

    // Empty the tape and put the cursor back at the start.
    void Clear();

    // The command half of the inlet. False when `word` is none of them, which
    // on a command inlet means the message is simply not understood.
    bool HandleCommand(const char* word, std::size_t length, const std::string& message,
                       std::size_t argOffset, YSE::THREAD thread);

    // ── sending ───────────────────────────────────────────────────────────

    // Outlet 0 gets the byte; outlet 1 gets Max's end bang *first* when this is
    // the final event — "the bang is sent out immediately before the final
    // event of the sequence is played".
    void SendStep(bool last, YSE::THREAD thread);

    // Claimed with a single exchange by readers and writers alike; the loser
    // drops. Mutable so the const diagnostic accessors can take it.
    mutable std::atomic<bool> busy{false};

    // Max's creation argument, kept as text so ClearParams can put the object
    // back to its no-argument shape.
    std::vector<std::string> creationArgs;

    // The file the argument named. Stored and inert until #692.
    std::string fileName;

    // The tape. Sized to MAX_EVENTS at construction and never resized; only the
    // first `count` are live.
    std::vector<Event> events;
    std::size_t count = 0;

    // Where the next step reads. Run-time position, and this object saves
    // nothing anyway.
    std::size_t position = 0;

    bool recording = false;
    bool playing = false;

    // Max's tempo multiplier, or TICK_SPEED. Set by `start` and only by
    // `start` — Max: the speed is settable "only at the time you start it".
    int speed = NORMAL_SPEED;

    // The block the last event was recorded at — what the next delta is
    // measured from.
    std::uint64_t lastBlock = 0;

    // Tick mode. `ticks` counts `tick` messages since the `start -1`; elapsed
    // time is derived from it as `ticks * 1000 / TICKS_PER_SECOND` rather than
    // accumulated per tick, so 48 ticks is exactly one second however many have
    // gone by. `dueMs` is the absolute time of the event under the cursor,
    // measured from the start of playback, which is the running sum of the
    // deltas walked so far.
    std::int64_t ticks = 0;
    std::int64_t dueMs = 0;

    // The step this object is waiting on, or 0. One clock per object.
    messageScheduler::Handle pending = 0;

    // What a send is made from: copied out under the guard and sent with it
    // released, so a patch that records into this object from downstream cannot
    // mutate the very byte still being fanned out.
    unsigned char sendByte = 0;
  };

} // namespace PATCHER
} // namespace YSE
