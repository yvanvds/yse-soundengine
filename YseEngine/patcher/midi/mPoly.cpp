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
      "The pitch, and the half of the pair that acts. An int or a float here is paired with the "
      "stored velocity and allocated: a non-zero velocity takes a voice — the lowest-numbered free "
      "one — and sends that voice's number with the pitch and velocity, while a velocity of 0 "
      "releases the voice holding that pitch and sends the same number back with velocity 0, which "
      "is how the whole MIDI world spells a release. A float pitch is truncated to an int, as it "
      "is "
      "in Max. A list is Max's inlet distribution written on one cord: the first element is the "
      "pitch and a second element sets the velocity first, so '60 100' plays pitch 60 at velocity "
      "100 and '60 0' releases it — which is exactly the shape '.midiparse''s note outlet sends. "
      "Further elements are ignored. A single-token numeric list is the number it spells, so a "
      "'.m 60' reaches this inlet as a pitch. The message 'stop' releases every sounding voice "
      "immediately, in ascending voice order — Max's only command, and a word rather than a pitch. "
      "There is no bang method and no 'clear', Max's poly having neither. Any pitch at all can be "
      "allocated, this object holding a table of voices rather than a bitmap over the MIDI note "
      "range.";

  constexpr char kVelocityInletDoc[] =
      "Sets the velocity that pitches arriving *after* it will be paired with — Max's right inlet. "
      "It outputs nothing by itself: only a pitch completes a note, so only a pitch can take a "
      "voice or free one. Ints, floats and a list whose leading token is a number all set it; a "
      "float is truncated. The value is stored and passed on exactly as given, unclamped and never "
      "refused for being outside MIDI's 0-127 — this object allocates notes, it does not rewrite "
      "them, and 0 is the only value it reads meaning into, that being what a release is spelled "
      "with.";

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
      "Allocates incoming notes to a numbered pool of voices — Max's 'poly', which 'provides "
      "polyphonic voice-allocation by allocating data to different individual voices'. A "
      "pitch/velocity pair goes in the two inlets and a voice number, a pitch and a velocity come "
      "out the three outlets: a note-on takes a free voice and holds it, and the release that "
      "frees "
      "it comes out carrying the same voice number, which is the whole point — it is what lets a "
      "patch send a note-off to the one voice that is playing the note. This is what lets a "
      "patcher "
      "build its own polyphonic instrument out of graph objects the way the engine's synth does "
      "internally: a '.route' or a '.gate' on the voice-number outlet fans one keyboard out across "
      "N identical voice chains, and this object decides which chain each note lands in. It hosts "
      "nothing and renders nothing — it allocates numbers, and the patch decides what a number "
      "means. The policy is the engine synth's allocateInGroup rather than a second, subtly "
      "different one: a free voice is preferred and scanned in ascending order, so a patch with "
      "more voices than it uses always plays on the low ones; with no free voice the oldest "
      "sounding note gives way, which is the synth's rule once its release-tail tier is dropped "
      "(this object cannot see a tail — the tail belongs to whatever the patch built downstream) "
      "and is Max's rule word for word. A steal sends the stolen note's release before the new "
      "note's attack on the same voice number, so the chain downstream is told to let go before it "
      "is told to play again. Stealing is Max's second creation argument and is off by default: "
      "with it off, a note that finds no free voice goes out the overflow outlet as the list "
      "'pitch "
      "velocity' and is not tracked. A note-off whose pitch no voice is holding goes out there "
      "too, "
      "as 'pitch 0', because in overflow mode the attacks that were refused went out there and "
      "their releases have to follow them or a patch reading that outlet would hang every note it "
      "was sent. Unlike Max the overflow outlet always exists — Max grows it only when not "
      "stealing, and ports here are built before the creation arguments are parsed — so while "
      "stealing it simply never fires. The right inlet stores the velocity for pitches arriving "
      "after it and outputs nothing itself, and a list in the left inlet is Max's inlet "
      "distribution on one cord: '60 100' plays pitch 60 at velocity 100. The outlets fire right "
      "to "
      "left — velocity, then pitch, then the voice number last — which here is not cosmetic: "
      "everything downstream takes the voice number on its hot inlet, that being the value which "
      "routes the note, so a voice number sent first would carry the previous note's pitch and "
      "velocity with it. Two note-ons for the same pitch are two notes and take two voices, Max's "
      "default repeat mode and the only answer that keeps a voice pool a voice pool; their "
      "note-offs pair oldest-first, so a balanced source stays balanced. 'stop' releases every "
      "sounding voice immediately, in ascending voice order — Max's only message; there is no "
      "'clear' and no bang method, poly having neither. Any pitch at all can be allocated, the "
      "pool "
      "being a table of voices rather than a bitmap over the MIDI note range, so a patch driving a "
      "non-MIDI synth through wider values is tracked exactly like any other. At most 128 voices "
      "may be asked for, all of the table allocated with the object, and nothing on the allocate, "
      "release, steal or stop paths allocates, locks or blocks on the audio thread — every one of "
      "them is a bounded walk of that table. Shrinking the pool under a sounding patch strands no "
      "note: only allocation is bounded by the voice count, while a release and a stop walk the "
      "whole table. Like '.flush' and '.sustain' it opens no device and needs none, so it works on "
      "every platform. It also stops by itself when the patcher is cleared or destroyed, and when "
      "the object is deleted on its own (issue #758): the patcher stops every object before it "
      "unwires any of them, so a patch torn down mid-chord cannot leave its voice chains holding "
      "notes that only this object knew about. Nothing can be promised for a process killed "
      "outright, and Calculate() does nothing.");
  ADD_CATEGORY(pCategory::MIDI);

  INLET_DOC(0, "pitch", kPitchInletDoc, "int, float, list, 'stop'");
  INLET_DOC(1, "velocity", kVelocityInletDoc, "0-127");
  OUTLET_DOC(0, "voice",
             "The voice the note was allocated to, numbered from 1. The same number comes out "
             "again when that note is released, and again when the voice is stolen, which is what "
             "lets a patch route a note-off to the voice that is playing the note. Sent last, "
             "after the pitch and the velocity, Max's outlets firing right to left — this is the "
             "value that routes, so everything downstream takes it on a hot inlet.",
             "1-128");
  OUTLET_DOC(1, "pitch",
             "The pitch of the note being started, released or stolen. The same value on the way "
             "in and on the way out, which is what lets a release find the note it belongs to. "
             "Sent after the velocity and before the voice number.",
             "int");
  OUTLET_DOC(2, "velocity",
             "The velocity: the stored one when a note is allocated, and 0 for every release — the "
             "one a note-off asks for, the one a steal sends for the note it displaces, and the "
             "ones 'stop' and the teardown pass send. Sent first, so anything downstream has the "
             "pair in hand by the time the voice number reaches its hot inlet.",
             "int");
  OUTLET_DOC(3, "overflow",
             "Notes no voice could be found for, as the list 'pitch velocity' — Max's fourth "
             "outlet. It fires when the pool is full and stealing is off, and it also carries a "
             "note-off whose pitch no voice is holding, as 'pitch 0', so the releases of "
             "overflowed notes follow them out the same outlet instead of vanishing. Silent while "
             "stealing, a stealing pool never overflowing.",
             "pitch velocity");
  PARAM_DOC("voices", "16",
            "The size of the voice pool — Max's first creation argument, whose documented default "
            "is 16. Clamped to 1-128 when it is read, so a live re-parse from the audio thread "
            "cannot put it out of range. Shrinking it under a sounding patch strands nothing: only "
            "allocation is bounded by it, while a release, a 'stop' and the teardown pass walk the "
            "whole table, so a voice left above the new bound still gets its note-off and simply "
            "never plays again.",
            "1-128");
  PARAM_DOC("steal", "0",
            "Whether a full pool steals — Max's second creation argument. 0, the default, sends "
            "notes it cannot hold out the overflow outlet; any non-zero value turns off the note "
            "held the longest and puts the new note in its place, sending the stolen note's "
            "release first so the voice chain downstream is told to let go before it is told to "
            "play again. It can be changed live, and doing so only affects the notes that arrive "
            "afterwards.",
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
