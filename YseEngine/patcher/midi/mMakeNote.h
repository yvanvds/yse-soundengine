#pragma once
// `.makenote` (issue #538) — the note-on that cannot forget its note-off.
//
// Not guarded on YSE_ENABLE_MIDI_DEVICE, for the reason `.midiflush`, the codec
// pair and the MPE family are not: this object opens no device and holds no
// port. It takes a pitch and emits a pitch/velocity pair now and the matching
// release later, which is as useful driving a patcher-built synth through
// `.noteon` on a platform with no MIDI hardware at all as it is driving a
// keyboard rack through `.midiout`.
#include "../pObject.h"
#include "../time/messageScheduler.h"

#include <atomic>
#include <cstddef>
#include <cstdint>

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief `.makenote` — a note-on and its scheduled note-off (issue #538),
     *         Max's `makenote`.
     *
     *  ### What it is for
     *
     *  Max: "Outputs a MIDI note-on message paired with a velocity value
     *  followed by a note-off message after a specified amount of time." A pitch
     *  in the left inlet comes straight back out with the stored velocity, and
     *  the same pitch comes out again with velocity 0 a duration later.
     *
     *  This is the object that makes a patch able to *play a note* at all.
     *  Everything upstream of MIDI output here — `.noteon`, `.midiformat`, the
     *  `.x*out` family — is a stateless formatter: it sends the message it is
     *  handed and remembers nothing. A patch that plays a note therefore has to
     *  send its own release, and forgetting one is the single most common way to
     *  strand a synth. `.midiflush` (#537) is the cure for that after the fact;
     *  this object is the prevention, and the two answer different halves of the
     *  same problem. A `.makenote` never *needs* flushing, because the release
     *  was scheduled at the same instant as the attack.
     *
     *  ### Where the time comes from
     *
     *  The patcher's deferred-message scheduler (#628), which is `.pipe`'s and
     *  `.delay`'s clock and not the `TimerThread` behind `.metro`. The reasons
     *  those headers give apply here word for word and one of them is sharper
     *  for this object than for either of them:
     *
     *  - `TimerThread::Add` takes a mutex and allocates a `std::function`, and a
     *    pitch routinely arrives on the audio callback — an in-patcher dispatch
     *    runs on `T_DSP`. Neither is allowed there.
     *  - A timer callback fires *outside* any dispatch frame, so the release
     *    would reach the objects downstream as an unrelated stimulus. A note-off
     *    is *caused* by the note-on it releases; that causal chain is the whole
     *    object.
     *
     *  The clock is the patcher's block counter, so it stops when the engine
     *  does: a paused patch holds its pending releases where they stand rather
     *  than firing them in a burst when it resumes. Its resolution is one audio
     *  block, and a duration of 0 therefore releases on the *next* block rather
     *  than inside the arming dispatch — the same floor `.pipe 0` reads as
     *  semantics, and the thing that keeps a `.makenote` wired back into its own
     *  inlet a fast trill rather than a stack overflow.
     *
     *  ### The pending set, its bound, and what a full one does
     *
     *  ``CAPACITY`` releases may be in flight per object — the maximum number of
     *  simultaneously sounding notes one `.makenote` can hold — all of it
     *  allocated with the object. Beyond it sits the patcher's own ceiling:
     *  `messageScheduler::CAPACITY` pending messages shared with every `.pipe`,
     *  `.delay`, `.qlist`, `.mtr` and `.seq` in the patch, so a `.makenote` can
     *  be refused by the patcher-wide budget while its own slots are free.
     *
     *  The two refusals are answered differently, and the difference is the
     *  object's one real design decision:
     *
     *  - **Its own set is full.** The *whole note* is refused — no note-on
     *    either — and counted on `Dropped()`. Emitting an attack this object has
     *    already proved it cannot release would manufacture exactly the hanging
     *    note it exists to prevent, at precisely the moment the patch is at its
     *    resource limit. `clipTransport`'s sounding-note table answers the same
     *    question the same way: "table full — drop rather than allocate", before
     *    it sends the note-on rather than after.
     *  - **The patcher-wide budget is full.** This is only discovered *after*
     *    the attack has gone out, because a slot has to be published before its
     *    message can be armed. So the release is sent immediately instead and
     *    the note is counted: a note shorter than it was asked to be, rather
     *    than one that never ends.
     *
     *  Neither is logged. The refusing thread may be the audio callback, where a
     *  string format is an allocation; `Dropped()` is the report, monotonic and
     *  readable from any thread, which is `messageScheduler`'s arrangement and
     *  `.pipe`'s.
     *
     *  ### `stop` and `clear`
     *
     *  Max's two commands, and Max's meanings. `stop` "causes makenote to send
     *  out immediate note-offs for all pitches it currently holds"; `clear`
     *  "erases all notes currently held by makenote, without sending
     *  note-offs" — the second being the one to reach for when the notes have
     *  already been released some other way. Both work by taking each slot away
     *  from its pending message with a single CAS, which is what makes them
     *  correct without a lock: a slot's life is
     *  ``FREE -> CLAIMED -> ARMED -> FIRING -> FREE``, the scheduler tag carries
     *  the slot index *and* its generation, and a delivery only happens if its
     *  own CAS wins. A release taken by `stop` cannot also be delivered, and a
     *  slot re-armed in between cannot be mistaken for the one the message named.
     *  `stop` emits in the order the notes were played, which is the order they
     *  would have been released in had nobody interrupted.
     *
     *  ### The order the two outlets fire in
     *
     *  Velocity first, then pitch — Max's outlets firing right to left, and here
     *  it is load-bearing rather than cosmetic. Every note sender downstream
     *  (`.noteon`, `.midiformat`, `.xnoteout`) takes its pitch on a hot inlet
     *  and its velocity on a cold one, so a pitch that arrived first would be
     *  sent with the *previous* note's velocity.
     *
     *  ### Four departures from Max, and why
     *
     *  - **Three inlets and two outlets, always.** Max's `makenote` grows a
     *    fourth inlet and a third outlet when it is given three creation
     *    arguments, the extra pair carrying a MIDI channel. Ports here are built
     *    in the constructor and creation arguments are parsed afterwards, so a
     *    port count that depends on the arguments is not expressible — the same
     *    wall `.pipe` met with Max's one-inlet-per-argument shape. Nothing is
     *    lost: every sender downstream already takes a channel of its own, which
     *    is where a channel belongs.
     *  - **No `repeatmode`.** Max's attribute picks what happens when a pitch
     *    that is already sounding is played again: poly, re-trigger, stop last,
     *    update duration, ignore. This object implements poly, which is Max's
     *    default — each note gets its own slot and its own deadline, and a
     *    repeated pitch is a second note. The other four are a separate ask.
     *  - **No `clock` message.** Max's routes the object onto a `setclock`.
     *    Milliseconds are the only unit here, so rather than misread a syntax it
     *    does not have — `1440 ticks` taken for its leading token becomes 1440
     *    ms, which is how `.clocker` got it wrong first — the duration inlet
     *    *refuses* anything `timeValue.h` recognises as a beat time and leaves
     *    the duration where it was. `.pipe` refuses it in the same words.
     *  - **A velocity of 0 schedules nothing.** In MIDI a note-on with velocity
     *    0 *is* a release; Max would still queue a second, identical release
     *    behind it. Here the pair goes out unchanged and no slot is spent, so a
     *    patch that plays releases through a `.makenote` cannot exhaust the
     *    pending set with notes that were never sounding.
     *
     *  ### Teardown is a `stop`
     *
     *  A patcher cleared or destroyed while notes are sounding releases them,
     *  and so does deleting the `.makenote` on its own (issue #758): the
     *  patcher runs a stop pass over every object *before* it unwires any of
     *  them, and this object's `Teardown` is `Release(true)` — the `stop` path
     *  exactly, in play order, bounded and allocation-free.
     *
     *  Without it the notes would not misfire, they would simply never end. A
     *  pending message is re-resolved against the block's pinned `GraphState`
     *  and against `pObject::InstanceTag()` before delivery, so a destroyed
     *  object's releases are *dropped* rather than sent through a dangling
     *  cord — and a dropped release is precisely the hanging note, with nothing
     *  left in the patch to bang. The ordering that fixes it belongs to the
     *  patcher rather than to this object, which is why the object could not
     *  have it alone: `patcherImplementation::Clear` used to unwire as it
     *  walked. A `stop` before the patch goes away is still the way to release
     *  at any other moment.
     *
     *  ### Real-time behaviour
     *
     *  `Calculate()` does nothing: this object is driven by its inlets and by
     *  the scheduler, and one that emitted would play a note on every DSP tick
     *  from a stimulus no patch sent. No path allocates, takes a lock or blocks —
     *  arming is a bounded walk of the slot table with one CAS attempt per slot,
     *  the velocity and duration are single atomic loads, `stop` and `clear` are
     *  the same bounded walk plus a fixed-capacity insertion sort over a stack
     *  array, and a release is one CAS and two sends.
     */
    PATCHER_CLASS(mMakeNote, YSE::OBJ::M_MAKENOTE)
    _NO_MESSAGES
    _NO_CALCULATE

    _INT_IN(IntIn)
    _FLOAT_IN(FloatIn)
    _LIST_IN(ListIn)

  public:
    /**
     *  @brief Notes one `.makenote` may hold sounding at once — the bounded,
     *         pre-allocated pending set issue #538 asks for.
     *
     *  Half the patcher-wide `messageScheduler::CAPACITY`, which is `.pipe`'s
     *  share and for `.pipe`'s reason: one object should be able to hold a wide
     *  chord without being able to starve every other deferring object in the
     *  patch on its own. A 64-note chord is past what any keyboard can play and
     *  past what most synths will sound.
     */
    static constexpr std::size_t CAPACITY = 64;

    /** @brief Max's velocity with no creation argument: "defaults to 0". */
    static constexpr int DEFAULT_VELOCITY = 0;

    /** @brief Max's duration with no creation argument, in milliseconds. */
    static constexpr int DEFAULT_DURATION = 0;

    /** @brief The highest MIDI velocity, and so the top of `Velocity()`. */
    static constexpr int MAX_VELOCITY = 127;

    /**
     *  @brief The velocity the next pitch will be played at: the stored
     *         parameter, clamped to MIDI's 0-127.
     *
     *  Clamped on read rather than on write, which is `.metro`'s and `.pipe`'s
     *  arrangement and for their reason — a live `SetParams` re-parse stores
     *  straight into the field from the audio thread and notifies nobody, so a
     *  clamp applied at the inlet would not cover that route. Clamped at all,
     *  unlike a pitch passing through, because this value decides whether a
     *  release is scheduled: an out-of-range velocity that silently read as 0,
     *  or as non-zero, would change the object's behaviour rather than just its
     *  output.
     */
    int Velocity() const;

    /**
     *  @brief How long the next note will sound, in milliseconds: the stored
     *         parameter with negatives and NaN clamped away. Clamped on read,
     *         for `Velocity()`'s reason.
     */
    int Duration() const;

    /** @brief Notes sounding right now — what a `stop` would release.
     *         Diagnostics and tests; a patch sees the same thing by sending
     *         `stop`. */
    std::size_t Pending() const {
      return pendingCount.load(std::memory_order_relaxed);
    }

    /**
     *  @brief Notes refused so far: the pending set was full (no note played at
     *         all), or the patcher-wide scheduler was full (the note played and
     *         was released at once).
     *
     *  Monotonic, readable from any thread, and the object's overflow report — a
     *  counter rather than a log line because the refusing thread may be the
     *  audio callback. See the class notes.
     */
    std::uint64_t Dropped() const {
      return dropped.load(std::memory_order_relaxed);
    }

    // The scheduler coming back with a note whose duration has elapsed.
    void DeliverDeferred(const deferredMessage& msg, YSE::THREAD thread) override;

    // The patcher is about to unwire this object (issue #758): release every
    // note still sounding, which is `stop`, while the cords the releases travel
    // down are still there. See the class notes.
    void Teardown(YSE::THREAD thread) override;

  private:
    // Slot lifecycle, packed with a generation into one atomic so a claim, a
    // stop and a delivery can each move a slot with a single CAS that no stale
    // scheduler tag can satisfy. `.pipe` and the scheduler's own Entry do
    // exactly this, for exactly this reason.
    //
    //   FREE --pitch--> CLAIMED --publish--> ARMED --delivery--> FIRING --> FREE
    //                                             \--stop/clear--> FIRING --> FREE
    static constexpr std::uint64_t STATE_FREE = 0;
    static constexpr std::uint64_t STATE_CLAIMED = 1;
    static constexpr std::uint64_t STATE_ARMED = 2;
    static constexpr std::uint64_t STATE_FIRING = 3;
    static constexpr std::uint64_t STATE_MASK = 3;

    // How the slot index and the slot generation share the scheduler's `int`
    // tag. Eight bits of index cover CAPACITY with room to spare; the rest is
    // generation, kept to 22 bits so a tag is always a positive int.
    static constexpr int TAG_INDEX_BITS = 8;
    static constexpr int TAG_INDEX_MASK = 0xFF;
    static constexpr std::uint64_t TAG_GEN_MASK = 0x3FFFFF;

    struct Slot {
      // Low 2 bits: state. Remaining bits: generation, bumped once per claim —
      // the ABA guard behind a scheduler tag's validity.
      std::atomic<std::uint64_t> stateGen{STATE_FREE};
      // The scheduler message this note is waiting on, for `stop` / `clear` to
      // hand the patcher-wide budget back early. Best effort by construction —
      // it is stored *after* the slot is published, because publishing has to
      // happen before the message is armed or a delivery could race ahead of
      // it — so a stale or missing handle costs a pending message that comes
      // due and finds nothing to do, never a wrong send.
      std::atomic<messageScheduler::Handle> handle{0};
      // Play order, which is the order `stop` releases in. Written under
      // CLAIMED and read after the ARMED->FIRING CAS, like the pitch, so it
      // needs no atomicity of its own.
      std::uint64_t seq = 0;
      int pitch = 0;
    };

    // A pitch arriving at the left inlet: send the attack, and arrange the
    // release.
    void Play(int pitch, YSE::THREAD thread);

    // Max's `clear` (emit = false) and `stop` (emit = true): take every pending
    // release away from the scheduler, and either forget it or send it now, in
    // the order the notes were played.
    void Release(bool emit, YSE::THREAD thread);

    // One pitch/velocity pair out of the two outlets, right to left.
    void Emit(int pitch, int velocity, YSE::THREAD thread);

    // Max's creation arguments and its middle and right inlets. Read on every
    // note and written from the inlets and by a live SetParams re-parse, so
    // atomic; unclamped, since Velocity() and Duration() are where the ranges
    // are applied.
    aInt velocity;
    aInt duration;

    Slot slots[CAPACITY];
    // Play tickets; what `stop` sorts by.
    std::atomic<std::uint64_t> nextSeq{1};
    std::atomic<std::size_t> pendingCount{0};
    std::atomic<std::uint64_t> dropped{0};
  };

} // namespace PATCHER
} // namespace YSE
