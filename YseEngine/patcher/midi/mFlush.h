#pragma once
// `.flush` (issue #540) — the note-pair safety valve.
//
// Not guarded on YSE_ENABLE_MIDI_DEVICE, for the reason `.makenote`,
// `.stripnote`, `.midiflush` and the codec pair are not: this object opens no
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
     *  @brief `.flush` — release every note the patcher is holding (issue
     *         #540), Max's `flush`.
     *
     *  ### What it is for
     *
     *  Max: "keeps track of all note-ons passed through it, and produces
     *  note-off messages for any held notes when it receives a bang." A pitch
     *  in the left inlet passes straight through with the stored velocity, and
     *  on the way past the object remembers whether that pair turned a note on
     *  or off. A bang then sends one release — the same pitch with velocity 0 —
     *  for every note still sounding.
     *
     *  This is the panic stop for a patch that plays notes out of its own
     *  logic. A sequence interrupted between its note-ons and its note-offs, a
     *  `.metro` switched off mid-phrase, a generative voice whose releases were
     *  never written: all of them leave notes sounding that nothing downstream
     *  can find again, because every note sender in the patcher (`.noteon`,
     *  `.midiformat`, the `.x*out` family) is a stateless formatter that sends
     *  what it is handed and remembers nothing.
     *
     *  ### It is not `.midiflush`, and the difference is the input contract
     *
     *  `.midiflush` (#537) watches a MIDI **byte stream** — it decodes running
     *  status, steps over real-time bytes interleaved mid-message, skips
     *  system-exclusive dumps, and releases what *that stream* left sounding,
     *  on the channel each note carried, as raw note-off bytes. It belongs
     *  **after** the formatters, in the cord that runs to `.midiout`.
     *
     *  This object watches **pitch/velocity pairs**, which is what a patch
     *  deals in *before* anything has been formatted, and it belongs there:
     *  between the logic that plays notes and the sender that encodes them. So
     *  it has no channel and wants none — at that point in a patch the channel
     *  has not been decided yet, every sender downstream taking a channel of
     *  its own — and its releases go out as the same pair-of-ints its input is,
     *  ready to be wired into the very `.noteon` or `.makenote` the attacks
     *  went through.
     *
     *  The two are therefore complements rather than alternatives, and a patch
     *  may reasonably hold both: `.flush` releases what the *patcher* is
     *  holding, `.midiflush` releases what the *stream* left sounding. Neither
     *  can be reached from the other's position in the chain — a byte watcher
     *  in front of the formatters would see no bytes, and a pair watcher behind
     *  them would see no pairs.
     *
     *  Their held-note bookkeeping is not shared, and deliberately so. The two
     *  sets are keyed differently: `.midiflush` holds a 16x128 bitmap indexed by
     *  the channel nibble it decoded out of a status byte, while this object
     *  holds 128 bits indexed by a bare pitch that arrived as an ordinary int
     *  and may be any value at all. `.makenote`'s pending set is not a bitmap
     *  in the first place — it is a table of scheduler handles, because each of
     *  its notes owes a release at its own deadline. What the three have in
     *  common is one line of bit-twiddling; a header binding three translation
     *  units together to share it would cost more than it saved.
     *
     *  ### The pair, and which half moves it
     *
     *  Two inlets and two outlets, Max's shape and `.stripnote`'s. The left
     *  inlet takes the pitch and is the one that acts; the right inlet stores a
     *  velocity for the pitches arriving *after* it and outputs nothing itself.
     *  A list in the left inlet is Max's inlet distribution written on one
     *  cord — `60 100` stores velocity 100 and then plays pitch 60 — which is
     *  what lets a single cord from `.midiparse`'s note outlet or from a
     *  `.pack` work.
     *
     *  A velocity of 0 is a release, which is how the whole MIDI world spells
     *  one and how every note source in the patcher reports one. So a pair with
     *  a non-zero velocity marks its pitch sounding and a pair with velocity 0
     *  clears it, and either way the pair passes through untouched: this object
     *  never filters — that is `.stripnote`'s job — it only remembers.
     *
     *  ### What a bang sends, and `clear`
     *
     *  One pair per sounding note, velocity 0, in ascending pitch order, and
     *  the set is emptied as they go. A note is released exactly once: banging
     *  twice sends nothing the second time, because after the first bang
     *  nothing is sounding. Notes played after a flush are tracked from scratch.
     *
     *  Max's `clear` erases the set without sending anything — the message to
     *  reach for when the notes have already been released some other way, and
     *  the exact counterpart of `.makenote`'s `clear`.
     *
     *  ### Pitches outside 0-127 pass through but are not tracked
     *
     *  The set is a fixed bitmap, so it spans the MIDI note range and nothing
     *  else. A pitch outside it is still sent on unaltered — this object does
     *  not rewrite what passes through it, and a patch driving a non-MIDI synth
     *  through wider values is entitled to its values — but it cannot be
     *  remembered, and a bang will not release it. Ignoring it is the honest
     *  answer: clamping would file the note under a pitch that is not its own
     *  and a bang would then release a note nobody played.
     *
     *  ### The set is fixed size, which is the whole real-time story
     *
     *  A message handler runs on whichever thread dispatched the message, and
     *  in-patcher delivery dispatches on `T_DSP` — routinely the audio
     *  callback. So what is sounding is a bitmap of 128 bits allocated with the
     *  object, 16 bytes of it, and nothing on the pass-through path, the flush
     *  path or the `clear` path allocates, takes a lock or blocks. The flush is
     *  bounded by the bitmap rather than by how many notes a patch managed to
     *  strand.
     *
     *  Two threads sending at once — or a patch that wires an outlet back into
     *  the left inlet, which a flush would otherwise recurse through — are
     *  refused by a test-and-set guard whose loser is counted on `Dropped()`
     *  rather than made to spin. That is `.midiflush`'s arrangement and it is
     *  here for the same reason: the bitmap and its count are ordinary members,
     *  and walking one while another thread rewrites it is not a thing that can
     *  be made to work by being careful. The velocity is atomic and sits
     *  outside the guard, so storing one never fails.
     *
     *  ### It fires on teardown too, and not by its own doing
     *
     *  A patcher cleared or destroyed while notes are sounding flushes them
     *  (issue #758), and so does deleting the `.flush` on its own: the patcher
     *  runs a stop pass over every object *before* it unwires any of them, and
     *  this object's `Teardown` is a flush. That ordering is the patcher's
     *  rather than this object's, which is why the object could not have it
     *  alone — `patcherImplementation::Clear` used to unwire as it walked, so a
     *  `.flush` reached after the `.noteon` downstream of it would have sent
     *  its releases into a cord that no longer existed.
     *
     *  What the releases meet on the way out is a fully wired patch, so they
     *  behave exactly as a bang's do. A bang before the patch goes away is
     *  still the way to flush at any other moment, and nothing at all can be
     *  promised for a process killed outright.
     *
     *  ### Real-time behaviour
     *
     *  `Calculate()` does nothing: the object is driven entirely by its inlets,
     *  and one that emitted would release a note on every DSP tick from a
     *  stimulus no patch sent.
     */
    PATCHER_CLASS(mFlush, YSE::OBJ::M_FLUSH)
    _NO_MESSAGES
    _NO_CALCULATE

    _BANG_IN(Flush)
    _INT_IN(IntIn)
    _FLOAT_IN(FloatIn)
    _LIST_IN(ListIn)

    // The patcher is about to unwire this object (issue #758): release what is
    // sounding, which is what a bang does, while the cords the releases travel
    // down are still there. See the class notes.
    void Teardown(YSE::THREAD thread) override;

  public:
    /** @brief Pitches tracked — the whole of the MIDI note range, and the width
     *         of the bitmap. */
    static constexpr int PITCHES = 128;

    /**
     *  @brief The velocity the next pitch will be paired with — Max's right
     *         inlet, 0 on a fresh object.
     *
     *  Unclamped and reported exactly as it was given, which is `.stripnote`'s
     *  arrangement: the only value this object reads meaning into is 0, that
     *  being what a release is spelled with. Diagnostics and tests; a patch
     *  sees the same thing by sending a pitch.
     */
    int Velocity() const {
      return (int)velocity.load();
    }

    /** @brief Notes currently sounding: what the next bang would release.
     *         Diagnostics and tests — a patch sees the same thing by banging. */
    int Held() const {
      return heldCount;
    }

    /** @brief Whether @p pitch is sounding. Out of the 0-127 range is false
     *         rather than an error, nothing outside it being tracked at all.
     *         Diagnostics and tests. */
    bool IsHeld(int pitch) const;

    /** @brief Messages refused — a concurrent or re-entrant send. Monotonic,
     *         readable from any thread; diagnostics and tests only. */
    std::uint64_t Dropped() const {
      return dropped.load(std::memory_order_relaxed);
    }

  private:
    /** @brief 32-bit words the 128 pitches take. */
    static constexpr int WORDS = PITCHES / 32;

    // The pitch half, which is the half that acts: pass the pair on, and
    // remember what it did.
    void Play(int pitch, YSE::THREAD thread);

    // Max's `bang` (emit = true) and `clear` (emit = false): empty the set,
    // either releasing what was in it or forgetting it.
    void Release(bool emit, YSE::THREAD thread);

    // One pitch/velocity pair out of the two outlets, right to left.
    void Emit(int pitch, int noteVelocity, YSE::THREAD thread);

    // Set or clear one bit, keeping `heldCount` with it. A repeated note-on for
    // a pitch already sounding is one note, and a release for a pitch that is
    // not sounding is nothing — which is what makes the count a count. A pitch
    // outside 0-127 is not tracked; see the class notes.
    void Mark(int pitch, bool on);

    bool Enter();
    void Leave();

    // Max's right inlet, read on every pitch and written from whichever thread
    // sent one — the audio callback included — so atomic. Outside the guard, so
    // storing a velocity never fails.
    aInt velocity{0};

    // What is sounding. Fixed size and allocated with the object: this is
    // written from the audio thread, where a set that could rehash or grow has
    // no place.
    std::uint32_t held[WORDS] = {};
    int heldCount = 0;

    std::atomic<bool> busy{false};
    std::atomic<std::uint64_t> dropped{0};
  };

} // namespace PATCHER
} // namespace YSE
