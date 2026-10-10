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
      "The pitch, and the half of the pair that acts. An int or a float here is paired with the "
      "stored velocity, and what happens next is the pedal: a note-on — any velocity but 0 — "
      "always "
      "goes straight out, because the pedal delays releases and never attacks; a note-off — "
      "velocity 0, which is how the whole MIDI world spells one — goes straight out with the pedal "
      "up and is held back with the pedal down, to be sent when the pedal lifts. A float pitch is "
      "truncated to an int, as it is in Max. A list is Max's inlet distribution written on one "
      "cord: the first element is the pitch and a second element sets the velocity first, so "
      "'60 100' plays pitch 60 at velocity 100 and '60 0' releases it — which is exactly the shape "
      "'.midiparse''s note outlet sends. Further elements are ignored. A single-token numeric list "
      "is the number it spells, so a '.m 60' reaches this inlet as a pitch. Three words are "
      "commands rather than pitches: 'sustain 1' and 'sustain 0' press and lift the pedal exactly "
      "as the right inlet does, 'flush' sends every held note-off now and leaves the pedal where "
      "it "
      "is, and 'clear' forgets them without sending anything. There is no bang method, Max's "
      "'sustain' having none. A pitch outside 0-127 passes through unaltered and is never held, "
      "even with the pedal down, the set being a fixed bitmap over the MIDI note range — a release "
      "it could not remember would never be sent at all.";

  constexpr char kVelocityInletDoc[] =
      "Sets the velocity that pitches arriving *after* it will be paired with — Max's middle "
      "inlet. "
      "It outputs nothing by itself: only a pitch completes a note, so only a pitch can pass one "
      "on "
      "or have its release held. Ints, floats and a list whose leading token is a number all set "
      "it; a float is truncated. The value is stored and passed on exactly as given, unclamped and "
      "never refused for being outside MIDI's 0-127 — this object holds releases, it does not "
      "rewrite notes, and 0 is the only value it reads meaning into, that being what a release is "
      "spelled with.";

  constexpr char kPedalInletDoc[] =
      "The pedal — Max's right inlet, and the same thing the message 'sustain 1' / 'sustain 0' "
      "does. Any non-zero value presses it, which stores a bit and sends nothing: from then on "
      "note-offs arriving at the left inlet are held back instead of passed on. A 0 lifts it, and "
      "that is what sends — every held note-off goes out, in ascending pitch order, and the set is "
      "emptied. Ints, floats and a list whose leading token is a number all work; a float is "
      "truncated first, so any value in (-1, 1) is a lift. Pressing a pedal that is already down, "
      "or lifting one that is already up, does what it says: the second lift has nothing left to "
      "send. The pedal starts up, so an object nobody has touched passes everything through.";

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

  ADD_DESCRIPTION(
      "Holds note-off messages while the sustain pedal is down — Max's 'sustain', which 'holds "
      "note-off messages for release'. A pitch in the left inlet is paired with the stored "
      "velocity, and the pedal decides what happens: a note-on (any velocity but 0) always passes "
      "straight through, because a pedal delays releases and never attacks; a note-off (velocity "
      "0, "
      "which is how the whole MIDI world spells one) passes through with the pedal up and is held "
      "back with the pedal down. Lifting the pedal — a 0 in the right inlet, or the message "
      "'sustain 0' — sends every held note-off, in ascending pitch order, and empties the set. "
      "That "
      "is the pedal a keyboard has: keys come up under the player's fingers and the notes keep "
      "sounding until the foot does. It agrees exactly with the engine's own synth, whose "
      "handleSustain marks a voice held instead of releasing it and drops that claim when the "
      "pedal "
      "lifts; this object is for the patch that is not driving the built-in synth — a '.midiout' "
      "to "
      "a hardware rack, or a voice assembled out of patcher objects — since where the built-in "
      "synth is being driven its own pedal is already doing this. It is not a second '.flush' and "
      "the two sets hold opposite things: '.flush' remembers the notes that are sounding, filled "
      "by "
      "note-ons, and releases them on a bang; this one remembers the releases it swallowed, filled "
      "by note-offs, and sends them when the pedal lifts. They compose in that order — a "
      "'.sustain' "
      "in front of a '.flush' leaves the '.flush' holding what is really sounding, pedal included. "
      "The middle inlet stores the velocity for pitches arriving after it and outputs nothing "
      "itself, and a list in the left inlet is Max's inlet distribution on one cord — '60 100' "
      "plays pitch 60 at velocity 100 — which is the shape '.midiparse''s note outlet sends. The "
      "velocity goes out the right outlet before the pitch goes out the left, Max's outlets firing "
      "right to left, which here is not cosmetic: everything downstream that takes a pair takes "
      "pitch on a hot inlet and velocity on a cold one, so a pitch sent first would carry the "
      "previous note's velocity. Max's three commands are words in the left inlet: 'sustain 1' / "
      "'sustain 0' press and lift the pedal, 'flush' sends every held note-off now and leaves the "
      "pedal where it is, and 'clear' forgets them without sending anything. There is no bang "
      "method, Max's sustain having none. Max's 'repeatmode' is not implemented: this object is "
      "Max's default mode 0, so a pitch played again while its release is held keeps that one held "
      "release and the new attack goes out on its own. A pitch outside 0-127 passes through "
      "unaltered and is never held even with the pedal down, the set being a fixed bitmap of 128 "
      "bits allocated with the object — a release it could not remember would never be sent by "
      "anything, and a note released early is merely a short one. Nothing on the pass-through, "
      "pedal-lift or clear paths allocates, locks or blocks on the audio thread, and a lift is "
      "bounded by the bitmap rather than by how many keys the player let go of. Like '.flush' it "
      "opens no device and needs none, so it works on every platform. It also flushes by itself "
      "when the patcher is cleared or destroyed, and when the object is deleted on its own (issue "
      "#758): the patcher stops every object before it unwires any of them, so a patch torn down "
      "mid-pedal cannot leave the rack sounding with the note-offs this object swallowed. Nothing "
      "can be promised for a process killed outright, and Calculate() does nothing.");
  ADD_CATEGORY(pCategory::MIDI);

  INLET_DOC(0, "pitch", kPitchInletDoc, "int, float, list, 'sustain 0/1', 'flush', 'clear'");
  INLET_DOC(1, "velocity", kVelocityInletDoc, "0-127");
  INLET_DOC(2, "pedal", kPedalInletDoc, "0 (up) or non-zero (down)");
  OUTLET_DOC(0, "pitch",
             "The pitch, sent once as the pair passes through and — for a release the pedal held "
             "back — once when the pedal lifts. The same value both times, which is what lets a "
             "release find the note it belongs to. Sent after the velocity, Max's outlets firing "
             "right to left.",
             "int");
  OUTLET_DOC(1, "velocity",
             "The velocity: the stored one as a pair passes through, and 0 for every release the "
             "pedal sends when it lifts. Sent before the pitch so anything downstream has it in "
             "hand by the time the pitch reaches its hot inlet.",
             "int");
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
