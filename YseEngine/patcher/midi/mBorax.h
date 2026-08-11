#pragma once
// `.borax` (issue #543) — the note stream, measured.
//
// Not guarded on YSE_ENABLE_MIDI_DEVICE, for the reason `.poly`, `.flush`,
// `.sustain`, `.stripnote`, `.makenote` and `.midiflush` are not: this object
// opens no device and holds no port. It watches pitch/velocity pairs on ordinary
// cords and reports numbers about them, which is as useful over a generative
// patch driving `.noteon` on a platform with no MIDI hardware at all as it is
// over a keyboard.
#include "../pObject.h"

#include <atomic>
#include <cstdint>

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief `.borax` — report note-on and note-off statistics (issue #543),
     *         Max's `borax`.
     *
     *  ### What it is for
     *
     *  Max calls it a "swiss army knife for music analysis": pitch/velocity
     *  pairs go in and nine outlets report what the stream is *doing* — how many
     *  notes have been played, which voice each one took, how many are sounding
     *  right now, how long the note that just ended lasted, and how far apart
     *  successive attacks are.
     *
     *  It is the input side of adaptive musical behaviour, and it is the only
     *  object in the patcher that answers those questions. Everything else
     *  around a note either *makes* one (`.makenote`, `.noteon`), *routes* one
     *  (`.poly`), *filters* one (`.stripnote`) or *cleans up after* one
     *  (`.flush`, `.sustain`, `.midiflush`). A patch that wants to play denser
     *  when the performer plays faster, or hold a chord until the last key is
     *  released, or pick a reverb size from how long the phrase's notes are, has
     *  no way to ask before this. `.timer` (#506) measures *one* interval
     *  between two bangs; this measures every note in a polyphonic stream at
     *  once, each against its own attack.
     *
     *  ### It reports; it does not hold
     *
     *  That distinction decides most of what follows. The notes this object
     *  knows about are held by whatever is downstream of it — a synth, a
     *  `.poly` and the voice chains behind it, a MIDI rack — and this object is
     *  wired *alongside* them rather than in the path. So:
     *
     *  - **Nothing is passed through.** The pitch and velocity outlets carry the
     *    pair the object was given because Max's do and because a report is
     *    incomplete without them, but a patch normally takes its notes from the
     *    cord that also fed this object rather than from here.
     *  - **There is no `Teardown`** (issue #758). `.flush`, `.poly` and
     *    `.makenote` override it because each of them is the *only* thing that
     *    knows about a sounding note, so a patcher torn down mid-chord would
     *    strand notes nothing could ever release. This object knows about no
     *    note that something else does not also know about, and a burst of
     *    note-off *reports* at teardown would be a second, duplicate release for
     *    every patch that wired these outlets anywhere — with durations
     *    measuring how long the patcher took to be destroyed rather than how
     *    long anybody played. Silence is the honest report.
     *
     *  ### The nine outlets, and which of them fire when
     *
     *  Max's, in Max's order, and Max's selectivity — the outlets are not all
     *  refreshed on every message, and a patch reads that as "this is news":
     *
     *  | # | Outlet | Fires on |
     *  |---|--------|----------|
     *  | 0 | note serial | note-on, note-off |
     *  | 1 | voice | note-on, note-off |
     *  | 2 | polyphony | note-on, note-off |
     *  | 3 | pitch | note-on, note-off |
     *  | 4 | velocity | note-on, note-off |
     *  | 5 | note-off count | note-off |
     *  | 6 | duration (ms) | note-off |
     *  | 7 | delta count | note-on (not the first), `delta` |
     *  | 8 | delta (ms) | note-on (not the first), `delta` |
     *
     *  They fire **right to left**, Max's order, and here as everywhere in this
     *  family that is load-bearing rather than cosmetic: the note serial is the
     *  value a patch keys on — it is what pairs a note-off with its note-on —
     *  so it goes to a hot inlet and has to arrive last, or it would carry the
     *  previous note's figures with it.
     *
     *  ### The serial is the note's, not a running total
     *
     *  Max: "Each note-on received by borax is assigned a unique number, equal
     *  to the total count of note-ons received. That number is sent out when the
     *  note-on is received, and the **same number** is sent out when the note is
     *  turned off." So the leftmost outlet is not a counter that only ever
     *  climbs — on a note-off it goes *back* to the number that note was given
     *  on the way in. That is what makes an attack and its release identifiable
     *  as one event by something that saw both, which is the whole point of
     *  reporting a duration alongside it.
     *
     *  ### The milliseconds are measured, never counted
     *
     *  `std::chrono::steady_clock`, which is `.timer`'s (#506) and `.clocker`'s
     *  reading, `timerThread`'s scheduling clock, `INTERNAL::time`'s source and
     *  `MIDI::midiOutSender`'s dating clock. Issue #543 asks for exactly that —
     *  "duration measurement needs the same clock source as `.timer`" — and the
     *  reason is that a duration disagreeing with the `.timer` next to it would
     *  be a bug nothing in a patch could diagnose. A *wall* clock would be wrong
     *  twice over: not monotonic, so an NTP correction mid-note would stretch,
     *  shrink or reverse a duration.
     *
     *  It is deliberately **not** the deferred-message scheduler `.makenote`
     *  (#538) arms its releases on. That clock is the patcher's block counter,
     *  which is right for *scheduling* — a paused patch must not fire a burst of
     *  releases when it resumes — and wrong for *measuring*, its resolution
     *  being one audio block. A note is measured here at the instant its message
     *  really arrived, to the resolution of a `QueryPerformanceCounter`.
     *
     *  Both figures go out as floats rather than the ints Max sends. A patch
     *  asking how long a note was is asking a question whose answer is often
     *  under a millisecond apart from the next one — a grace note, the two
     *  halves of a rolled chord — and truncating to whole milliseconds would
     *  throw that away for nothing. `.timer` sends its interval as a float for
     *  the same reason.
     *
     *  ### Voices, and why a repeated pitch takes a second one
     *
     *  Max: "Each note is also assigned a unique voice number, equal to the
     *  lowest available number." A voice becomes available again when the note
     *  holding it is turned off. That is `.poly`'s (#542) free-voice rule, and
     *  the numbering here is `.poly`'s too — from 1, the way a patch sees them —
     *  rather than the 0-based numbering of the Pd clone, so a `.borax` watching
     *  a stream that a `.poly` also allocates does not report a different set of
     *  numbers for the same notes.
     *
     *  It does **not** steal and it has no overflow outlet: a full table
     *  ignores the note entirely rather than displacing one it is measuring.
     *  Stealing is an allocation policy — it exists so a *sounding* voice can be
     *  reused — and this object sounds nothing.
     *
     *  Two note-ons for the same pitch are two notes and take two voices, which
     *  is `.poly`'s answer and Max's default. Their note-offs pair oldest-first,
     *  so the first release reports the duration of the first attack, and a
     *  source that balances its note-ons and note-offs stays balanced.
     *
     *  ### What is ignored, and it is ignored silently
     *
     *  - A note-off for a pitch **no voice is holding**: there is no attack to
     *    measure it from, and reporting a duration from nothing would be worse
     *    than reporting none. The Pd clone posts a warning here; this object
     *    cannot, the thread being routinely the audio callback, where a formatted
     *    string is an allocation.
     *  - A note-on with **no free voice** — 128 already sounding. `MAX_VOICES`
     *    is the whole MIDI note range, so every distinct key on a keyboard can
     *    be down at once.
     *
     *  Neither emits anything at all, so a patch reads silence as "this message
     *  told me nothing", which is true.
     *
     *  ### `delta`, and the bang that resets
     *
     *  Max's two messages, and Max's meanings.
     *
     *  `delta` in the left inlet "causes the delta time (the time elapsed since
     *  the last note-on) and the delta count to be sent out" — a reading taken
     *  on demand rather than at an attack, which is how a patch asks "how long
     *  has it been quiet?". It counts as a delta report, so the delta count
     *  climbs. Before any note-on it emits nothing: there is no interval between
     *  one event and no event, which is `.timer`'s rule and for `.timer`'s
     *  reason.
     *
     *  A bang in the **right** inlet resets, Max's only bang and Max's wording:
     *  "resets borax by sending note-offs for all notes currently being held,
     *  erasing the borax object's memory of all notes received, and setting its
     *  counters and its clock to 0". Those note-offs are full note-off reports
     *  here — duration and note-off count included — and go out in ascending
     *  voice order before the counters are zeroed. Max does not say whether the
     *  durations come out; they do, because they are the one thing this object
     *  measured that nothing else can reconstruct, and dropping them would throw
     *  away data at the one moment a patch is most likely to be summing it up.
     *
     *  ### Real-time behaviour
     *
     *  A message handler runs on whichever thread dispatched the message, and
     *  in-patcher delivery dispatches on `T_DSP` — routinely the audio callback.
     *  So the voice table is `MAX_VOICES` entries allocated with the object, and
     *  nothing on the note-on path, the note-off path, the `delta` path or the
     *  reset path allocates, takes a lock or blocks. Every one of them is a
     *  bounded walk of that table plus one `steady_clock::now()`, which
     *  `INTERNAL::time::update` already reads from the callback every block
     *  (#667). Nothing here builds text: every outlet carries a number.
     *
     *  Two threads sending at once — or a patch that wires an outlet back into
     *  an inlet, which a reset would otherwise recurse through — are refused by
     *  a test-and-set guard whose loser is counted on `Dropped()` rather than
     *  made to spin. That is `.flush`'s, `.sustain`'s and `.poly`'s arrangement
     *  and it is here for the same reason: the table and the counters are
     *  ordinary members, and walking one while another thread rewrites it is not
     *  a thing that can be made to work by being careful. The velocity is atomic
     *  and sits outside the guard, so storing one never fails.
     *
     *  `Calculate()` does nothing: the object is driven entirely by its inlets,
     *  and one that emitted would report a note on every DSP tick that nobody
     *  played.
     */
    PATCHER_CLASS(mBorax, YSE::OBJ::M_BORAX)
    _NO_MESSAGES
    _NO_CALCULATE

    _INT_IN(IntIn)
    _FLOAT_IN(FloatIn)
    _LIST_IN(ListIn)
    _BANG_IN(Reset)

  public:
    /**
     *  @brief The most notes one `.borax` can measure at once, and the size of
     *         the table allocated with the object.
     *
     *  128, the Pd clone's `MAX_POLY` and the whole MIDI note range, so every
     *  distinct key on a keyboard can be down at once. A note arriving past it
     *  is ignored rather than displacing one being measured — see the class
     *  notes on why this object does not steal.
     */
    static constexpr int MAX_VOICES = 128;

    /**
     *  @brief The velocity the next pitch will be paired with — Max's middle
     *         inlet, 0 on a fresh object.
     *
     *  Unclamped and read exactly as it was given, which is `.flush`'s,
     *  `.poly`'s and `.stripnote`'s arrangement: the only value this object
     *  reads meaning into is 0, that being what a release is spelled with.
     *  Diagnostics and tests; a patch sees the same thing by sending a pitch.
     */
    int Velocity() const {
      return (int)velocity.load();
    }

    /** @brief Note-ons received since the last reset — the serial the next one
     *         will be given. Max's leftmost outlet. */
    std::uint64_t NoteCount() const {
      return noteCount;
    }

    /** @brief Notes completed since the last reset — Max's sixth outlet, and the
     *         count that accompanies each duration. */
    std::uint64_t NoteOffCount() const {
      return noteOffCount;
    }

    /** @brief Delta times reported since the last reset — Max's eighth outlet.
     *         Both a note-on after the first and the `delta` message add one. */
    std::uint64_t DeltaCount() const {
      return deltaCount;
    }

    /** @brief Notes sounding right now — Max's third outlet, and the figure the
     *         object exists to make a patch able to ask for. */
    int Poly() const {
      return poly;
    }

    /** @brief The pitch voice @p voice is measuring, or -1 when it is free or is
     *         not a voice at all. Numbered from 1, `.poly`'s numbering and the
     *         way a patch sees them. Diagnostics and tests. */
    int PitchOfVoice(int voice) const;

    /** @brief The last duration reported, in milliseconds, or 0 before any note
     *         has ended. Diagnostics and tests — a patch reads outlet 6. */
    double LastDurationMs() const {
      return lastDurationMs;
    }

    /** @brief The last delta time reported, in milliseconds, or 0 before any has
     *         been. Diagnostics and tests — a patch reads outlet 8. */
    double LastDeltaMs() const {
      return lastDeltaMs;
    }

    /** @brief Messages refused — a concurrent or re-entrant send. Monotonic,
     *         readable from any thread; diagnostics and tests only. */
    std::uint64_t Dropped() const {
      return dropped.load(std::memory_order_relaxed);
    }

  private:
    // One slot of the table. Plain members under the busy guard, never atomics:
    // a voice is only ever read and written by the one thread that got past
    // Enter().
    struct Voice {
      // What it is measuring. Any int at all — the table holds pitches rather
      // than being a bitmap over them, `.poly`'s arrangement.
      int pitch = 0;
      // The instant the attack arrived, on the engine's monotonic clock. What a
      // duration is measured from.
      std::int64_t onsetNs = 0;
      // The number this note was given on the way in, and the number its release
      // reports again. See the class notes.
      std::uint64_t serial = 0;
      // Allocation ticket, and what "the oldest of the notes on this pitch"
      // means. Never reused, so an older voice always compares lower.
      std::uint64_t age = 0;
      bool active = false;
    };

    // steady_clock now, in nanoseconds. `.timer`'s reading and issue #543's ask;
    // kept in nanoseconds so a millisecond figure is a rounding of a finer
    // reading rather than a difference of coarse ones.
    static std::int64_t NowNs();

    // A count on an int outlet. Unreachable in any real patch — it is 2^31
    // notes — but a cast that wrapped would be undefined behaviour rather than
    // a wrong number, so it saturates.
    static int AsOutlet(std::uint64_t count);

    // The pitch half, which is the half that acts: measure an attack, or close
    // one off.
    void Play(int pitch, YSE::THREAD thread);

    // A non-zero velocity: take the lowest free voice and start measuring.
    void NoteOn(int pitch, int noteVelocity, YSE::THREAD thread);

    // A velocity of 0: close off the voice holding this pitch — the oldest of
    // them if a patch played it twice — and report how long it lasted.
    void NoteOff(int pitch, YSE::THREAD thread);

    // The five outlets both events share, right to left. @p slot is the table
    // index; the voice number a patch sees is one more than it.
    void EmitNote(int slot, int noteVelocity, YSE::THREAD thread);

    // Outlets 8 and 7: the interval since the last attack, and the running count
    // of such readings. Nothing at all when there has been no attack to measure
    // from. @p now is the reading both this and the caller work from, so a
    // note-on's delta and its own onset are one instant rather than two.
    void EmitDelta(std::int64_t now, YSE::THREAD thread);

    // Max's right-inlet bang: report a note-off for everything still sounding,
    // then zero the counters and the clock.
    void ResetAll(YSE::THREAD thread);

    bool Enter();
    void Leave();

    // Max's middle inlet, read on every pitch and written from whichever thread
    // sent one — the audio callback included — so atomic. Outside the guard, so
    // storing a velocity never fails.
    aInt velocity{0};

    // The table. Fixed size and allocated with the object: this is written from
    // the audio thread, where a table that could grow has no place.
    Voice table[MAX_VOICES];

    // Everything below is written only by a thread holding the guard.
    std::uint64_t noteCount = 0;
    std::uint64_t noteOffCount = 0;
    std::uint64_t deltaCount = 0;
    int poly = 0;
    std::uint64_t nextAge = 1;

    // The instant of the last note-on, and whether there has been one. What a
    // delta is measured from; cleared by a reset, which is Max's "setting its
    // clock to 0".
    std::int64_t lastOnsetNs = 0;
    bool haveOnset = false;

    // The last figures that went out, for the accessors above.
    double lastDurationMs = 0.0;
    double lastDeltaMs = 0.0;

    std::atomic<bool> busy{false};
    std::atomic<std::uint64_t> dropped{0};
  };

} // namespace PATCHER
} // namespace YSE
