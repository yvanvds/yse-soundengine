#pragma once
// `.sustain` (issue #541) — the pedal, written as a patcher object.
//
// Not guarded on YSE_ENABLE_MIDI_DEVICE, for the reason `.flush`, `.stripnote`,
// `.makenote`, `.midiflush` and the codec pair are not: this object opens no
// device and holds no port. It watches pitch/velocity pairs on ordinary cords
// and sends pitch/velocity pairs back out, which is as useful in front of a
// patcher-built synth on a platform with no MIDI hardware at all as it is in
// front of a hardware rack.
#include "../pObject.h"

#include <atomic>
#include <cstdint>

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief `.sustain` — hold note-offs while the pedal is down (issue #541),
     *         Max's `sustain`.
     *
     *  ### What it is for
     *
     *  Max: "holds note-off messages for release", the object behaving as a
     *  sustain pedal does. A pitch in the left inlet is paired with the stored
     *  velocity, and what happens next depends on one bit of state:
     *
     *  - A **note-on** — any velocity but 0 — always passes straight through.
     *    The pedal never delays an attack; it delays only a release.
     *  - A **note-off** — velocity 0, which is how the whole MIDI world spells
     *    one — passes through when the pedal is up, and is *held back* when the
     *    pedal is down.
     *  - Lifting the pedal sends every note-off that was held back, in ascending
     *    pitch order, and empties the set.
     *
     *  That is the whole object, and it is the pedal a keyboard has: keys come
     *  up under the player's fingers and the notes keep sounding until the foot
     *  does.
     *
     *  ### It agrees with the synth, and deliberately
     *
     *  `SYNTH::implementationObject::handleSustain` is the same rule expressed
     *  in voices rather than in messages: with the pedal down a NOTE_OFF marks
     *  the voice `heldBySustain` instead of releasing it, and lifting the pedal
     *  drops that claim so every voice whose key is up releases then. Same
     *  deferral, same trigger, same moment. The two are reached differently —
     *  the synth's pedal is CC 64 arriving at a synth channel, this one is a
     *  toggle on a cord — and this object exists for the patch that is *not*
     *  driving the built-in synth: a `.midiout` to a hardware rack, or a voice
     *  assembled out of patcher objects. Where a patch drives the built-in
     *  synth, the synth's own pedal is already doing this and a `.sustain` in
     *  front of it would only defer what is going to be deferred again.
     *
     *  ### It is not `.flush`, and the set is not the same set
     *
     *  `.flush` (#540) remembers the notes that are **sounding** and releases
     *  them on demand; its set is filled by note-ons. This object remembers the
     *  **releases it swallowed** and sends them when the pedal lifts; its set is
     *  filled by note-offs, and a note-on puts nothing in it. Two objects with
     *  the same 128-bit shape holding opposite things, which is exactly why the
     *  bookkeeping is not shared — as `.midiflush`'s 16x128 channel bitmap and
     *  `.makenote`'s table of scheduler handles are not shared either. What the
     *  four have in common is one line of bit-twiddling; a header binding four
     *  translation units together to share it would cost more than it saved.
     *
     *  The two compose, and in that order: a `.sustain` in front of a `.flush`
     *  leaves the `.flush` holding what is really sounding, pedal included.
     *
     *  ### Three inlets, Max's
     *
     *  Left takes the pitch and is the one that acts. Middle stores a velocity
     *  for the pitches arriving *after* it and outputs nothing itself. Right
     *  takes the pedal: non-zero presses it, 0 lifts it, and lifting it is what
     *  sends the held releases. A list in the left inlet is Max's inlet
     *  distribution written on one cord — `60 0` stores velocity 0 and then
     *  plays pitch 60 — which is what lets a single cord from `.midiparse`'s
     *  note outlet or from a `.pack` work.
     *
     *  ### `sustain`, `flush` and `clear`
     *
     *  Max's three commands, in the left inlet and spelled as words so they can
     *  never be mistaken for pitches:
     *
     *  - `sustain 1` / `sustain 0` — "equivalent to pressing or releasing the
     *    sustain pedal", the right inlet reached by message instead of by cord.
     *  - `flush` — "output all held note-offs", now, whatever the pedal is
     *    doing. The pedal stays where it is; only the set is emptied.
     *  - `clear` — "clears the object's internal memory. No note-off messages
     *    are output": the one to reach for when the notes have already been
     *    released some other way, and the exact counterpart of `.flush`'s and
     *    `.makenote`'s `clear`.
     *
     *  There is no `bang` method, Max's `sustain` having none. `flush` is the
     *  word that does what a bang does on a `.flush`, and giving a bang the same
     *  meaning here would make the two objects differ in the one place a patch
     *  would never look.
     *
     *  ### One departure from Max: no `repeatmode`
     *
     *  Max's attribute picks what happens when a pitch whose release is being
     *  held is played again: 0 historical, 1 re-trigger, 2 stop-last. This
     *  object implements 0, which is Max's default — the held release stays
     *  held, the new attack goes out on its own, and the pedal still owes that
     *  pitch exactly one note-off. The other two are a separate ask, as they
     *  are for `.makenote` (#538), and the reason is the same: mode 1 would have
     *  to emit a release this object was asked to swallow, and mode 2 would have
     *  to count repeats per pitch rather than remember them, so neither is the
     *  bitmap with a flag on it that mode 0 is.
     *
     *  ### Pitches outside 0-127 pass through and are never held
     *
     *  The set is a fixed bitmap, so it spans the MIDI note range and nothing
     *  else. A pitch outside it is still sent on unaltered — this object does
     *  not rewrite what passes through it, and a patch driving a non-MIDI synth
     *  through wider values is entitled to its values — and its *release* goes
     *  straight out even with the pedal down. That is the safe half of the
     *  choice rather than the tidy one: a release swallowed by a set that cannot
     *  remember it would never be sent by anything, which is a hanging note, and
     *  a note released early is merely a short one.
     *
     *  ### The set is fixed size, which is the whole real-time story
     *
     *  A message handler runs on whichever thread dispatched the message, and
     *  in-patcher delivery dispatches on `T_DSP` — routinely the audio callback.
     *  So what is held is a bitmap of 128 bits allocated with the object, 16
     *  bytes of it, and nothing on the pass-through path, the release path or
     *  the `clear` path allocates, takes a lock or blocks. A pedal lift is
     *  bounded by the bitmap rather than by how many keys the player let go of.
     *
     *  Two threads sending at once — or a patch that wires an outlet back into
     *  the left inlet, which a pedal lift would otherwise recurse through — are
     *  refused by a test-and-set guard whose loser is counted on `Dropped()`
     *  rather than made to spin. That is `.flush`'s and `.midiflush`'s
     *  arrangement and it is here for the same reason: the bitmap and its count
     *  are ordinary members, and walking one while another thread rewrites it is
     *  not a thing that can be made to work by being careful.
     *
     *  The velocity and the pedal flag are atomic and sit *outside* the guard,
     *  so neither can fail to be stored. That matters more for the pedal than
     *  for the velocity: a lift that was refused would leave the object's idea
     *  of the pedal contradicting the player's foot for as long as the patch
     *  ran. A refused lift therefore still lifts — only the sending of the held
     *  releases is skipped and counted, and the next `flush`, pedal lift or
     *  teardown sends them.
     *
     *  ### It fires on teardown too, and not by its own doing
     *
     *  A patcher cleared or destroyed with the pedal held flushes what the pedal
     *  is holding (issue #758), and so does deleting the `.sustain` on its own:
     *  the patcher runs a stop pass over every object *before* it unwires any of
     *  them, and this object's `Teardown` is a `flush`. Without it, a patch torn
     *  down mid-pedal would leave the rack sounding with nothing left in the
     *  patch to lift the foot — the note-offs were never sent by anyone, because
     *  this object swallowed them. That ordering is the patcher's rather than
     *  this object's, which is why the object could not have it alone:
     *  `patcherImplementation::Clear` used to unwire as it walked, so a
     *  `.sustain` reached after the `.noteon` downstream of it would have sent
     *  its releases into a cord that no longer existed.
     *
     *  Nothing at all can be promised for a process killed outright.
     *
     *  ### Real-time behaviour
     *
     *  `Calculate()` does nothing: the object is driven entirely by its inlets,
     *  and one that emitted would release a note on every DSP tick from a
     *  stimulus no patch sent.
     */
    PATCHER_CLASS(mSustain, YSE::OBJ::M_SUSTAIN)
    _NO_MESSAGES
    _NO_CALCULATE

    _INT_IN(IntIn)
    _FLOAT_IN(FloatIn)
    _LIST_IN(ListIn)

    // The patcher is about to unwire this object (issue #758): send the
    // note-offs the pedal is holding, which is what `flush` does, while the
    // cords they travel down are still there. See the class notes.
    void Teardown(YSE::THREAD thread) override;

  public:
    /** @brief Pitches tracked — the whole of the MIDI note range, and the width
     *         of the bitmap. */
    static constexpr int PITCHES = 128;

    /**
     *  @brief The velocity the next pitch will be paired with — Max's middle
     *         inlet, 0 on a fresh object.
     *
     *  Unclamped and reported exactly as it was given, which is `.stripnote`'s
     *  and `.flush`'s arrangement: the only value this object reads meaning into
     *  is 0, that being what a release is spelled with. Diagnostics and tests; a
     *  patch sees the same thing by sending a pitch.
     */
    int Velocity() const {
      return (int)velocity.load();
    }

    /** @brief Whether the pedal is down — Max's right inlet and its `sustain`
     *         message. Up on a fresh object, so an object nobody has touched
     *         passes everything through. */
    bool Pedal() const {
      return pedal.load(std::memory_order_relaxed);
    }

    /** @brief Note-offs being held back: what lifting the pedal would send.
     *         Diagnostics and tests — a patch sees the same thing by lifting the
     *         pedal or sending `flush`. */
    int Held() const {
      return heldCount;
    }

    /** @brief Whether a note-off for @p pitch is being held. Out of the 0-127
     *         range is false rather than an error, nothing outside it ever being
     *         held at all. Diagnostics and tests. */
    bool IsHeld(int pitch) const;

    /** @brief Messages refused — a concurrent or re-entrant send. Monotonic,
     *         readable from any thread; diagnostics and tests only. */
    std::uint64_t Dropped() const {
      return dropped.load(std::memory_order_relaxed);
    }

  private:
    /** @brief 32-bit words the 128 pitches take. */
    static constexpr int WORDS = PITCHES / 32;

    // The pitch half, which is the half that acts: pass the pair on, or hold its
    // release back.
    void Play(int pitch, YSE::THREAD thread);

    // Max's right inlet and its `sustain` message. Pressing is a stored bit and
    // nothing else; lifting is what sends.
    void SetPedal(bool down, YSE::THREAD thread);

    // Max's `flush` (emit = true) and `clear` (emit = false): empty the set,
    // either sending the note-offs in it or forgetting them.
    void Release(bool emit, YSE::THREAD thread);

    // One pitch/velocity pair out of the two outlets, right to left.
    void Emit(int pitch, int noteVelocity, YSE::THREAD thread);

    // Set or clear one bit, keeping `heldCount` with it. A second note-off for a
    // pitch already held is one held release, which is what makes the count a
    // count. A pitch outside 0-127 is never held; see the class notes.
    void Mark(int pitch, bool on);

    bool Enter();
    void Leave();

    // Max's middle inlet, read on every pitch and written from whichever thread
    // sent one — the audio callback included — so atomic. Outside the guard, so
    // storing a velocity never fails.
    aInt velocity{0};

    // The pedal. Outside the guard too, and for a sharper reason than the
    // velocity's: see the class notes on a refused lift.
    std::atomic<bool> pedal{false};

    // The note-offs held back. Fixed size and allocated with the object: this is
    // written from the audio thread, where a set that could rehash or grow has
    // no place.
    std::uint32_t held[WORDS] = {};
    int heldCount = 0;

    std::atomic<bool> busy{false};
    std::atomic<std::uint64_t> dropped{0};
  };

} // namespace PATCHER
} // namespace YSE
