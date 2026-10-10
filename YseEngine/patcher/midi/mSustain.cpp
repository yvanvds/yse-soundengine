// `.sustain` (issue #541). See mSustain.h for the design; this file is the
// grammar, the held-release set and the pedal.
// No platform guard, deliberately — see the header.
#include "mSustain.h"

#include "../math/gExprEval.h"
#include "../pListArgs.h"
#include "../pObjectList.hpp"
#include "../pSelector.h"

#include <cstddef>

using namespace YSE::PATCHER;

#define className mSustain

namespace {

  // The bounds of one whitespace-separated token, found in place: a substr here
  // would allocate on whichever thread the message arrived on, and that is
  // routinely the audio callback. `.flush`, `.stripnote` and `.makenote` read
  // their lists the same way.
  bool NextToken(const std::string& text, std::size_t from, std::size_t& begin, std::size_t& end) {
    begin = from;
    while (begin < text.size() && IsSelectorSeparator(text[begin]))
      begin++;
    end = begin;
    while (end < text.size() && !IsSelectorSeparator(text[end]))
      end++;
    return end > begin;
  }

  // A token compared against a command word, without building a string to do it.
  bool TokenIs(const std::string& text, std::size_t begin, std::size_t length, const char* word,
               std::size_t wordLength) {
    return length == wordLength && text.compare(begin, length, word, wordLength) == 0;
  }

  constexpr char kPitchInletDoc[] =
      "A pitch passes with the stored velocity; a release is held back while the pedal is down. "
      "Also takes \"pitch velocity\", \"sustain 0/1\", \"flush\" and \"clear\".";

  constexpr char kVelocityInletDoc[] =
      "Velocity for the pitches that follow; sends nothing. 0 means a release. Not clamped.";

  constexpr char kPedalInletDoc[] =
      "Non-zero presses the pedal. 0 lifts it and sends every held release. The pedal starts up.";

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

  ADD_IN_2;
  REG_INT_IN(IntIn);
  REG_FLOAT_IN(FloatIn);
  REG_LIST_IN(ListIn);

  // Two int outlets rather than one list: this is the shape everything around a
  // note takes — `.makenote`, `.stripnote`, `.flush`, `.noteon` and `.xnoteout`
  // all have a pitch port and a velocity port — so the pair is wired across
  // rather than packed and unpacked again.
  ADD_OUT_INT;
  ADD_OUT_INT;

  // No creation arguments: Max's sustain has none, and a pedal that starts up
  // with a velocity of 0 is the right start — until one has arrived, no note-on
  // has either.

  ADD_DESCRIPTION("A sustain pedal for pitch/velocity pairs. While the pedal is down, releases are "
                  "held back; lifting it sends them all.");
  ADD_CATEGORY(pCategory::MIDI);

  INLET_DOC(0, "pitch", kPitchInletDoc, "int, float, list, 'sustain 0/1', 'flush', 'clear'");
  INLET_DOC(1, "velocity", kVelocityInletDoc, "0-127");
  INLET_DOC(2, "pedal", kPedalInletDoc, "0 (up) or non-zero (down)");
  OUTLET_DOC(0, "pitch", "The pitch, as a pair passes or as a held release is sent. Sent last.",
             "int");
  OUTLET_DOC(1, "velocity", "The stored velocity as a pair passes, or 0 for a release.", "int");
}

void mSustain::Emit(int pitch, int noteVelocity, YSE::THREAD thread) {
  // Right to left, and load-bearing: everything downstream that takes a pair
  // takes its velocity on a cold inlet and its pitch on a hot one, so a pitch
  // sent first would be paired with the previous note's velocity.
  outputs[1].SendInt(noteVelocity, thread);
  outputs[0].SendInt(pitch, thread);
}

void mSustain::Mark(int pitch, bool on) {
  // Outside the MIDI note range there is no bit to set. The caller sends such a
  // release on immediately rather than holding it — see the class notes on why
  // that is the safe half of the choice.
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

void mSustain::Play(int pitch, YSE::THREAD thread) {
  if (!Enter()) return;

  const int noteVelocity = Velocity();
  // A velocity of 0 is a release — how the whole MIDI world spells one, and how
  // every note source in the patcher reports one. Anything else is an attack,
  // and an attack is never delayed: a pedal holds notes on, it does not hold
  // them off. A pitch the bitmap cannot hold is passed on for the same reason a
  // note-on is — a swallowed release nothing can remember is a hanging note.
  const bool trackable = pitch >= 0 && pitch < PITCHES;
  if (noteVelocity != 0 || !Pedal() || !trackable) {
    Emit(pitch, noteVelocity, thread);
    Leave();
    return;
  }

  // Held back. Max's repeat mode 0: a second release for a pitch already held is
  // still one release owed, and an attack that arrived in between left this bit
  // alone.
  Mark(pitch, true);
  Leave();
}

void mSustain::SetPedal(bool down, YSE::THREAD thread) {
  // Stored first and unconditionally, outside the guard: a lift that failed to
  // register would leave this object's idea of the pedal contradicting the
  // player's foot for as long as the patch ran. See the class notes.
  pedal.store(down, std::memory_order_relaxed);
  // Pressing sends nothing — there is nothing yet to send. Lifting is the whole
  // event.
  if (!down) Release(true, thread);
}

void mSustain::Release(bool emit, YSE::THREAD thread) {
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
      // Velocity 0: the release this object swallowed, sent at last. `clear`
      // walks the same bits and sends none of them, which is the whole
      // difference between Max's two commands.
      if (emit) Emit((word * 32) + bit, 0, thread);
    }
  }

  Leave();
}

