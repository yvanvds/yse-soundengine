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
      "The pitch, and the half of the pair that acts. An int or a float here goes straight out the "
      "left outlet with the stored velocity out the right one — nothing is filtered and nothing is "
      "rewritten — and on the way past the object remembers what the pair did: a non-zero velocity "
      "marks the pitch sounding, and a velocity of 0 clears it, 0 being how the whole MIDI world "
      "spells a release. A float pitch is truncated to an int, as it is in Max. A list is Max's "
      "inlet distribution written on one cord: the first element is the pitch and a second element "
      "sets the velocity first, so '60 100' plays pitch 60 at velocity 100 and '60 0' releases it "
      "— which is exactly the shape '.midiparse''s note outlet sends. Further elements are "
      "ignored. "
      "A single-token numeric list is the number it spells, so a '.m 60' reaches this inlet as a "
      "pitch. A bang releases every note still sounding, and the message 'clear' forgets them "
      "without sending anything — Max's two commands, and words rather than pitches. A pitch "
      "outside 0-127 passes through unaltered but is not tracked, the set being a fixed bitmap "
      "over "
      "the MIDI note range.";

  constexpr char kVelocityInletDoc[] =
      "Sets the velocity that pitches arriving *after* it will be paired with — Max's right inlet. "
      "It outputs nothing by itself: only a pitch completes a note, so only a pitch can pass one "
      "on "
      "or mark one sounding. Ints, floats and a list whose leading token is a number all set it; a "
      "float is truncated. The value is stored and passed on exactly as given, unclamped and never "
      "refused for being outside MIDI's 0-127 — this object remembers notes, it does not rewrite "
      "them, and 0 is the only value it reads meaning into.";

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

  ADD_DESCRIPTION(
      "Releases every note the patcher is holding — Max's 'flush', which 'keeps track of all "
      "note-ons passed through it, and produces note-off messages for any held notes when it "
      "receives a bang'. A pitch in the left inlet passes straight through with the stored "
      "velocity, and on the way past the object remembers whether that pair turned a note on (any "
      "velocity but 0) or off (velocity 0). A bang then sends one release — the same pitch with "
      "velocity 0 — for every note still sounding, in ascending pitch order, and empties the set; "
      "'clear' empties it without sending anything. This is the panic stop for a patch that plays "
      "notes out of its own logic: a sequence interrupted between its note-ons and its note-offs, "
      "a "
      "'.metro' switched off mid-phrase, a generative voice whose releases were never written. "
      "Nothing else can find those notes again, every note sender in the patcher ('.noteon', "
      "'.midiformat', the '.x*out' family) being a stateless formatter that sends what it is "
      "handed "
      "and remembers nothing. It is not a second '.midiflush' and the two are complements rather "
      "than alternatives: '.midiflush' watches a MIDI byte stream, decoding running status and "
      "system-exclusive dumps, and releases what that stream left sounding on the channel each "
      "note "
      "carried, so it belongs after the formatters in the cord to '.midiout'; this object watches "
      "pitch/velocity pairs, which is what a patch deals in before anything has been formatted, so "
      "it belongs between the logic that plays notes and the sender that encodes them. It "
      "accordingly has no channel and wants none — at that point the channel has not been decided, "
      "every sender downstream taking one of its own — and its releases go out as the same pair of "
      "ints its input is, ready to be wired into the very '.noteon' the attacks went through. "
      "Nothing is filtered on the way past: that is '.stripnote''s job, and this object only "
      "remembers. The right inlet stores the velocity for pitches arriving after it and outputs "
      "nothing itself, and a list in the left inlet is Max's inlet distribution on one cord — "
      "'60 100' plays pitch 60 at velocity 100 — which is the shape '.midiparse''s note outlet "
      "sends. The velocity goes out the right outlet before the pitch goes out the left, Max's "
      "outlets firing right to left, which here is not cosmetic: everything downstream that takes "
      "a "
      "pair takes pitch on a hot inlet and velocity on a cold one, so a pitch sent first would "
      "carry the previous note's velocity. A note is released exactly once, so banging twice sends "
      "nothing the second time, and notes played after a flush are tracked from scratch. A pitch "
      "outside 0-127 passes through unaltered but is not tracked, the set being a fixed bitmap of "
      "128 bits allocated with the object — so neither the pass-through nor the flush allocates, "
      "locks or blocks on the audio thread, and the flush is bounded by the bitmap rather than by "
      "how many notes a patch managed to strand. Like '.stripnote' it opens no device and needs "
      "none, so it works on every platform. It also flushes by itself when the patcher is cleared "
      "or destroyed, and when the object is deleted on its own (issue #758): the patcher stops "
      "every object before it unwires any of them, so the releases go out down a patch that is "
      "still whole and reach the sender exactly as a bang's do. Nothing can be promised for a "
      "process killed outright, and Calculate() does nothing.");
  ADD_CATEGORY(pCategory::MIDI);

  INLET_DOC(0, "pitch", kPitchInletDoc, "int, float, list, 'bang', 'clear'");
  INLET_DOC(1, "velocity", kVelocityInletDoc, "0-127");
  OUTLET_DOC(0, "pitch",
             "The pitch, sent once as it passes through and again when a bang releases it. The "
             "same value both times, which is what lets a release find the note it belongs to. "
             "Sent after the velocity, Max's outlets firing right to left.",
             "int");
  OUTLET_DOC(1, "velocity",
             "The velocity: the stored one as the pair passes through, and 0 for every release a "
             "bang sends. Sent before the pitch so anything downstream has it in hand by the time "
             "the pitch reaches its hot inlet.",
             "int");
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
