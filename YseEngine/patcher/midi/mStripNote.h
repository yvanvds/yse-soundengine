#pragma once
// `.stripnote` (issue #539) — the note-off filter.
//
// Not guarded on YSE_ENABLE_MIDI_DEVICE, for the reason `.makenote`,
// `.midiflush` and the codec pair are not: this object opens no device and
// holds no port. It takes a pitch and a velocity and either passes them on or
// does not, which is as useful in front of a patcher-built synth on a platform
// with no MIDI hardware at all as it is in front of a hardware rack.
#include "../pObject.h"

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief `.stripnote` — pass note-ons, drop the releases (issue #539),
     *         Max's `stripnote`.
     *
     *  ### What it is for
     *
     *  Max: "Only pass note-on messages: those having any velocity above 0."
     *  A pitch in the left inlet comes out again — with the velocity that is
     *  stored at the time — but only if that velocity is not 0. If it is 0,
     *  nothing at all comes out.
     *
     *  This is the standard first object after a note source when a patch only
     *  cares about attacks: triggering a sample, advancing a sequence, firing
     *  anything one-shot. Every note source in the patcher reports a release as
     *  a pitch with velocity 0 — `.notein` and `.midiparse` both fold a
     *  note-off message and a zero-velocity note-on into that one shape — so a
     *  patch wired straight through fires *twice* per key, once on the way down
     *  and once on the way up. The second one is invariably a bug, and it is
     *  the kind that looks like a doubled sample or a sequence running at
     *  double speed rather than like a MIDI problem.
     *
     *  `.midiparse`'s note outlet sends the pair as one list, which is exactly
     *  what this object's left inlet reads, so the standard patch is one cord:
     *  `.midiparse` -> `.stripnote` -> whatever the attack should trigger.
     *
     *  ### The pair, and which half moves it
     *
     *  Two inlets and two outlets, Max's shape. The left inlet takes the pitch
     *  and is the one that acts; the right inlet stores a velocity for the
     *  pitches that arrive *after* it and outputs nothing itself. That
     *  asymmetry is not decoration — it is what makes the object a filter of
     *  notes rather than of numbers, since a note is a pair and only the pitch
     *  half tells you the pair is complete.
     *
     *  A list in the left inlet is Max's inlet distribution written on one
     *  cord: `60 100` stores velocity 100 and then plays pitch 60, exactly as
     *  if the velocity had reached the right inlet first. That is what lets a
     *  single cord from `.midiparse` or a `.pack` work, and it is `.makenote`'s
     *  arrangement (#538) for the same reason.
     *
     *  ### The test is `!= 0`, deliberately not a range check
     *
     *  Max's rule is literal: the velocity is passed "provided it is not 0".
     *  Nothing here clamps a velocity to MIDI's 0-127 or refuses one outside
     *  it, and nothing rejects a negative. This object is a *filter*, and a
     *  filter that quietly rewrote the values passing through it would be
     *  worse than useless in front of the formatters downstream, which are
     *  where range belongs. The only value with meaning here is 0, because 0 is
     *  what the whole MIDI world spells a release with.
     *
     *  ### The order the two outlets fire in
     *
     *  Velocity first, then pitch — Max's outlets firing right to left, and
     *  here it is load-bearing rather than cosmetic. Everything downstream that
     *  takes a pitch/velocity pair (`.makenote`, `.noteon`, `.midiformat`, the
     *  `.x*out` family) takes its pitch on a hot inlet and its velocity on a
     *  cold one, so a pitch that arrived first would be paired with the
     *  *previous* note's velocity.
     *
     *  ### No creation arguments, and nothing held
     *
     *  Max's `stripnote` takes none, and neither does this: the velocity has no
     *  documented initial value other than 0, which means a fresh object passes
     *  nothing until a velocity has reached it — the correct behaviour, since
     *  until then no note-on has been seen either.
     *
     *  There is no `Teardown` (issue #758) and there is deliberately nothing to
     *  put in one. Unlike `.makenote` and `.midiflush`, this object never
     *  causes a note to sound: it forwards attacks it was given and drops
     *  releases, and it holds no record of what is sounding downstream. A patch
     *  that needs its notes released on teardown gets that from the object that
     *  played them.
     *
     *  ### Real-time behaviour
     *
     *  `Calculate()` does nothing — the object is driven entirely by its
     *  inlets, and one that emitted would fire a note on every DSP tick from a
     *  stimulus no patch sent. The whole of the state is one atomic int, and
     *  every message path is a load, a compare and at most two sends: nothing
     *  allocates, takes a lock or blocks, which matters because an in-patcher
     *  dispatch runs on `T_DSP` and so routinely on the audio callback.
     */
    PATCHER_CLASS(mStripNote, YSE::OBJ::M_STRIPNOTE)
    _NO_MESSAGES
    _NO_CALCULATE

    _INT_IN(IntIn)
    _FLOAT_IN(FloatIn)
    _LIST_IN(ListIn)

  public:
    /**
     *  @brief The velocity the next pitch will be paired with — Max's right
     *         inlet, 0 on a fresh object.
     *
     *  Unclamped, and reported exactly as it was given: the only value this
     *  object reads meaning into is 0. Diagnostics and tests; a patch sees the
     *  same thing by sending a pitch.
     */
    int Velocity() const {
      return (int)velocity.load();
    }

  private:
    // The pitch half, which is the half that acts: pass the pair on, or drop it.
    void Play(int pitch, YSE::THREAD thread);

    // Max's right inlet, read on every pitch and written from whichever thread
    // sent one — the audio callback included — so atomic.
    aInt velocity{0};
  };

} // namespace PATCHER
} // namespace YSE