void mSustain::Teardown(YSE::THREAD thread) {
  // The patcher's stop pass (issue #758). A flush and nothing else — the same
  // handler the `flush` message runs, re-entrancy guard included — because the
  // whole point of the pass is that at this moment the patch is still wired and
  // this object is still able to send the note-offs it swallowed. The pedal is
  // left where it is: the object is about to stop existing, and a patch that
  // reads it back in the same pass should not see a foot lift nobody made.
  Release(true, thread);
}

bool mSustain::IsHeld(int pitch) const {
  if (pitch < 0 || pitch >= PITCHES) return false;
  return (held[pitch / 32] & (1u << (pitch % 32))) != 0;
}

bool mSustain::Enter() {
  if (busy.exchange(true, std::memory_order_acq_rel)) {
    // Another thread is mid-message, or a patch has wired an outlet back into
    // the left inlet. Counted rather than spun on: this is a path the audio
    // callback takes, and neither the set nor its count is re-entrant.
    dropped.fetch_add(1, std::memory_order_relaxed);
    return false;
  }
  return true;
}

void mSustain::Leave() {
  busy.store(false, std::memory_order_release);
}

INT_IN(IntIn) {
  if (inlet == 0) {
    Play(value, thread);
    return;
  }
  if (inlet == 1) {
    // Max's middle inlet: the velocity for pitches arriving after it. Stored as
    // given — see the header on why nothing is clamped here.
    velocity = value;
    return;
  }
  SetPedal(value != 0, thread);
}

FLOAT_IN(FloatIn) {
  // Max converts a float to an int in every inlet. ExprToInt rather than a cast:
  // a value outside the int range is undefined behaviour to cast.
  if (inlet == 0) {
    Play(ExprToInt(value), thread);
    return;
  }
  if (inlet == 1) {
    velocity = ExprToInt(value);
    return;
  }
  SetPedal(ExprToInt(value) != 0, thread);
}

LIST_IN(ListIn) {
  std::size_t begin = 0;
  std::size_t end = 0;
  if (!NextToken(value, 0, begin, end)) return;
  const std::size_t length = end - begin;

  if (inlet != 0) {
    float number = 0.f;
    if (!ReadNumericToken(value.c_str() + begin, length, number)) return;
    if (inlet == 1) {
      velocity = ExprToInt(number);
      return;
    }
    SetPedal(ExprToInt(number) != 0, thread);
    return;
  }

  // Max's three commands, left inlet only and words rather than pitches, so none
  // of them can be played by accident.
  if (TokenIs(value, begin, length, "clear", 5)) {
    Release(false, thread);
    return;
  }
  if (TokenIs(value, begin, length, "flush", 5)) {
    Release(true, thread);
    return;
  }

  std::size_t secondBegin = 0;
  std::size_t secondEnd = 0;
  const bool hasSecond = NextToken(value, end, secondBegin, secondEnd);

  if (TokenIs(value, begin, length, "sustain", 7)) {
    // Max's attribute, reached by message: `sustain 1` presses, `sustain 0`
    // lifts. Bare `sustain` names a state without giving one, so it is ignored
    // rather than guessed at — pressing and lifting are opposite mistakes and
    // neither is the safe one.
    float state = 0.f;
    if (!hasSecond) return;
    if (!ReadNumericToken(value.c_str() + secondBegin, secondEnd - secondBegin, state)) return;
    SetPedal(ExprToInt(state) != 0, thread);
    return;
  }

  float pitch = 0.f;
  if (!ReadNumericToken(value.c_str() + begin, length, pitch)) return;

  // Max's list distributes across the inlets right to left, so a second element
  // is the velocity and it is stored *before* the pitch is read against it —
  // exactly as if it had arrived at the middle inlet first. That is what makes
  // one cord from `.midiparse`'s note outlet work, and it is the only way the
  // very first note can be handled correctly. Further elements are ignored.
  if (hasSecond) {
    float second = 0.f;
    if (ReadNumericToken(value.c_str() + secondBegin, secondEnd - secondBegin, second)) {
      velocity = ExprToInt(second);
    }
  }

  Play(ExprToInt(pitch), thread);
}

#undef className
