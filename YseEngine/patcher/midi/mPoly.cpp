// `.poly` (issue #542). See mPoly.h for the design; this file is the grammar,
// the voice table and the allocator.
// No platform guard, deliberately — see the header.
#include "mPoly.h"

#include "../math/gExprEval.h"
#include "../pListArgs.h"
#include "../pObjectList.hpp"
#include "../pSelector.h"

#include <cstddef>

using namespace YSE::PATCHER;

#define className mPoly

namespace {

  // The bounds of one whitespace-separated token, found in place: a substr here
  // would allocate on whichever thread the message arrived on, and that is
  // routinely the audio callback. `.flush`, `.sustain`, `.stripnote` and
  // `.makenote` read their lists the same way.
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
      "A pitch takes a voice, or frees one when the stored velocity is 0. A list \"pitch "
      "velocity\" sets the velocity first. \"stop\" releases every sounding voice.";

  constexpr char kVelocityInletDoc[] =
      "Velocity for the pitches that follow; sends nothing. 0 means a release. Not clamped.";

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

  // Three int outlets rather than one list: the voice number is what routes the
  // note, so it has to be able to reach a `.route` or a `.gate` selector on its
  // own, and the pitch/velocity pair keeps the shape every note object in the
  // patcher takes. The fourth is Max's overflow outlet, and a list because that
  // is the shape Max sends there.
  ADD_OUT_INT;
  ADD_OUT_INT;
  ADD_OUT_INT;
  ADD_OUT_LIST;

  ADD_PARAM(voiceCount);
  ADD_PARAM(steal);
  voiceCount = DEFAULT_VOICES;
  steal = 0;

  // The overflow text, sized for two ints and the space between them, so a send
  // on the audio thread only ever writes into memory this line reserved.
  overflowText.reserve((std::size_t)(2 * FORMAT_INT_WIDTH) + 2);

  ADD_DESCRIPTION(
      "Assigns notes to a numbered pool of voices. Each note-on takes a free voice, and "
      "its release comes out with the same voice number.");
  ADD_CATEGORY(pCategory::MIDI);

  INLET_DOC(0, "pitch", kPitchInletDoc, "int, float, list, 'stop'");
  INLET_DOC(1, "velocity", kVelocityInletDoc, "0-127");
  OUTLET_DOC(0, "voice",
             "The voice number, from 1: the same for a note's attack and its release. Sent last.",
             "1-128");
  OUTLET_DOC(1, "pitch", "The pitch of the note being started or released.", "int");
  OUTLET_DOC(2, "velocity", "The stored velocity for a note-on, 0 for a release.", "int");
  OUTLET_DOC(3, "overflow",
             "\"pitch velocity\" for a note no voice could take, and \"pitch 0\" for a release "
             "whose pitch no voice holds.",
             "pitch velocity");
  PARAM_DOC("voices", "16", "Number of voices. Clamped to 1-128.", "1-128");
  PARAM_DOC("steal", "0",
            "0 sends notes that find no free voice to the overflow outlet. Any other value takes "
            "the voice of the note held longest, releasing that note first.",
            "0 or non-zero");
}

int mPoly::Voices() const {
  const Int count = voiceCount.load();
  if (count < 1) return 1;
  return count > MAX_VOICES ? MAX_VOICES : (int)count;
}

int mPoly::PitchOfVoice(int voice) const {
  if (voice < 1 || voice > MAX_VOICES) return -1;
  const Voice& v = table[voice - 1];
  return v.active ? v.pitch : -1;
}

void mPoly::Emit(int voice, int pitch, int noteVelocity, YSE::THREAD thread) {
  // Right to left, and load-bearing: everything downstream takes the voice
  // number on a hot inlet, that being the value which routes the note, so a
  // voice number sent first would carry the previous note's pair with it.
  outputs[2].SendInt(noteVelocity, thread);
  outputs[1].SendInt(pitch, thread);
  outputs[0].SendInt(voice, thread);
}

void mPoly::Overflow(int pitch, int noteVelocity, YSE::THREAD thread) {
  // Refilled immediately before the send rather than kept between them: the
  // send path is synchronous, so a patch looping this outlet back into an inlet
  // re-enters here inside SendList and a buffer filled any earlier would be the
  // inner message's by the time this one was read. Into memory reserved at
  // construction, so nothing here allocates.
  char digits[FORMAT_INT_WIDTH];
  std::size_t written = WriteInt(pitch, digits);
  overflowText.assign(digits, written);
  overflowText.push_back(' ');
  written = WriteInt(noteVelocity, digits);
  overflowText.append(digits, written);

  outputs[3].SendList(overflowText, thread);
}

