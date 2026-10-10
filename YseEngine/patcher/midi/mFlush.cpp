// `.flush` (issue #540). See mFlush.h for the design; this file is the grammar,
// the held-note set and the flush.
// No platform guard, deliberately — see the header.
#include "mFlush.h"

#include "../math/gExprEval.h"
#include "../pListArgs.h"
#include "../pObjectList.hpp"
#include "../pSelector.h"

#include <cstddef>

using namespace YSE::PATCHER;

#define className mFlush

namespace {

  // The bounds of one whitespace-separated token, found in place: a substr here
  // would allocate on whichever thread the message arrived on, and that is
  // routinely the audio callback. `.stripnote` and `.makenote` read their lists
  // the same way.
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
      "A pitch passes through with the stored velocity. A list \"pitch velocity\" sets the "
      "velocity first. A bang releases every sounding note; \"clear\" forgets them.";

  constexpr char kVelocityInletDoc[] =
      "Velocity for the pitches that follow; sends nothing. 0 means a release. Not clamped.";

} // namespace

CONSTRUCT() {
  ADD_IN_0;
  REG_BANG_IN(Flush);
  REG_INT_IN(IntIn);
  REG_FLOAT_IN(FloatIn);
  REG_LIST_IN(ListIn);

  ADD_IN_1;
  REG_INT_IN(IntIn);
  REG_FLOAT_IN(FloatIn);
  REG_LIST_IN(ListIn);

  // Two int outlets rather than one list: this is the shape everything around a
  // note takes — `.makenote`, `.stripnote`, `.noteon` and `.xnoteout` all have a
  // pitch port and a velocity port — so the pair is wired across rather than
  // packed and unpacked again.
  ADD_OUT_INT;
  ADD_OUT_INT;

  // No creation arguments: Max's flush has none, and a velocity of 0 on a fresh
  // object is the right start — until one has arrived, no note-on has either.

  ADD_DESCRIPTION("Passes pitch/velocity pairs through and remembers which notes are sounding. A "
                  "bang releases them all with velocity 0.");
  ADD_CATEGORY(pCategory::MIDI);

  INLET_DOC(0, "pitch", kPitchInletDoc, "int, float, list, 'bang', 'clear'");
  INLET_DOC(1, "velocity", kVelocityInletDoc, "0-127");
  OUTLET_DOC(0, "pitch", "The pitch, as it passes and again when a bang releases it. Sent last.",
             "int");
  OUTLET_DOC(1, "velocity", "The stored velocity as a pair passes, or 0 for a release.", "int");
}

void mFlush::Emit(int pitch, int noteVelocity, YSE::THREAD thread) {
  // Right to left, and load-bearing: everything downstream that takes a pair
  // takes its velocity on a cold inlet and its pitch on a hot one, so a pitch
  // sent first would be paired with the previous note's velocity.
  outputs[1].SendInt(noteVelocity, thread);
  outputs[0].SendInt(pitch, thread);
}

void mFlush::Mark(int pitch, bool on) {
  // Outside the MIDI note range there is no bit to set. Passed on all the same
  // by the caller — this object does not rewrite what goes through it — but not
  // remembered, since clamping would file the note under a pitch that is not
  // its own and a bang would then release a note nobody played.
  if (pitch < 0 || pitch >= PITCHES) return;

  std::uint32_t& word = held[pitch / 32];
  const std::uint32_t bit = 1u << (pitch % 32);

  if (on) {
    if ((word & bit) == 0) {
      word |= bit;
      heldCount++;
    }
    return;
  }

  if ((word & bit) != 0) {
    word &= ~bit;
    heldCount--;
  }
}

void mFlush::Play(int pitch, YSE::THREAD thread) {
  if (!Enter()) return;

  const int noteVelocity = Velocity();
  // A velocity of 0 is a release — how the whole MIDI world spells one, and how
  // every note source in the patcher reports one. Anything else is a note-on.
  // Marked *before* the pair is passed on, so the object's own idea of what is
  // sounding is settled before anything downstream can act on it.
  Mark(pitch, noteVelocity != 0);
  Emit(pitch, noteVelocity, thread);

  Leave();
}

void mFlush::Release(bool emit, YSE::THREAD thread) {
  if (!Enter()) return;

  for (int word = 0; word < WORDS; word++) {
    const std::uint32_t bits = held[word];
    if (bits == 0) continue;
    // Cleared before anything is sent, so an outlet wired somewhere that reads
    // this object back sees a set already emptied of what is on its way out.
    held[word] = 0;
    for (int bit = 0; bit < 32; bit++) {
      if ((bits & (1u << bit)) == 0) continue;
      heldCount--;
      // Velocity 0: the release, and the only value this object ever invents.
      // `clear` walks the same bits and sends none of them, which is the whole
      // difference between Max's two commands.
      if (emit) Emit((word * 32) + bit, 0, thread);
    }
  }

  Leave();
}

void mFlush::Teardown(YSE::THREAD thread) {
  // The patcher's stop pass (issue #758). A flush and nothing else — the same
  // handler a bang runs, re-entrancy guard included — because the whole point
  // of the pass is that at this moment the patch is still wired and this object
  // is still able to do what a bang would have made it do.
  Release(true, thread);
}

bool mFlush::IsHeld(int pitch) const {
  if (pitch < 0 || pitch >= PITCHES) return false;
  return (held[pitch / 32] & (1u << (pitch % 32))) != 0;
}

bool mFlush::Enter() {
  if (busy.exchange(true, std::memory_order_acq_rel)) {
    // Another thread is mid-message, or a patch has wired an outlet back into
    // the left inlet. Counted rather than spun on: this is a path the audio
    // callback takes, and neither the set nor its count is re-entrant.
    dropped.fetch_add(1, std::memory_order_relaxed);
    return false;
  }
  return true;
}

void mFlush::Leave() {
  busy.store(false, std::memory_order_release);
}

BANG_IN(Flush) {
  (void)inlet;
  Release(true, thread);
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
  // Max converts a float to an int in both inlets. ExprToInt rather than a cast:
  // a value outside the int range is undefined behaviour to cast.
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

  if (inlet != 0) {
    float number = 0.f;
    if (!ReadNumericToken(value.c_str() + begin, length, number)) return;
    velocity = ExprToInt(number);
    return;
  }

  // Max's `clear`, left inlet only and a word rather than a pitch, so it cannot
  // be played. There is no `stop` here: a bang is already what `.makenote`
  // spells `stop`, Max's flush having a bang method where its makenote has none.
  if (length == 5 && value.compare(begin, length, "clear", 5) == 0) {
    Release(false, thread);
    return;
  }

  float pitch = 0.f;
  if (!ReadNumericToken(value.c_str() + begin, length, pitch)) return;

  // Max's list distributes across the inlets right to left, so a second element
  // is the velocity and it is stored *before* the pitch is read against it —
  // exactly as if it had arrived at the right inlet first. That is what makes
  // one cord from `.midiparse`'s note outlet work, and it is the only way the
  // very first note can be tracked correctly. Further elements are ignored.
  std::size_t secondBegin = 0;
  std::size_t secondEnd = 0;
  if (NextToken(value, end, secondBegin, secondEnd)) {
    float second = 0.f;
    if (ReadNumericToken(value.c_str() + secondBegin, secondEnd - secondBegin, second)) {
      velocity = ExprToInt(second);
    }
  }

  Play(ExprToInt(pitch), thread);
}

#undef className
