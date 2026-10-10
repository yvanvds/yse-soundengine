#pragma once
// `.poly` (issue #542) — the note-to-voice allocator.
//
// Not guarded on YSE_ENABLE_MIDI_DEVICE, for the reason `.flush`, `.sustain`,
// `.stripnote`, `.makenote`, `.midiflush` and the codec pair are not: this
// object opens no device and holds no port. It takes pitch/velocity pairs off
// ordinary cords and sends voice/pitch/velocity triples back out, which is what
// a patcher-built polyphonic instrument is made of on a platform with no MIDI
// hardware at all.
#include "../pObject.h"

#include <atomic>
#include <cstdint>
#include <string>

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief `.poly` — allocate incoming notes to a numbered pool of voices
     *         (issue #542), Max's `poly`.
     *
     *  ### What it is for
     *
     *  Max: "Provides polyphonic voice-allocation by allocating data to
     *  different individual voices." A pitch/velocity pair goes in; a voice
     *  number, a pitch and a velocity come out. A note-on takes a free voice and
     *  keeps it until its release arrives, and that release comes out carrying
     *  the *same* voice number — which is the whole point, because it is what
     *  lets a patch send a note-off to the one voice that is playing the note.
     *
     *  This is what lets a patcher build its own polyphonic instrument out of
     *  graph objects, the way `YSE::synth` does internally: a `.route` or a
     *  `.gate` on the voice-number outlet fans one keyboard out across N
     *  identical voice chains, and the allocator here decides which chain each
     *  note lands in.
     *
     *  It is **not** a voice-instance host — it hosts nothing, it renders
     *  nothing, and it has no notion of what a voice *is* beyond a number. It
     *  allocates numbers, and the patch decides what a number means.
     *
     *  ### The allocation policy is the synth's, deliberately
     *
     *  `SYNTH::implementationObject::allocateInGroup` is the engine's answer to
     *  this exact question, and this object gives the same one rather than
     *  inventing a second, subtly different one (issue #542's non-goal):
     *
     *  - A **free voice** is preferred, scanned in ascending voice order, so a
     *    patch with more voices than it uses always plays on the low ones and a
     *    patch can reason about which chains are live.
     *  - With **no free voice**, the **oldest sounding note** is the one that
     *    gives way. That is the synth's rule once its two-tier form is applied
     *    here: the synth prefers a voice already in its release tail and falls
     *    back to the oldest overall, and here a voice whose note-off has been
     *    sent is free rather than releasing — there is no tail this object can
     *    see, the tail belonging to whatever the patch built downstream. So the
     *    two tiers collapse into one, and it is also Max's rule word for word:
     *    "it turns off the note it has held the longest and puts the new note in
     *    its place".
     *
     *  A steal sends the stolen note's release **before** the new note's attack,
     *  on the same voice number, so the chain downstream is told to let go
     *  before it is told to play again. Without that the patch would have one
     *  voice sounding two notes and no way of ever releasing the first.
     *
     *  ### Overflow: what happens when there is no voice and no stealing
     *
     *  Max's second creation argument decides, and both halves are here. With
     *  `steal` at 0 — Max's default — a note that finds no free voice goes out
     *  the **overflow outlet** as the list `pitch velocity` and is not tracked;
     *  with `steal` non-zero the oldest note gives way instead.
     *
     *  A note-off whose pitch **no voice is holding** also goes out the overflow
     *  outlet, as `pitch 0`. That is not decoration: in overflow mode the
     *  attacks that were refused went out there, so their releases have to
     *  follow them or a patch that wired the overflow outlet to a monosynth
     *  would hang every note it sent there. It costs a steal-mode patch nothing,
     *  such a patch having no reason to wire that outlet at all.
     *
     *  ### One departure from Max: the overflow outlet always exists
     *
     *  Max grows the fourth outlet only when it is *not* stealing, since a
     *  stealing `poly` can never overflow. Ports here are built in the
     *  constructor and creation arguments are parsed afterwards, so a port count
     *  that depends on the arguments is not expressible — the same wall
     *  `.makenote` met with Max's channel pair and `.pipe` with its
     *  one-inlet-per-argument shape. The outlet is therefore always the fourth
     *  one and simply never fires while stealing, which is the harmless half of
     *  the difference: a patch reading a JSON dump sees a stable shape, and
     *  nothing downstream can tell the difference between an outlet that is
     *  silent and one that is not there.
     *
     *  ### Which half of the pair moves it, and the order the outlets fire in
     *
     *  Two inlets, Max's. The left takes the pitch and is the one that acts; the
     *  right stores a velocity for the pitches arriving *after* it and outputs
     *  nothing itself. A list in the left inlet is Max's inlet distribution
     *  written on one cord — `60 100` stores velocity 100 and then plays pitch
     *  60 — which is what lets a single cord from `.midiparse`'s note outlet or
     *  from a `.pack` work.
     *
     *  A velocity of 0 is a release, which is how the whole MIDI world spells
     *  one and how every note source in the patcher reports one. So a non-zero
     *  velocity allocates and a velocity of 0 frees, and the freeing is by
     *  *pitch*: the voice released is the one holding that pitch, which is the
     *  behaviour the object exists for.
     *
     *  The three outlets fire right to left, Max's order and load-bearing here
     *  rather than cosmetic: velocity, then pitch, then the voice number last.
     *  Everything downstream that takes the triple takes the voice number on its
     *  hot inlet — that is the value that routes the note — so a voice number
     *  sent first would carry the *previous* note's pitch and velocity with it.
     *
     *  ### A repeated pitch takes a second voice
     *
     *  Two note-ons for the same pitch are two notes and get two voices, which
     *  is `.makenote`'s answer (Max's `repeatmode` default, "poly") and the only
     *  one that keeps a voice pool a voice pool: collapsing them would leave the
     *  second attack with no voice of its own to be released from. Their
     *  note-offs then pair oldest-first — the first release frees the voice that
     *  was allocated first — so a source that balances its note-ons and
     *  note-offs stays balanced. A source that sends one release for two attacks
     *  leaves the second voice sounding, and `stop` is what clears that up.
     *
     *  ### `stop`
     *
     *  Max's only message, and Max's meaning: "immediately sends note-offs for
     *  all the notes currently being held by poly, freeing all voices". They go
     *  out in ascending voice order, each as a release on its own voice number,
     *  which is the deterministic order the voice table walks in rather than the
     *  accident of arrival.
     *
     *  There is no `clear` and no `bang` method: Max's `poly` has neither, and
     *  giving a bang a meaning here that its siblings do not share would make
     *  the family differ in the one place a patch would never look.
     *
     *  ### Any pitch at all, and that is not `.flush`'s bargain
     *
     *  `.flush` and `.sustain` hold a 128-bit bitmap indexed by pitch and so
     *  cannot remember a pitch outside the MIDI note range. This object holds a
     *  *table of voices*, each with the pitch it happens to be playing, so a
     *  patch driving a non-MIDI synth through wider values is tracked exactly
     *  like any other — allocated, released and stolen the same way. The bound
     *  here is on how many notes may sound at once, never on what they may be.
     *
     *  ### The table is fixed size, which is the whole real-time story
     *
     *  A message handler runs on whichever thread dispatched the message, and
     *  in-patcher delivery dispatches on `T_DSP` — routinely the audio callback.
     *  So the voice table is ``MAX_VOICES`` entries allocated with the object,
     *  and nothing on the allocate path, the release path, the steal path or the
     *  `stop` path allocates, takes a lock or blocks. Every one of them is a
     *  bounded walk of that table. The overflow outlet's text is built into a
     *  string reserved at construction, `.funnel`'s arrangement and for its
     *  reason.
     *
     *  Two threads sending at once — or a patch that wires an outlet back into
     *  the left inlet, which a steal or a `stop` would otherwise recurse
     *  through — are refused by a test-and-set guard whose loser is counted on
     *  `Dropped()` rather than made to spin. That is `.flush`'s and
     *  `.sustain`'s arrangement and it is here for the same reason: the table
     *  and its count are ordinary members, and walking one while another thread
     *  rewrites it is not a thing that can be made to work by being careful. The
     *  velocity is atomic and sits outside the guard, so storing one never
     *  fails.
     *
     *  ### It stops on teardown too, and not by its own doing
     *
     *  A patcher cleared or destroyed with voices sounding releases them (issue
     *  #758), and so does deleting the `.poly` on its own: the patcher runs a
     *  stop pass over every object *before* it unwires any of them, and this
     *  object's `Teardown` is a `stop`. Without it a patch torn down mid-chord
     *  would leave every voice chain it built holding a note with nothing left
     *  upstream to release it — and this object is the *only* thing that knows
     *  which voice holds which note, so nothing else could. That ordering is the
     *  patcher's rather than this object's, which is why the object could not
     *  have it alone: `patcherImplementation::Clear` used to unwire as it
     *  walked, so a `.poly` reached after the voices downstream of it would have
     *  sent its releases into cords that no longer existed.
     *
     *  Nothing at all can be promised for a process killed outright.
     *
     *  ### Real-time behaviour
     *
     *  `Calculate()` does nothing: the object is driven entirely by its inlets,
     *  and one that emitted would allocate a voice on every DSP tick from a
     *  stimulus no patch sent.
     */
    PATCHER_CLASS(mPoly, YSE::OBJ::M_POLY)
    _NO_MESSAGES
    _NO_CALCULATE

    _INT_IN(IntIn)
    _FLOAT_IN(FloatIn)
    _LIST_IN(ListIn)

    // The patcher is about to unwire this object (issue #758): release every
    // voice still sounding, which is what `stop` does, while the cords the
    // releases travel down are still there. See the class notes.
    void Teardown(YSE::THREAD thread) override;

  public:
    /**
     *  @brief The largest voice pool one `.poly` can be given, and the size of
     *         the table allocated with the object.
     *
     *  128 is the whole MIDI note range, so a patch can hold every distinct key
     *  on a keyboard down at once without overflowing. Beyond that a pool stops
     *  being a pool: the point of allocating voices is that there are fewer of
     *  them than there are notes, and a patch is going to have to build every
     *  one of these chains by hand.
     */
    static constexpr int MAX_VOICES = 128;

    /** @brief Voices with no creation argument: Max's "poly will hold up to 16
     *         notes". */
    static constexpr int DEFAULT_VOICES = 16;

    /**
     *  @brief The voice pool size — Max's first creation argument, clamped to
     *         1..``MAX_VOICES``.
     *
     *  Clamped on read rather than on write, which is `.makenote`'s and
     *  `.metro`'s arrangement and for their reason: a live `SetParams` re-parse
     *  stores straight into the field from the audio thread and notifies nobody,
     *  so a clamp applied at construction would not cover that route.
     *
     *  Shrinking the pool under a sounding patch never strands a note: only
     *  *allocation* is bounded by this value, while a release, a `stop` and the
     *  teardown pass walk the whole table. A voice left above the new bound
     *  therefore still gets its note-off; it simply never plays again.
     */
    int Voices() const;

    /** @brief Whether the pool steals — Max's second creation argument, false
     *         (overflow) unless it was given non-zero. */
    bool Steals() const {
      return steal.load() != 0;
    }

    /**
     *  @brief The velocity the next pitch will be paired with — Max's right
     *         inlet, 0 on a fresh object.
     *
     *  Unclamped and reported exactly as it was given, which is `.flush`'s and
     *  `.stripnote`'s arrangement: the only value this object reads meaning into
     *  is 0, that being what a release is spelled with. Diagnostics and tests; a
     *  patch sees the same thing by sending a pitch.
     */
    int Velocity() const {
      return (int)velocity.load();
    }

    /** @brief Voices sounding right now: what a `stop` would release.
     *         Diagnostics and tests — a patch sees the same thing by sending
     *         `stop`. */
    int Held() const {
      return heldCount;
    }

    /** @brief The pitch voice @p voice is playing, or -1 when it is free or is
     *         not a voice at all. Numbered from 1, the way a patch sees them.
     *         Diagnostics and tests. */
    int PitchOfVoice(int voice) const;

    /** @brief Messages refused — a concurrent or re-entrant send. Monotonic,
     *         readable from any thread; diagnostics and tests only. */
    std::uint64_t Dropped() const {
      return dropped.load(std::memory_order_relaxed);
    }

  private:
    // One slot of the pool. Plain members under the busy guard, never atomics:
    // a voice is only ever read and written by the one thread that got past
    // Enter().
    struct Voice {
      // What it is playing. Any int at all — see the class notes on why this is
      // not a bitmap.
      int pitch = 0;
      // Allocation ticket, and what "held the longest" is measured with. Never
      // reused, so an older voice always compares lower.
      std::uint64_t age = 0;
      bool active = false;
    };

    // The pitch half, which is the half that acts: allocate, or release.
    void Play(int pitch, YSE::THREAD thread);

    // A non-zero velocity: take a free voice, steal the oldest, or overflow.
    void NoteOn(int pitch, int noteVelocity, YSE::THREAD thread);

    // A velocity of 0: free the voice holding this pitch — the oldest of them
    // if a patch played it twice — and send its release on that voice's number.
    void NoteOff(int pitch, YSE::THREAD thread);

    // Max's `stop`: release every sounding voice, in ascending voice order.
    void Release(YSE::THREAD thread);

    // One voice/pitch/velocity triple, right to left. `voice` is the 1-based
    // number a patch sees.
    void Emit(int voice, int pitch, int noteVelocity, YSE::THREAD thread);

    // The fourth outlet: a note no voice could be found for, as `pitch
    // velocity`. See the class notes on why a stray release comes out here too.
    void Overflow(int pitch, int noteVelocity, YSE::THREAD thread);

    bool Enter();
    void Leave();

    // Max's right inlet, read on every pitch and written from whichever thread
    // sent one — the audio callback included — so atomic. Outside the guard, so
    // storing a velocity never fails.
    aInt velocity{0};

    // Max's creation arguments. Atomic because a live SetParams re-parse writes
    // them from the audio thread; unclamped, Voices() being where the range is
    // applied.
    aInt voiceCount;
    aInt steal;

    // The pool. Fixed size and allocated with the object: this is written from
    // the audio thread, where a table that could grow has no place.
    Voice table[MAX_VOICES];
    int heldCount = 0;
    // Allocation tickets. Under the guard like the table, so a plain counter.
    std::uint64_t nextAge = 1;

    // The overflow outlet's text, reserved at construction and refilled
    // immediately before each send — `.funnel`'s arrangement, and for its
    // reason: the send path is synchronous, so a patch looping an outlet back
    // in re-enters during it.
    std::string overflowText;

    std::atomic<bool> busy{false};
    std::atomic<std::uint64_t> dropped{0};
  };

} // namespace PATCHER
} // namespace YSE
