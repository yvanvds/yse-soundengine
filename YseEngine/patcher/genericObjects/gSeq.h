#pragma once
#include "../io/fileScheduler.h"
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
     *  ### A resumed step passes its THREAD tag straight through
     *
     *  ``messageScheduler::DeliverDue`` tags a delivery ``T_GUI``, which is the
     *  right reading for an outlet send — the block's own traversal renders
     *  what the delivery caused. It used to be the wrong reading for a *remote*
     *  send, because ``patcherImplementation::PassData`` read the same tag as
     *  "the caller is the control thread" and answered it by taking ``mtx`` and
     *  building a log string, on the audio callback (**#690**). That is fixed
     *  at the patcher level: ``PassData`` asks ``CallingThread`` which thread it
     *  is really on. Every scheduler client, this one included, now simply
     *  forwards the tag it was delivered with.
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
     *  automatically when the patch is loaded" — and since #692 that is exactly
     *  what it does: ``SetParent`` asks for the file, and the sequence arrives
     *  with the patcher's next block.
     *
     *  Unlike ``.qlist`` and ``.mtr``, which both refused to *invent* one, this
     *  argument is Max's own and there is no second source for it to contradict.
     *  ``.qlist`` saves its cue list with the patcher and ``.mtr`` has ``embed``,
     *  so an object that also read a file when it joined a patcher would have two
     *  answers to what it holds, arriving in an order nothing controls. ``.seq``
     *  saves nothing at all, so the file is the only answer there is —
     *  ``.textfile``'s position exactly.
     *
     *  ### Reading and writing files (issue #692)
     *
     *  ``read`` and ``write`` shipped inert in #502 because a patcher message
     *  handler runs on whichever thread dispatched the message: in-patcher
     *  delivery dispatches on ``T_DSP``, ``THREAD`` is a *dispatch-semantics* tag
     *  rather than a thread identity, and opening a file there would block the
     *  audio callback. #683's ``fileScheduler`` is the shared answer and this is
     *  its fourth consumer, after ``.textfile`` (#687), ``.qlist`` (#689) and
     *  ``.mtr`` (#691), following the same recipe: ``SetParent`` calls
     *  ``EnableFileIO()``, the handler makes a wait-free slot claim, the write
     *  payload is built into a buffer reserved at construction, and
     *  ``DeliverFileResult`` parses the bytes in the dispatch frame the patcher
     *  hands out at the top of a later block.
     *
     *  **``read`` takes both of Max's formats.** A buffer beginning ``MThd`` is a
     *  standard MIDI file; anything else is Max's text form, "a start time in
     *  milliseconds (the time elapsed since the beginning of the sequence)
     *  followed by the (space-separated) bytes of a MIDI message recorded at that
     *  start time" — absolute times there, deltas here, so the reader
     *  differentiates them. A read **replaces** the tape and **stops the
     *  transport**, ``.mtr``'s rule for ``.mtr``'s reason: a sequence left playing
     *  would have a step already armed at a delta belonging to a tape that no
     *  longer exists.
     *
     *  **``write <name> [format]`` writes a standard MIDI file**, Max's own
     *  ("a non-zero format argument writes a multi-track, format 1 file"). Format
     *  0 is one ``MTrk`` holding everything; format 1 is a conductor track
     *  carrying the meta events and anything with no channel, then one track per
     *  MIDI channel the tape actually uses. Max's text form is **read but not
     *  written**, which is Max: its ``write`` produces MIDI files and the text
     *  form is an import path.
     *
     *  ### Why the MIDI parser is here rather than in ``YseEngine/midi/``
     *
     *  ``MIDI::fileImpl`` already parses standard MIDI files, and it cannot be
     *  used from here. Three reasons, each sufficient: it slurps the file through
     *  ``std::ifstream`` from a filesystem *path*, so it neither takes the bytes
     *  the scheduler already has nor honours the host's ``IO()`` layer; it builds
     *  ``std::vector``s of raw events and tempo entries and ``stable_sort``s them
     *  twice, and this completion runs on the audio thread at the top of
     *  ``Calculate``, where nothing may allocate; and its byte primitives
     *  (``readVarLen``, ``readU16``, ``channelDataBytes``) are anonymous-namespace
     *  statics in its ``.cpp``, so there is nothing to link against in any case.
     *  The reader here is therefore its own: a byte walk over the scheduler's
     *  buffer straight into this object's fixed tables, with no container in the
     *  middle. Lifting the shared primitives into a header both could use is
     *  filed separately.
     *
     *  The walk is bounded rather than merely finite: the header is validated
     *  before anything is cleared, at most ``MAX_FILE_TRACKS`` ``MTrk`` chunks are
     *  merged, and the merge stops as soon as the tape is full — so the cost is at
     *  worst ``MAX_EVENTS`` events times ``MAX_FILE_TRACKS`` comparisons, whatever
     *  the file claims about itself.
     *
     *  ### The meta outlet, and the three tempo attributes
     *
     *  Both were deferred from #502 for the same reason — they describe a
     *  sequence *read from a file*, and there was no file — and both land here.
     *
     *  Max's **meta** outlet is its rightmost and carries MIDI-file meta messages
     *  "prepended with the word ``meta``, followed by the name of the meta message
     *  and the data". It is **appended** as outlet 2, which is also Max's own
     *  position for it, so no saved patch's cords shift either way. Meta events
     *  are stored *in the tape* rather than in a timeline beside it, which is what
     *  makes them arrive at their place in the sequence for free: ``hook``,
     *  ``delay`` and ``addeventdelay`` move them with the music, tick mode plays
     *  them, and a ``write`` puts them back where they were.
     *
     *  The **tempo** attributes follow from the tape being milliseconds. A read
     *  applies the file's tempo map as it converts ticks to milliseconds, so
     *  playback is right by default. ``sequencetempo`` is then Max's "unmodified
     *  tempo of the sequence" — the first tempo the file declares — and ``tempo``
     *  "reflects the current tempo at the current playback time", which it does
     *  literally: a tempo meta passing the playhead sets it. ``overridetempo 1``
     *  makes ``tempo`` win, Max's "the value of the tempo attribute will override
     *  any tempo requested by the sequence".
     *
     *  One departure, and it is the tape's model rather than a shortcut: Max keeps
     *  the sequence in *ticks*, so ``overridetempo`` re-times a loaded sequence on
     *  the spot, while here the conversion happens once at read time and the flag
     *  therefore takes effect on the **next** read. Keeping ticks alongside
     *  milliseconds would give an object whose recorded half and file half measure
     *  time differently and whose ``hook`` could only edit one of them. Note also
     *  that ``tempo`` is not a playback-speed control for a *recorded* sequence —
     *  ``start <multiplier>`` and ``hook`` are, and both were already here.
     *
     *  ### ``dump`` and ``print`` stay consumed and inert
     *
     *  ``dump`` "opens a standard Open Document dialog box ... the selected file
     *  is opened as text in a new Untitled text window", which is the editing
     *  window this patcher is headless for; ``read`` already loads a file, and
     *  there is no text outlet for the raw bytes of one to go out of.
     *  ``print`` is inert for a neighbouring reason: Max prints the first sixteen
     *  events to its console, and the patcher's ``LogImpl().emit`` builds a
     *  ``std::string`` and takes a lock, which is exactly what a handler that may
     *  be the audio callback must not do. The tape is readable through ``Count()``
     *  and ``EventAt()`` instead, which is what the tests read it with.
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

    // ── the file half (issue #692) ────────────────────────────────────────

    /**
     *  @brief Most meta events a file may put on the tape — 256.
     *
     *  A meta occupies a tape slot like any byte does; this is the ceiling on
     *  how many of those slots may carry a *payload* rather than one byte.
     *  Generous for what real files hold (a name, a tempo map, a time
     *  signature, markers) and cheap: the payloads are reserved once at
     *  construction. A file with more keeps its timing and loses the extra
     *  metas rather than being refused.
     */
    static constexpr std::size_t META_CAPACITY = 256;

    /** @brief Longest meta payload kept, in bytes. One past it the meta is
     *         dropped whole rather than truncated — half a copyright notice is
     *         a different one — and the sequence loads on around it. */
    static constexpr std::size_t META_BYTES_CAPACITY = 128;

    /** @brief Longest message the meta outlet can send: the word ``meta``, the
     *         longest name Max has for one, and a payload of bytes each spelled
     *         out as up to three digits and a space. */
    static constexpr std::size_t META_TEXT_CAPACITY = 5 + 24 + (META_BYTES_CAPACITY * 4);

    /** @brief Most ``MTrk`` chunks a read merges. A file declaring more is
     *         refused whole rather than half read, and the bound is what keeps
     *         the merge's cost fixed on a completion the audio thread runs. */
    static constexpr std::size_t MAX_FILE_TRACKS = 32;

    /** @brief Max's default tempo, in beats per minute, and the tempo a MIDI
     *         file that declares none is read at — the Standard MIDI File
     *         specification's own default of 500000 microseconds per quarter
     *         note. */
    static constexpr float DEFAULT_TEMPO = 120.f;

    /** @brief Ticks per quarter note a ``write`` puts in its header. 480 is the
     *         common sequencer value and divides evenly by 2, 3, 4 and 5, so
     *         triplets and quintuplets land on whole ticks. */
    static constexpr int WRITE_DIVISION = 480;

    /** @brief Most chunks a format 1 ``write`` produces: the conductor track
     *         plus one per MIDI channel. */
    static constexpr std::size_t MAX_WRITE_TRACKS = 17;

    /**
     *  @brief Longest file a ``write`` can produce, and what ``fileScratch`` is
     *         reserved to at construction because the message asking for one
     *         may be on the audio thread.
     *
     *  Every term is a real bound: the 14-byte header, each chunk's 8-byte
     *  header and its 4-byte end-of-track event, at most four delta bytes and
     *  one data byte per tape event, and each meta's delta, ``FF <type>``,
     *  length and payload.
     */
    static constexpr std::size_t FILE_TEXT_CAPACITY =
        14 + (MAX_WRITE_TRACKS * (8 + 4 + 4)) + (MAX_EVENTS * 5) +
        (META_CAPACITY * (4 + 2 + 4 + META_BYTES_CAPACITY));

    static_assert(FILE_TEXT_CAPACITY <= fileScheduler::BYTES_CAPACITY,
                  "a .seq write must fit one file slot");

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
     *         automatically when the patch is loaded", which since #692 is what
     *         ``SetParent`` asks the scheduler for. */
    std::string Filename() const;

    /** @brief Max's ``tempo``, in beats per minute: "if the seq has read a MIDI
     *         file with tempo information, the tempo attribute will reflect the
     *         current tempo at the current playback time". */
    float Tempo() const;

    /** @brief Max's read-only ``sequencetempo``, "the unmodified tempo of the
     *         sequence" — the first tempo the file last read declared, or
     *         ``DEFAULT_TEMPO`` when it declared none. */
    float SequenceTempo() const;

    /** @brief Max's ``overridetempo``: whether ``Tempo()`` wins over the tempo
     *         a file asks for. Applies to the next read, not to the sequence
     *         already loaded — see the class documentation. */
    bool OverridesTempo() const;

    /** @brief How many events of the tape are meta events rather than bytes
     *         (issue #692). Diagnostics and tests. */
    std::size_t MetaCount() const;

    /** @brief The name the last ``read`` was given, which a bare ``read``
     *         reuses. Seeded from the creation argument. Control thread. */
    const std::string& ReadFile() const {
      return readPath;
    }

    /** @brief The same for ``write``. Control thread. */
    const std::string& WriteFile() const {
      return writePath;
    }

    // The scheduler coming back with the next byte of a running sequence.
    void DeliverDeferred(const deferredMessage& msg, YSE::THREAD thread) override;

    // Build the file plumbing while still on the control thread, so a `read`
    // arriving later on the audio thread finds the table already there
    // (issue #683) — and then ask for the file the creation argument named,
    // which is Max's "read into seq automatically when the patch is loaded".
    void SetParent(pObject* newParent) override;

    // A read or write this object asked for has finished. Called on the
    // patcher's dispatch thread inside a fresh messageEventScope; parses the
    // bytes into the fixed tape and bangs the file outlet. Allocation-free,
    // like every other path into this object.
    void DeliverFileResult(const fileResult& result, YSE::THREAD thread) override;

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

    /**
     *  @brief One tape event: the gap before it, and what it is.
     *
     *  Almost always a single raw MIDI byte, which is Max's whole storage
     *  model. A file read can also put a **meta** event here (issue #692), and
     *  putting it *in* the tape rather than on a timeline beside it is what
     *  makes it keep its place for free — ``hook``, ``delay`` and
     *  ``addeventdelay`` move it with the music, tick mode plays it, and a
     *  ``write`` puts it back where it was. The payload lives in ``metas``,
     *  because it is variable-length and a tape of 4096 fixed-size payloads
     *  would cost more than the sequence.
     */
    struct Event {
      int deltaMs = 0;
      unsigned char byte = 0;
      bool meta = false;
      std::uint16_t metaIndex = 0;
    };

    /** @brief One MIDI-file meta event's payload: its type byte, and its data
     *         exactly as the file held it, so a ``write`` puts back what a
     *         ``read`` took. */
    struct Meta {
      unsigned char type = 0;
      std::string raw;
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

    // ── files (issue #692) ────────────────────────────────────────────────

    /** @brief What a completion carries back, so the two halves can be told
     *         apart in ``DeliverFileResult``. Private: the tag means nothing to
     *         the scheduler. */
    static constexpr int FILE_TAG_READ = 0;
    static constexpr int FILE_TAG_WRITE = 1;

    /** @brief ``Serialize``'s track filter. ``EVERYTHING`` is format 0's single
     *         chunk; ``CONDUCTOR`` is format 1's first chunk, carrying the meta
     *         events and anything with no channel of its own; 0-15 is one MIDI
     *         channel's chunk. */
    static constexpr int TRACK_EVERYTHING = -2;
    static constexpr int TRACK_CONDUCTOR = -1;

    /**
     *  @brief One whole MIDI message on the tape, as ``NextMessage`` hands it
     *         over.
     *
     *  ``begin`` is the tape index the message starts at and ``absMs`` its
     *  absolute time, which is what a write needs in order to re-derive delta
     *  times separately for each chunk. ``dataBegin`` is where the *data* bytes
     *  start, which is not ``begin + 1`` for a message that arrived under
     *  running status: there the status byte is implied and has to be written
     *  out from ``status`` rather than copied from the tape.
     */
    struct Message {
      std::size_t begin = 0;
      std::size_t dataBegin = 0;
      std::size_t end = 0;
      int absMs = 0;
      int channel = -1; ///< 0-15, or -1 for a meta or a channel-less message.
      bool meta = false;
      std::size_t metaIndex = 0;
      unsigned char status = 0; ///< 0 for a meta event.
    };

    /** @brief Where a walk of the tape has got to: the next event, the absolute
     *         time of it, and the running status in force. */
    struct Walk {
      std::size_t at = 0;
      int absMs = 0;
      unsigned char running = 0;
    };

    // The next whole MIDI message (or meta event) at or after `walk`. False at
    // the end of the tape. Guard held; allocates nothing. Bytes a file has
    // nowhere to put — a real-time byte, a system-common one, an orphaned data
    // byte, a message the tape ends in the middle of — are skipped rather than
    // half written, while the clock still passes over them.
    bool NextMessage(Walk& walk, Message& out) const;

    // The read / write half of the inlet. `name` is the argument the message
    // carried, or empty for a bare `read` / `write`, which reuses the last name
    // given. `format` is Max's write argument, non-zero for a multi-track file.
    // False when there is nothing to do — no patcher, no name yet, a name that
    // does not fit, a sequence too big for one slot, or a full file table — in
    // every case silently, since this may be the audio thread.
    bool RequestFile(FILE_OP op, const char* name, std::size_t length, int format);

    // Format the tape into `fileScratch` as a standard MIDI file. Takes the
    // guard; allocates nothing, the scratch having been reserved to
    // FILE_TEXT_CAPACITY at construction. False when the guard was held
    // elsewhere or the file would not fit one slot.
    bool Serialize(int format);

    // One `MTrk` chunk into `fileScratch`, holding the events `filter` selects.
    // Guard held. The tempo map is followed on every chunk whether or not that
    // chunk emits the tempo metas, so every chunk shares one tick timeline.
    bool WriteChunk(int filter);

    // The other direction: replace the tape from the `length` bytes at `text`,
    // a standard MIDI file when it begins `MThd` and Max's text form otherwise.
    // Takes the guard; allocates nothing. False when the guard was held
    // elsewhere or the file could not be read at all, in which case nothing
    // changed.
    bool LoadFrom(const char* text, std::size_t length);

    // The two readers behind it. Guard held; both leave the tape empty rather
    // than half filled when they refuse.
    bool LoadMidiFile(const unsigned char* data, std::size_t length);
    void LoadText(const char* text, std::size_t length);

    // Wind back to an empty tape with no transport running, which is what a read
    // has to do to the sequence it replaces. Guard held.
    void ResetForLoad();

    // Append one event to the tape. False when the tape is full, which stops a
    // load rather than truncating a MIDI message half way. Guard held.
    bool PushByte(int deltaMs, unsigned char byte);
    bool PushMeta(int deltaMs, unsigned char type, const unsigned char* data, std::size_t length);

    // Build `sendText` as Max's "the word meta, followed by the name of the meta
    // message and the data" for `metas[index]`. Guard held, so the send that
    // follows can happen with the guard released.
    void FormatMeta(std::size_t index);

    // ── sending ───────────────────────────────────────────────────────────

    // Copy event `index` into what a send is made from — the byte, or a meta
    // message already formatted — and let a tempo meta reaching the playhead
    // update `tempo`, which is Max's "the tempo attribute will reflect the
    // current tempo at the current playback time". Guard held, so the send that
    // follows can happen with the guard released.
    void PrepareSend(std::size_t index);

    // Outlet 0 gets the byte, or outlet 2 the meta message; outlet 1 gets Max's
    // end bang *first* when this is the final event — "the bang is sent out
    // immediately before the final event of the sequence is played".
    void SendStep(bool last, YSE::THREAD thread);

    // Claimed with a single exchange by readers and writers alike; the loser
    // drops. Mutable so the const diagnostic accessors can take it.
    mutable std::atomic<bool> busy{false};

    // Max's creation argument, kept as text so ClearParams can put the object
    // back to its no-argument shape.
    std::vector<std::string> creationArgs;

    // The file the argument named — Max's "read into seq automatically when the
    // patch is loaded", which SetParent asks for (issue #692).
    std::string fileName;

    // The tape. Sized to MAX_EVENTS at construction and never resized; only the
    // first `count` are live.
    std::vector<Event> events;
    std::size_t count = 0;

    // The meta payloads a file read put on the tape, indexed by Event::metaIndex.
    // Sized to META_CAPACITY at construction with every payload reserved, so a
    // read allocates nothing; only the first `metaCount` are live.
    std::vector<Meta> metas;
    std::size_t metaCount = 0;

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

    // Max's three tempo attributes (issue #692). `sequenceTempo` is what the
    // file declared and `tempo` what is in force — the same thing until a tempo
    // meta passes the playhead or a patch sets one. `overrideTempo` decides
    // which the *next* read converts its ticks with; see the class docs on why
    // it is the next one rather than this one.
    float tempo = DEFAULT_TEMPO;
    float sequenceTempo = DEFAULT_TEMPO;
    bool overrideTempo = false;

    // The last name each half of the file surface was given — what a bare
    // `read` / `write` reuses, there being no dialog to ask with. Both are
    // seeded from the creation argument and reserved to the scheduler's path
    // bound at construction, so remembering a name on a message path is an
    // assign() into storage that exists rather than an allocation (issue #692).
    std::string readPath;
    std::string writePath;

    // Where a `write` is built before it is handed to the scheduler. Reserved to
    // FILE_TEXT_CAPACITY at construction for the same reason.
    std::string fileScratch;

    // What a send is made from: copied out under the guard and sent with it
    // released, so a patch that records into this object from downstream cannot
    // mutate the very byte still being fanned out. `sendMeta` says which of the
    // two the step took, and `sendText` carries a meta message already formatted
    // — built under the guard because the payload it reads is guarded state.
    unsigned char sendByte = 0;
    bool sendMeta = false;
    std::string sendText;
  };

} // namespace PATCHER
} // namespace YSE
