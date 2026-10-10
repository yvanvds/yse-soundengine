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
      "A pitch passes with the stored velocity, unless that velocity is 0. A list \"pitch "
      "velocity\" sets the velocity first.";

  constexpr char kVelocityInletDoc[] =
      "Velocity for the pitches that follow; sends nothing. Not clamped.";

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

  ADD_DESCRIPTION("Passes note-ons and drops releases: a pitch goes through only when its velocity "
                  "is not 0.");
  ADD_CATEGORY(pCategory::MIDI);

  INLET_DOC(0, "pitch", kPitchInletDoc, "int, float, list");
  INLET_DOC(1, "velocity", kVelocityInletDoc, "0-127");
  OUTLET_DOC(0, "pitch", "The pitch of a note-on. Sent last.", "int");
  OUTLET_DOC(1, "velocity", "The velocity of a note-on; never 0.", "int");
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
