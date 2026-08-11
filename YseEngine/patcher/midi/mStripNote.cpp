// `.stripnote` (issue #539). See mStripNote.h for the design; this file is the
// grammar and the one comparison the object exists for.
// No platform guard, deliberately — see the header.
#include "mStripNote.h"

#include "../math/gExprEval.h"
#include "../pListArgs.h"
#include "../pObjectList.hpp"
#include "../pSelector.h"

#include <cstddef>

using namespace YSE::PATCHER;

#define className mStripNote

namespace {

  // The bounds of one whitespace-separated token, found in place: a substr here
  // would allocate on whichever thread the message arrived on, and that is
  // routinely the audio callback. `.makenote` reads its list the same way.
  bool NextToken(const std::string& text, std::size_t from, std::size_t& begin, std::size_t& end) {
    begin = from;
    while (begin < text.size() && IsSelectorSeparator(text[begin]))
      begin++;
    end = begin;
    while (end < text.size() && !IsSelectorSeparator(text[end]))
      end++;
    return end > begin;
  }

  constexpr char kPitchInletDoc[] =
      "The pitch, and the half of the pair that acts. An int or a float here is sent straight out "
      "the left outlet with the stored velocity out the right one — but only if that velocity is "
      "not 0, which is Max's 'only pass note-on messages: those having any velocity above 0'. A "
      "velocity of 0 is a release, and a release sends nothing at all. A float pitch is truncated "
      "to an int, as it is in Max. A list is Max's inlet distribution written on one cord: the "
      "first element is the pitch and a second element sets the velocity first, so '60 100' passes "
      "pitch 60 at velocity 100 and '60 0' passes nothing — which is exactly the shape "
      "'.midiparse''s note outlet sends, so the standard patch is a single cord. Further elements "
      "are ignored. A single-token numeric list is the number it spells, so a '.m 60' reaches this "
      "inlet as a pitch. There is no bang method, as Max's stripnote has none.";

  constexpr char kVelocityInletDoc[] =
      "Sets the velocity that pitches arriving *after* it will be paired with — Max's right inlet. "
      "It outputs nothing by itself: only a pitch completes a note, so only a pitch can pass one "
      "on. Ints, floats and a list whose leading token is a number all set it; a float is "
      "truncated. The value is stored and passed on exactly as given, unclamped and never refused "
      "for being outside MIDI's 0-127 — this object filters notes, it does not rewrite them, and 0 "
      "is the only value it reads meaning into.";

} // namespace

