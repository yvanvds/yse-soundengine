#pragma once
#include "../io/fileScheduler.h"
#include "../pListArgs.h"
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
     *  @brief A multi-track recorder for messages — ``.mtr`` (issue #501).
     *
     *  Max's ``mtr``, "a multi-track recorder for any kind of message": each
     *  track has an inlet, an outlet, and a tape. Send it ``record``, play with
     *  the patch, send it ``play``, and what you did comes back in the rhythm
     *  you did it in.
     *
     *  ### What it is next to ``.qlist``
     *
     *  ``.qlist`` (#500) is the sibling and the contrast is the whole design.
     *  A cue list is *written*: the timing is text an author types, one
     *  sequence, one cursor, and every stored line is prose. This one is
     *  *recorded*: the timing is measured off the clock as messages arrive, and
     *  there are N sequences running at once, each with its own cursor, its own
     *  speed and its own mute. ``.qlist`` is a score; ``.mtr`` is a tape
     *  machine — automation, which is what a parameter-driven patch eventually
     *  needs.
     *
     *  That is also why the two answer the "where does the time come from"
     *  question with the same mechanism but from opposite ends. Both use the
     *  patcher's deferred-message scheduler (#628); ``.qlist`` only ever *waits*
     *  on its clock, while this object also *reads* it, which is what
     *  ``messageScheduler::Now`` was added for. Using one clock for both halves
     *  is not a convenience: a gap measured on one clock and waited out on
     *  another is a gap that does not come back the length it went in.
     *
     *  ### Shape
     *
     *  Max: "The number of tracks determines the number of inlets and outlets
     *  in addition to the leftmost inlet and outlet." So N tracks gives N+1 of
     *  each. Inlet 0 takes commands for the whole object; inlet *n* is track
     *  *n*'s tape head, taking both the data it records and commands addressed
     *  to that one track. Outlet *n* is where track *n* plays back, and outlet
     *  0 reports — Max: the "track number, delta time, and absolute time of
     *  each message being output" as a three-item list, in answer to ``next``.
     *
     *  Max: "If there is no argument, there will be only one track", and "up to
     *  32 tracks are possible". The 32 is kept rather than Max 8's later 128,
     *  and it happens to be the number this implementation would have chosen
     *  anyway: a playing track holds one slot in the patcher's shared pending
     *  set of ``messageScheduler::CAPACITY`` (128), so a single 128-track
     *  object could take the whole table and leave nothing for the rest of the
     *  patch.
     *
     *  ### Recording, and why a delta is measured in blocks
     *
     *  ``record`` starts a track's tape at zero and each message that arrives
     *  in that track's inlet is stored with the gap since the previous one —
     *  Max: "numbers received in that track's inlet are combined with a delta
     *  time (the amount of time elapsed since the previous event) and stored".
     *  The first event's delta is measured from the ``record`` message itself,
     *  which is what gives ``delay`` something to overwrite.
     *
     *  The gap is read off the scheduler's block counter, so its resolution is
     *  one audio block. That is not a shortcut, it is the honest ceiling:
     *  playback waits on the same clock, whose deadline floor is also one
     *  block, so a delta recorded finer than a block could not be played back.
     *  The clock stops when the engine does, so a paused patch holds a
     *  recording where it stands — the same property the scheduler documents
     *  for a pending message, and the only meaning "50 ms ago" can have on a
     *  clock that is not running. A standalone object has no patcher and so no
     *  clock: everything records at delta 0 and plays back as fast as the walk
     *  can go.
     *
     *  A track is a fixed tape. ``MAX_EVENTS`` events of at most
     *  ``EVENT_CAPACITY`` characters, allocated whole at construction; a
     *  message past either is dropped rather than growing anything, because
     *  recording runs on whichever thread the message arrived on.
     *
     *  ### Playing back
     *
     *  ``play`` rewinds the track and arms the first event's delta; each
     *  delivery sends one event and arms the next one's. ``stop`` ends it,
     *  ``rewind`` moves the cursor without playing, and ``next`` is the manual
     *  mode — Max: "causes each track to output only the next message in its
     *  recorded sequence" — which also reports on outlet 0 and is the reason
     *  outlet 0 exists.
     *
     *  ``mute`` is Max's exactly: "causes mtr to stop producing output, while
     *  still continuing to 'play'", so a muted track keeps its clock and its
     *  cursor and only the send is skipped.
     *
     *  ``timescale`` is a **speed percentage** — Max: "100 is the original
     *  timescale, whereas 200 would be twice as fast" — so it divides every
     *  wait. Max's own footnote is reproduced along with it: "when a track is
     *  played again, the timescale is reset to 100", which is why the scale is
     *  sent *after* ``play`` rather than before it. A ``play`` in a track inlet
     *  may carry its own count and scale instead, Max's ``play 3 200``: three
     *  times through, at twice the speed.
     *
     *  ``first`` delays the start of playback without touching the tape;
     *  ``delay`` rewrites the first delta of every track, which is the same
     *  effect made permanent. Max documents both, and the difference between
     *  them is exactly that one is a setting and the other is an edit.
     *
     *  ### Commands in a track inlet
     *
     *  Max allows the transport words in a track's own inlet, addressing that
     *  one track, and that is reproduced. It does mean a track inlet reserves
     *  ``record``, ``play``, ``stop``, ``next``, ``rewind``, ``clear``,
     *  ``mute`` and ``unmute`` — and, since #691, ``read`` and ``write``, which
     *  Max registers on that inlet too: a message beginning with one of those
     *  words is a command and is never recorded as data. That is Max's behaviour rather
     *  than a limitation invented here — those are registered methods on the
     *  same inlet in Max too — but it is the reason the ``.prepend`` / ``.atoi``
     *  rule about not reserving words on a data inlet cannot be followed here.
     *  Anything else, including every number and every list, is data.
     *
     *  ### The one place a resumed step disagrees with its own THREAD tag
     *
     *  ``.qlist``'s, for ``.qlist``'s reason, and it is filed as **#690**. The
     *  scheduler delivers with ``T_GUI``, which is the right reading for an
     *  outlet send — the block's own traversal renders what the delivery
     *  caused — but ``patcherImplementation::PassData`` reads the same tag as
     *  "the caller is the control thread" and answers it by taking ``mtx`` and
     *  building a log string, on the audio callback. This object never sends
     *  remotely, so it only ever needs the outlet reading and passes the
     *  delivered tag straight through; the note is here because the next
     *  scheduler client that *does* send remotely will need the sidestep
     *  ``.qlist`` documents.
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
     *  The guard is never held across a send. A playback step reads its event
     *  into a buffer reserved at construction, advances the cursor and **arms
     *  the next step** all under the guard, then releases it and only then
     *  sends — arming first rather than after, so a step needs the guard once
     *  rather than twice and cannot lose it half way through. The scheduler's
     *  one-block deadline floor is what makes that safe: the step just armed
     *  cannot be delivered inside the dispatch that armed it.
     *
     *  ``Calculate()`` does nothing, for the reason it does nothing in every
     *  other store: the object is driven by its inlets and by its own clock,
     *  and one that emitted would restart itself on every DSP tick.
     *
     *  ### What persists
     *
     *  The family rule is *save iff Max gives the object a save flag*, and this
     *  object is the one place the reference changed its mind. Max 5: "the only
     *  way to save the contents of mtr is with the write message; the object's
     *  contents cannot be embedded in a patcher file." Max 8 added the flag —
     *  "when embed is set to 1, any recorded data is saved with the patcher" —
     *  so ``.mtr`` follows the flag, which is ``.funbuff``'s rule and reconciles
     *  both versions: nothing is written at all until ``embed 1``, and the flag
     *  itself is saved so a reloaded object still knows to embed.
     *
     *  Cursors, mutes, timescales and whether a track is playing or recording
     *  are run-time position, and are not saved for the reason ``.coll``'s
     *  pointer is not.
     *
     *  ### Tape files (issue #691)
     *
     *  ``write <file>`` saves the tapes and ``read <file>`` loads them back.
     *  Neither opens anything on the message path, for the reason this object
     *  shipped without them: a handler runs on whichever thread dispatched the
     *  message, in-patcher delivery dispatches on ``T_DSP``, and ``THREAD`` is a
     *  *dispatch-semantics* tag rather than a thread identity, so there is no
     *  predicate an object can ask to find out that it is not on the audio
     *  callback — where opening a file would block it. ``fileScheduler``
     *  (issue **#683**) is the shared answer, and this is its fourth consumer
     *  after ``.coll``, ``.textfile`` (#687) and ``.qlist`` (#689): the request
     *  is a wait-free claim on a patcher-owned slot, the disk work runs on the
     *  background pool honouring the host's ``IO()`` layer, and the bytes are
     *  parsed in the completion the patcher delivers at the top of a later
     *  block.
     *
     *  ### The format, and why only the text one
     *
     *  Max's own, quoted from the reference: "Line 1: ``track <track number>;``
     *  … Line 2, etc.: ``<delta time> <message>;`` … Last line: ``end;``". A
     *  ``write`` emits exactly that, one block per track, and a ``read`` parses
     *  it, breaking segments on a ``;`` **or** a newline the way ``.qlist``
     *  does — a hand-written file that left the semicolons off still loads as
     *  the lines it looks like, and a ``\r`` before the break is already
     *  whitespace to the tokenizer, so a CRLF file loads the same everywhere.
     *  A bang event has no text, and is written and read as the word ``bang``;
     *  the one thing that therefore cannot round-trip is a recorded one-word
     *  list that *is* the word bang, which is the same kind of limitation as
     *  ``.coll``'s comma.
     *
     *  Max reads ``.txt``, ``.pat`` and ``.json`` and has a ``writejson`` that
     *  saves "times as floats, preserving the timing of events precisely". Only
     *  the text form is here, and the reason is a hard one rather than a matter
     *  of effort: a completion is delivered **on the audio thread**, at the top
     *  of ``Calculate``, so parsing it must not allocate — and a JSON reader
     *  builds a document. A format that could be written and never read back
     *  would be worse than one that is not offered, so ``writejson`` is not
     *  invented either; ``embed`` already saves the tapes as JSON *into the
     *  patch*, on the control thread, which is where a JSON parse can afford to
     *  live. The float-precision argument for ``writejson`` also has nothing to
     *  buy here: a delta is measured in whole blocks (see above), so there is no
     *  sub-millisecond timing for a float to preserve.
     *
     *  ### One track, or all of them
     *
     *  Max's file surface is per-inlet — "In other inlets: opens a file
     *  containing only the track that corresponds to the inlet" — and that is
     *  reproduced. ``read`` / ``write`` in inlet 0 address every track;
     *  ``read`` / ``write`` in track *n*'s inlet address that one track, and a
     *  per-track read takes the **first** track block the file holds, whatever
     *  number that block declares, since Max defines the per-inlet form only for
     *  a file that holds one track. This means those two words join the eight
     *  transport words already reserved on a track inlet: a message beginning
     *  ``read`` or ``write`` in a track inlet is a command and is not recorded
     *  as data. Max registers them on that inlet too, so this follows Max rather
     *  than inventing a restriction.
     *
     *  A read **replaces** the tapes it addresses and stops their transport:
     *  recording ends, a playing track is stopped, its pending step cancelled,
     *  its cursor and its absolute clock wound back. That is a departure from
     *  ``.qlist``, whose walk carries on into the list it just loaded, and the
     *  difference is the clock: a track that kept playing would already have a
     *  step armed at a delta belonging to a tape that no longer exists, so its
     *  first gap after the read would be one no recording ever made.
     *
     *  A track block naming a track this object does not have is dropped, an
     *  event longer than ``EVENT_CAPACITY`` is skipped with the rest of the file
     *  still loading, events past ``MAX_EVENTS`` are dropped, and a file larger
     *  than ``fileScheduler::BYTES_CAPACITY`` or one that cannot be opened is
     *  refused whole. A ``write`` is refused the same way when the tapes do not
     *  fit one slot — 128 KiB holds three full tracks, and refusing whole rather
     *  than truncating is the family's rule, half a tape set being a different
     *  tape set. One *track* always fits, which the static_assert below pins.
     *
     *  ``read`` and ``write`` with no argument reuse the last name given, one
     *  name per half for the whole object rather than one per track: Max's bare
     *  forms open a file dialog, which a headless patcher has no equivalent of
     *  and which remembers nothing anyway, so there is no Max behaviour to
     *  match and the family's per-half arrangement is kept. Max documents no
     *  ``readagain`` / ``writeagain`` for ``mtr`` (``coll`` has them), so
     *  neither is invented.
     *
     *  ### No filename creation argument, and what ``embed`` has to do with it
     *
     *  ``.textfile`` reads a file when it is loaded because Max gives ``text`` a
     *  filename argument that "names a text file to be read in when the object
     *  is loaded". Max gives ``mtr`` no such argument — its one argument is the
     *  track count — so there is nothing to seed a load-time read with and
     *  ``SetParent`` only builds the plumbing.
     *
     *  It is worth saying why inventing one would be wrong here, because
     *  ``embed`` makes the case sharper than ``.qlist``'s rather than weaker.
     *  With ``embed`` off this object saves nothing, which is Max 5's world
     *  where ``write`` is the only persistence there is — and that is precisely
     *  the state in which a filename argument looks attractive. But ``embed`` is
     *  a *runtime* flag that a patch can turn on at any moment, and it is itself
     *  saved: an object that also read a file when it joined a patcher would, the
     *  first time a patch flipped it on and saved, have two answers to what is on
     *  its tapes — the patch and the file — arriving in an order neither the
     *  patch nor the object controls. ``.qlist`` could rule that collision out by
     *  reading its save flag; here it could not be ruled out at load time at all.
     *
     *  ### The file outlet, which Max has not got
     *
     *  A successful ``read`` bangs the **last** outlet, appended after every
     *  track outlet so no saved patch's cords shift. Max's ``mtr`` has no such
     *  outlet, where ``coll``, ``text`` and ``qlist`` all do, and the departure
     *  is documented rather than quietly made: Max's ``read`` is *synchronous*,
     *  so ``read x`` followed by ``play`` plays the file in Max. Here it cannot
     *  be — the read is a background job whose result lands a block or more
     *  later — so without a signal the arrival of a tape would be entirely
     *  unobservable to a patch, and the ``read``-then-``play`` idiom would have
     *  no correct spelling at all. It fires only on success, and only for a
     *  ``read``: Max has no outlet for a finished write anywhere in this family
     *  and neither does this.
     *
     *  ### Deliberately not here
     *
     *  Max 8's dictionary surface (``bang``, ``info``, ``dump``,
     *  ``dictionary``), the patcher having no dictionary type; its transport
     *  attributes (``sync``, ``transport``, ``quantize``, ``autostart``), there
     *  being no patcher-to-domain-clock bridge at all today (#688); the ``.pat``
     *  and ``.json`` file formats and ``writejson``, for the reason above; and
     *  the editing window and everything addressing it, the patcher being
     *  headless.
     */
    PATCHER_CLASS(gMtr, YSE::OBJ::G_MTR)
    _NO_MESSAGES
    _NO_CALCULATE

    _BANG_IN(BangIn)
    _INT_IN(IntIn)
    _FLOAT_IN(FloatIn)
    _LIST_IN(ListIn)

    _PARM_CLEAR
    _PARM_PARSE

    /**
     *  @brief Most tracks the object will build — 32.
     *
     *  Max's own ceiling, as the reference stated it for most of this object's
     *  life: "up to 32 tracks are possible". Max 8 later raised it to 128,
     *  which is not followed, because a playing track holds one slot in the
     *  patcher-wide pending set of ``messageScheduler::CAPACITY`` (also 128) —
     *  one object could take the whole table. A larger argument is clamped and
     *  the clamp is logged, on the control thread, before the object is wired.
     */
    static constexpr int MAX_TRACKS = 32;

    /** @brief Fewest tracks, and Max's no-argument shape: "if there is no
     *         argument, there will be only one track". */
    static constexpr int MIN_TRACKS = 1;
    static constexpr int DEFAULT_TRACKS = 1;

    /**
     *  @brief Most events one track's tape holds — 256.
     *
     *  The patcher's own bound, as in ``.coll``, ``.qlist`` and
     *  ``patcherImplementation::kValueListCap``. Allocated per track at
     *  construction and never resized, so the object costs what its argument
     *  asked for and nothing more; an event past it is dropped, since growing
     *  the tape would allocate on whichever thread the message arrived on.
     */
    static constexpr std::size_t MAX_EVENTS = 256;

    /** @brief Longest recorded message, in characters. A longer one is dropped
     *         whole rather than truncated — half a message is a different
     *         message. */
    static constexpr std::size_t EVENT_CAPACITY = 128;

    /** @brief Max's original timescale, and the value a ``play`` resets a track
     *         to: "100 is the original timescale, whereas 200 would be twice as
     *         fast". */
    static constexpr int DEFAULT_TIMESCALE = 100;

    /** @brief What one event costs a file, in characters: ``<delta> <message>``
     *         and the ``;\n`` that ends it (issue #691). */
    static constexpr std::size_t FILE_EVENT_CAPACITY =
        (std::size_t)FORMAT_INT_WIDTH + 1 + EVENT_CAPACITY + 2;

    /**
     *  @brief What one whole track costs a file — its ``track <n>;`` line, a
     *         full tape, and its ``end;`` line.
     *
     *  Pinned against the scheduler's slot below, so a ``write`` addressed to a
     *  single track can only ever fail on the disk rather than on its own
     *  bound. The whole object is a different matter: 32 of these do not fit
     *  one 128 KiB slot, so an object-wide ``write`` of very full tapes is
     *  refused whole rather than truncated.
     */
    static constexpr std::size_t FILE_TRACK_CAPACITY =
        6 + (std::size_t)FORMAT_INT_WIDTH + 2 + (MAX_EVENTS * FILE_EVENT_CAPACITY) + 5;

    /** @brief Longest text a ``write`` may produce, reserved once at
     *         construction because the message asking for one may be on the
     *         audio thread. */
    static constexpr std::size_t FILE_TEXT_CAPACITY = fileScheduler::BYTES_CAPACITY;

    static_assert(FILE_TEXT_CAPACITY <= fileScheduler::BYTES_CAPACITY,
                  "a .mtr write must fit one file slot");
    static_assert(FILE_TRACK_CAPACITY <= FILE_TEXT_CAPACITY,
                  "one full .mtr track must always fit one file slot");

    /** @brief How many tracks this object was built with — Max's creation
     *         argument, clamped to ``MIN_TRACKS``-``MAX_TRACKS``. */
    int TrackCount() const {
      return (int)tracks.size();
    }

    /** @brief How many events track @p track (0-based) has recorded. */
    std::size_t Count(int track) const;

    /** @brief Event @p index of track @p track as ``"<delta> <message>"``, or
     *         ``""``. Diagnostics and tests: it returns a copy, so control
     *         thread only. */
    std::string EventAt(int track, std::size_t index) const;

    /** @brief Where the next ``next`` or playback step will read on @p track. */
    std::size_t Position(int track) const;

    /** @brief Whether @p track is recording. */
    bool IsRecording(int track) const;

    /** @brief Whether @p track is playing back on the clock. */
    bool IsPlaying(int track) const;

    /** @brief Whether @p track is muted — playing, but not sending. */
    bool IsMuted(int track) const;

    /** @brief @p track's playback speed percentage; 100 is as recorded. */
    int Timescale(int track) const;

    /** @brief Max's ``first``: how long playback waits after a ``play`` before
     *         the first event's own delta starts counting. */
    int First() const;

    /** @brief Whether ``embed`` has been turned on, so the tapes are written
     *         into a saved patch. Off until a patch turns it on. */
    bool Embeds() const;

    /** @brief The name the last ``read`` was given, which a bare ``read``
     *         reuses — one name for the whole object rather than one per track.
     *         Empty until one names a file, there being no creation argument to
     *         seed it. Control thread. */
    const std::string& ReadFile() const {
      return readPath;
    }

    /** @brief The same for ``write``. Control thread. */
    const std::string& WriteFile() const {
      return writePath;
    }

    /** @brief The outlet a finished ``read`` bangs — the last one, appended
     *         after every track's (issue #691). */
    int FileOutlet() const {
      return (int)tracks.size() + 1;
    }

    // Build the file plumbing while still on the control thread, so a `read`
    // arriving later on the audio thread finds the table already there
    // (issue #683). Nothing is requested here, unlike `.textfile`: Max gives
    // `mtr` no filename argument, and one invented here would collide with
    // `embed` — see the class documentation.
    void SetParent(pObject* newParent) override;

    // A read or write this object asked for has finished. Called on the
    // patcher's dispatch thread inside a fresh messageEventScope; parses the
    // bytes into the tapes and bangs the file outlet. Allocation-free, like
    // every other path into this object.
    void DeliverFileResult(const fileResult& result, YSE::THREAD thread) override;

    // The tapes, into the object's "state" key of a DumpJSON — but only when
    // `embed` is on, which is Max 8's rule for this object and .funbuff's rule
    // in this patcher. Control thread — patcherImplementation::DumpJSON holds
    // mtx — but the guard is still taken, because a message may be arriving
    // from a rendering graph while the patch is being saved.
    void DumpState(nlohmann::json::value_type& json) override;

    // The other half: called from ParseJSON on the control thread, on a freshly
    // built object the audio thread cannot see yet.
    void RestoreState(const nlohmann::json::value_type& json) override;

    // The scheduler coming back with a track's next event.
    void DeliverDeferred(const deferredMessage& msg, YSE::THREAD thread) override;

  private:
    /** @brief The transport words, which mean the same thing in the control
     *         inlet (addressing every track, or the ones named) and in a track
     *         inlet (addressing that one). */
    enum class Cmd {
      RECORD,
      PLAY,
      STOP,
      NEXT,
      REWIND,
      CLEAR,
      MUTE,
      UNMUTE,
      NONE,
    };

    /** @brief What one playback step did. */
    enum class Step {
      OUTPUT, ///< An event was taken; send it, and `armed` says whether a clock
              ///< is behind the next one.
      END, ///< The track has stopped: nothing left, or stopped from elsewhere.
      DROP, ///< The guard was lost; this walk does nothing further.
    };

    /**
     *  @brief Non-blocking exclusive access to every tape at once.
     *
     *  ``Held()`` is false when another thread had it — the caller then does
     *  nothing at all. Never waits, never allocates. ``.coll``'s ``storeGuard``
     *  and ``.value``'s ``valueSlotGuard``, for the reason both give: this
     *  object is reachable from the control thread and from a rendering graph
     *  alike, a mutex is out on the second of those, and there is no single
     *  writer to build a seqlock around. One guard for all the tracks rather
     *  than one each, because the object-wide commands (``timescale``,
     *  ``delay``, a bare ``play``) touch every track and a per-track guard
     *  would only let them do so half way.
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

    /** @brief One recorded message: the gap before it, and what it was. */
    struct Event {
      int deltaMs = 0;
      // A bang has no text, and storing it as the word "bang" would come back
      // out as a one-word list rather than as a bang.
      bool bang = false;
      // Reserved to EVENT_CAPACITY + 1 at construction, so no store allocates.
      std::string text;
    };

    /** @brief One tape, and where its head is. */
    struct Track {
      // Sized to MAX_EVENTS at construction and never resized; only the first
      // `count` are live.
      std::vector<Event> events;
      std::size_t count = 0;
      // Where the next step reads. Run-time position, so it is not saved.
      std::size_t position = 0;
      bool recording = false;
      bool playing = false;
      bool muted = false;
      // Max's speed percentage, reset to 100 by every `play`.
      int timescale = DEFAULT_TIMESCALE;
      // Plays left, including the one in progress. Max's `play 3 200`.
      int iterations = 1;
      // The block the last event was recorded at — what the next delta is
      // measured from.
      std::uint64_t lastBlock = 0;
      // Absolute time of the last event output, the third item outlet 0
      // reports. Reset by `play` and `rewind`.
      int absMs = 0;
      // The step this track is waiting on, or 0. One clock per track, which is
      // what makes the tracks independent.
      messageScheduler::Handle pending = 0;
    };

    // Build the inlets, the outlets and the tapes from the creation argument.
    // Control thread, before the object is wired or published.
    void ShapePorts();

    // ── the clock ─────────────────────────────────────────────────────────

    // The scheduler's block counter, or 0 when there is no patcher. RT-safe on
    // any thread.
    std::uint64_t NowBlock() const;

    // Arm `track`'s next step `deltaMs` out, scaled by its timescale, plus
    // `extraMs` that is not — Max's `first`, which delays the start of playback
    // rather than being part of the recorded rhythm the timescale stretches.
    // Guard held; false when there is no scheduler (a standalone object) or the
    // pending set was full, in which case the caller plays on without waiting.
    bool ArmStep(std::size_t track, int deltaMs, int extraMs);

    // Drop `track`'s pending step, if it has one. Guard held.
    void CancelStep(Track& tr);

    // ── recording ─────────────────────────────────────────────────────────

    // Store one message on `track`, with the gap since the previous one. Does
    // nothing at all when the track is not recording, when it loses the guard,
    // or when the tape is full. `text` is ignored for a bang.
    void Record(std::size_t track, bool bang, const char* text, std::size_t length);

    // ── playing back ──────────────────────────────────────────────────────

    // Take the event under `track`'s cursor into `sendText` / `sendBang`,
    // advance the cursor and arm the next step — all under the guard, so a step
    // takes it once. `muted` and `armed` come back for the caller, which sends
    // with the guard released.
    Step TakeStep(std::size_t track, bool& muted, bool& armed);

    // Walk `track` from wherever its cursor is: one event per armed step, or
    // straight through when there is no clock to arm on. Re-entered from
    // DeliverDeferred each time a wait elapses.
    void Resume(std::size_t track, YSE::THREAD thread);

    // Max's manual mode: output one event and report it on outlet 0.
    void NextStep(std::size_t track, YSE::THREAD thread);

    // ── commands ──────────────────────────────────────────────────────────

    // Apply `cmd` to one track. `iterations` and `timescale` are Max's optional
    // `play` arguments and are ignored by every other command.
    void Apply(Cmd cmd, std::size_t track, int iterations, int timescale, YSE::THREAD thread);

    // Read the command word at the front of `message`, or Cmd::NONE.
    static Cmd ReadCommand(const char* word, std::size_t length);

    // Apply `cmd` to the tracks named after it, or to every track when none are
    // named — Max's "play, followed by one or more track numbers". Track
    // numbers are 1-based, as Max's are.
    void RunOnTracks(Cmd cmd, const std::string& message, std::size_t argOffset,
                     YSE::THREAD thread);

    // The object-wide words, which take a value rather than a track list.
    // False when `word` is none of them.
    bool HandleSetting(const char* word, std::size_t length, const std::string& message,
                       std::size_t argOffset);

    // ── files ─────────────────────────────────────────────────────────────

    /**
     *  @brief What a completion carries back, so the four shapes of request can
     *         be told apart in ``DeliverFileResult``.
     *
     *  Private to this object — the tag means nothing to the scheduler. The
     *  per-track tags carry the track in the tag itself, which is what lets one
     *  object have a read in flight for two tracks at once without their
     *  results being confusable.
     */
    static constexpr int FILE_TAG_READ_ALL = 0;
    static constexpr int FILE_TAG_WRITE_ALL = 1;
    static constexpr int FILE_TAG_READ_TRACK = 2;
    static constexpr int FILE_TAG_WRITE_TRACK = FILE_TAG_READ_TRACK + MAX_TRACKS;

    // The read / write half of an inlet. `track` is the one being addressed, or
    // ALL_TRACKS for the whole object, which is Max's per-inlet rule. `name` is
    // the argument the message carried, or empty for a bare `read` / `write`,
    // which reuses the last name given. False when there is nothing to do — no
    // patcher, no name yet, a name that does not fit, a tape set too big for one
    // slot, or a full file table — in every case silently, since this may be the
    // audio thread.
    static constexpr std::size_t ALL_TRACKS = (std::size_t)-1;
    bool RequestFile(FILE_OP op, std::size_t track, const char* name, std::size_t length);

    // Format `track`'s tape, or every tape, into `fileScratch` as Max's text
    // format. Takes the guard; allocates nothing, the scratch having been
    // reserved to FILE_TEXT_CAPACITY at construction. False when the guard was
    // held elsewhere or the text would not fit one slot.
    bool Serialize(std::size_t track);

    // The other direction: replace `track`'s tape, or every tape, from the
    // `length` bytes at `text`. Takes the guard; allocates nothing. False when
    // the guard was held elsewhere, in which case nothing changed.
    bool LoadFrom(std::size_t track, const char* text, std::size_t length);

    // One `<delta> <message>` line into `track`'s tape, appended after whatever
    // LoadFrom has already put there. Guard held.
    void LoadEvent(std::size_t track, const char* text, std::size_t length);

    // Wind `track` back to an empty tape with no transport running, which is
    // what a read has to do to the tapes it replaces. Guard held.
    void ResetForLoad(Track& tr);

    // ── sending ───────────────────────────────────────────────────────────

    // Send what TakeStep / NextStep left in the send buffers out `track`'s
    // outlet, in the kind it was recorded: a bang as a bang, and text as the
    // int, float or list it spells. `.route`'s rule, shared with `.coll`,
    // `.textfile` and `.qlist`.
    void SendEvent(std::size_t track, bool bang, YSE::THREAD thread);

    // Outlet 0's report — Max's "track number, delta time, and absolute time of
    // each message being output ... as a list". Built into a buffer reserved at
    // construction, so it allocates nothing.
    void SendReport(std::size_t track, int deltaMs, int absMs, YSE::THREAD thread);

    // Claimed with a single exchange by readers and writers alike; the loser
    // drops. Mutable so the const diagnostic accessors can take it.
    mutable std::atomic<bool> busy{false};

    // Max's creation argument, kept as text so ClearParams can put the object
    // back to its no-argument shape. `.bondo`'s arrangement, for `.bondo`'s
    // reason: the port count is a parameter, so both sides and the tape table
    // have to be rebuilt together.
    std::vector<std::string> creationArgs;

    // One per track, built by ShapePorts and never resized afterwards.
    std::vector<Track> tracks;

    // Max's `first`, in milliseconds: added once to the wait before a track's
    // first event. Object-wide, as Max's is.
    int firstMs = 0;

    // Max 8's embed flag: off until a patch turns it on, and the gate on
    // whether anything is written at all. Saved alongside the tapes, so a
    // reloaded object still knows to embed itself next time.
    bool embed = false;

    // What a send is made from, reserved at construction. Copies rather than
    // the event itself: the send path is synchronous, so handing an outlet the
    // stored string would let a patch that records into this object from
    // downstream mutate the very message still being fanned out.
    std::string sendText;
    bool sendBang = false;
    // Outlet 0's three-number report, built in place.
    std::string reportText;

    // The last name each half of the file surface was given — what a bare
    // `read` / `write` reuses, there being no dialog to ask with and no creation
    // argument to seed them. Reserved to the scheduler's path bound at
    // construction, so remembering a name on a message path is an assign() into
    // storage that exists rather than an allocation (issue #691).
    std::string readPath;
    std::string writePath;

    // Where a `write` is formatted before it is handed to the scheduler.
    // Reserved to FILE_TEXT_CAPACITY at construction for the same reason.
    std::string fileScratch;
  };

} // namespace PATCHER
} // namespace YSE