void mPoly::NoteOn(int pitch, int noteVelocity, YSE::THREAD thread) {
  const int count = Voices();

  // 1. A free voice, scanned in ascending order — the synth's first step, and
  //    what makes a patch with spare voices always play on the low ones.
  for (int i = 0; i < count; i++) {
    if (table[i].active) continue;
    table[i].pitch = pitch;
    table[i].age = nextAge++;
    table[i].active = true;
    heldCount++;
    Emit(i + 1, pitch, noteVelocity, thread);
    return;
  }

  // 2. No free voice. Max's second creation argument decides which of the two
  //    things happens next, and with it at 0 the note simply does not play.
  if (!Steals()) {
    Overflow(pitch, noteVelocity, thread);
    return;
  }

  // 3. Steal the oldest sounding note — the synth's rule and Max's. Ages are
  //    never reused, so the lowest is the one held the longest.
  int victim = -1;
  std::uint64_t bestAge = UINT64_MAX;
  for (int i = 0; i < count; i++) {
    if (table[i].active && table[i].age < bestAge) {
      bestAge = table[i].age;
      victim = i;
    }
  }
  if (victim < 0) {
    // Unreachable: step 1 found no free voice below `count`, so every one of
    // them is active. Answered rather than asserted, an assert on the audio
    // thread being worse than an overflow message.
    Overflow(pitch, noteVelocity, thread);
    return;
  }

  const int stolen = table[victim].pitch;
  // Freed before its release is sent, so a patch reading this object back from
  // inside that send sees a voice already given up — `.flush`'s rule for the
  // same reason.
  table[victim].active = false;
  heldCount--;
  // The stolen note lets go *before* the new one plays, on the same voice
  // number. Without this the chain downstream would hold two notes at once and
  // have no way of ever releasing the first.
  Emit(victim + 1, stolen, 0, thread);

  table[victim].pitch = pitch;
  table[victim].age = nextAge++;
  table[victim].active = true;
  heldCount++;
  Emit(victim + 1, pitch, noteVelocity, thread);
}

void mPoly::NoteOff(int pitch, YSE::THREAD thread) {
  // The whole table, not just the first `count` voices: a pool shrunk under a
  // sounding patch leaves notes above the new bound, and they are still owed
  // their releases. Oldest first, so a pitch played twice is released in the
  // order it was played and a balanced source stays balanced.
  int found = -1;
  std::uint64_t bestAge = UINT64_MAX;
  for (int i = 0; i < MAX_VOICES; i++) {
    if (table[i].active && table[i].pitch == pitch && table[i].age < bestAge) {
      bestAge = table[i].age;
      found = i;
    }
  }

  if (found < 0) {
    // No voice is playing it: either it overflowed, or it was stolen, or nobody
    // ever played it. Out the overflow outlet so the release follows wherever
    // the attack went — see the header.
    Overflow(pitch, 0, thread);
    return;
  }

  table[found].active = false;
  heldCount--;
  Emit(found + 1, pitch, 0, thread);
}

void mPoly::Play(int pitch, YSE::THREAD thread) {
  if (!Enter()) return;

  // A velocity of 0 is a release — how the whole MIDI world spells one, and how
  // every note source in the patcher reports one. Anything else is a note-on.
  const int noteVelocity = Velocity();
  if (noteVelocity == 0) {
    NoteOff(pitch, thread);
  } else {
    NoteOn(pitch, noteVelocity, thread);
  }

  Leave();
}

void mPoly::Release(YSE::THREAD thread) {
  if (!Enter()) return;

  // Every voice is freed first and only then released, in one bounded pass over
  // a stack array: a send runs the whole subgraph behind the outlets, which may
  // come back into this object, and a walk that emitted as it went could meet a
  // voice allocated by its own output.
  int taken[MAX_VOICES];
  int pitches[MAX_VOICES];
  int count = 0;

  for (int i = 0; i < MAX_VOICES; i++) {
    if (!table[i].active) continue;
    table[i].active = false;
    heldCount--;
    taken[count] = i;
    pitches[count] = table[i].pitch;
    count++;
  }

  // Ascending voice order — the order the table walks in, and deterministic
  // rather than the accident of arrival.
  for (int n = 0; n < count; n++)
    Emit(taken[n] + 1, pitches[n], 0, thread);

  Leave();
}

void mPoly::Teardown(YSE::THREAD thread) {
  // The patcher's stop pass (issue #758). A `stop` and nothing else — the same
  // handler the message runs, re-entrancy guard included — because the whole
  // point of the pass is that at this moment the patch is still wired and this
  // object is still able to tell each voice chain which note to let go of. It
  // is the only thing that knows.
  Release(thread);
}

bool mPoly::Enter() {
  if (busy.exchange(true, std::memory_order_acq_rel)) {
    // Another thread is mid-message, or a patch has wired an outlet back into
    // the left inlet. Counted rather than spun on: this is a path the audio
    // callback takes, and neither the table nor its count is re-entrant.
    dropped.fetch_add(1, std::memory_order_relaxed);
    return false;
  }
  return true;
}

void mPoly::Leave() {
  busy.store(false, std::memory_order_release);
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

  // Max's only command, left inlet only and a word rather than a pitch, so it
  // cannot be played. There is no `clear` here and no bang method: Max's poly
  // has neither.
  if (length == 4 && value.compare(begin, length, "stop", 4) == 0) {
    Release(thread);
    return;
  }

  float pitch = 0.f;
  if (!ReadNumericToken(value.c_str() + begin, length, pitch)) return;

  // Max's list distributes across the inlets right to left, so a second element
  // is the velocity and it is stored *before* the pitch is read against it —
  // exactly as if it had arrived at the right inlet first. That is what makes
  // one cord from `.midiparse`'s note outlet work, and it is the only way the
  // very first note can be allocated correctly. Further elements are ignored.
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