CONSTRUCT() {
  ADD_IN_0;
  REG_INT_IN(IntIn);
  REG_FLOAT_IN(FloatIn);
  REG_LIST_IN(ListIn);

  ADD_IN_1;
  REG_INT_IN(IntIn);
  REG_FLOAT_IN(FloatIn);
  REG_LIST_IN(ListIn);

  // Two int outlets rather than one list: this is the shape everything
  // downstream of a note takes — `.makenote`, `.noteon` and `.xnoteout` all
  // have a pitch inlet and a velocity inlet — so the pair is wired across
  // rather than packed and unpacked again.
  ADD_OUT_INT;
  ADD_OUT_INT;

  // No creation arguments: Max's stripnote has none, and a velocity of 0 on a
  // fresh object is the right start — until one has arrived, no note-on has
  // either.

  ADD_DESCRIPTION(
      "Passes note-ons and drops the releases — Max's 'stripnote', 'only pass note-on messages: "
      "those having any velocity above 0'. A pitch in the left inlet goes out the left outlet with "
      "the stored velocity out the right one, unless that velocity is 0, in which case nothing is "
      "sent at all. This is the standard first object after a note source when a patch only cares "
      "about attacks — triggering a sample, advancing a sequence, firing anything one-shot. Every "
      "note source in the patcher reports a release as a pitch with velocity 0, '.notein' and "
      "'.midiparse' both folding a note-off message and a zero-velocity note-on into that one "
      "shape, so a patch wired straight through fires twice per key: once on the way down and once "
      "on the way up. The second one is always a bug, and it looks like a doubled sample or a "
      "sequence at double speed rather than like a MIDI problem. The right inlet stores the "
      "velocity for pitches arriving after it and outputs nothing itself, because a note is a pair "
      "and only the pitch half says the pair is complete. A list in the left inlet is Max's inlet "
      "distribution on one cord — '60 100' is pitch 60 at velocity 100 — which is exactly the "
      "shape '.midiparse''s note outlet sends, so '.midiparse' into '.stripnote' is a single cord. "
      "The test is literally 'not 0': nothing is clamped to MIDI's 0-127 and nothing outside it is "
      "refused, since this object filters notes rather than rewriting them and range belongs to "
      "the formatters downstream. The velocity goes out the right outlet before the pitch goes out "
      "the left, Max's outlets firing right to left, which here is not cosmetic — everything "
      "downstream that takes a pair ('.makenote', '.noteon', '.midiformat', the '.x*out' family) "
      "takes pitch on a hot inlet and velocity on a cold one, so a pitch sent first would carry "
      "the previous note's velocity. There are no creation arguments, as in Max, and nothing is "
      "held: unlike '.makenote' and '.midiflush' this object never causes a note to sound, so it "
      "has nothing to release when the patcher goes away. Calculate() does nothing and no message "
      "path allocates, locks or blocks.");
  ADD_CATEGORY(pCategory::MIDI);

  INLET_DOC(0, "pitch", kPitchInletDoc, "int, float, list");
  INLET_DOC(1, "velocity", kVelocityInletDoc, "0-127");
  OUTLET_DOC(0, "pitch",
             "The pitch of a note-on, unaltered. Nothing is sent here for a release — a pitch "
             "whose velocity is 0 — which is the whole object. Sent after the velocity, Max's "
             "outlets firing right to left.",
             "int");
  OUTLET_DOC(1, "velocity",
             "The velocity of a note-on, unaltered and never 0: a 0 is what stops the pair from "
             "being sent at all. Sent before the pitch, so anything downstream has it in hand by "
             "the time the pitch reaches its hot inlet.",
             "int");
}

void mStripNote::Play(int pitch, YSE::THREAD thread) {
  const int noteVelocity = Velocity();
  // Max's rule, and the entire object: "provided the velocity is not 0". A
  // release — a note-off, or the zero-velocity note-on every source folds one
  // into — stops here.
  if (noteVelocity == 0) return;

  // Right to left, and load-bearing: everything downstream that takes a pair
  // takes its velocity on a cold inlet and its pitch on a hot one, so a pitch
  // sent first would be paired with the previous note's velocity.
  outputs[1].SendInt(noteVelocity, thread);
  outputs[0].SendInt(pitch, thread);
}

INT_IN(IntIn) {
  if (inlet == 0) {
    Play(value, thread);
    return;
  }
  // Max's right inlet: the velocity for pitches arriving after it. Stored as
  // given — see the header on why nothing is clamped here.
  velocity = value;
}

FLOAT_IN(FloatIn) {
  // Max converts a float to an int in both inlets. ExprToInt rather than a
  // cast: a value outside the int range is undefined behaviour to cast.
  if (inlet == 0) {
    Play(ExprToInt(value), thread);
    return;
  }
  velocity = ExprToInt(value);
}

LIST_IN(ListIn) {
  std::size_t begin = 0;
  std::size_t end = 0;
  if (!NextToken(value, 0, begin, end)) return;
  const std::size_t length = end - begin;

  float number = 0.f;
  if (!ReadNumericToken(value.c_str() + begin, length, number)) return;

  if (inlet != 0) {
    velocity = ExprToInt(number);
    return;
  }

  // Max's list distributes across the inlets right to left, so a second element
  // is the velocity and it is stored *before* the pitch is tested against it —
  // exactly as if it had arrived at the right inlet first. That is what makes
  // one cord from `.midiparse`'s note outlet work, and it is the only way the
  // very first note can be filtered correctly. Further elements are ignored.
  std::size_t secondBegin = 0;
  std::size_t secondEnd = 0;
  if (NextToken(value, end, secondBegin, secondEnd)) {
    float second = 0.f;
    if (ReadNumericToken(value.c_str() + secondBegin, secondEnd - secondBegin, second)) {
      velocity = ExprToInt(second);
    }
  }

  Play(ExprToInt(number), thread);
}

#undef className
