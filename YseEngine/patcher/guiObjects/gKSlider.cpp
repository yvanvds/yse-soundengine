// `.kslider` (issue #555). See gKSlider.h for the design; this file is the
// grammar, the 128-key store and the GUI protocol.
#include "gKSlider.h"

#include "../math/gExprEval.h"
#include "../pListArgs.h"
#include "../pSelector.h"

using namespace YSE::PATCHER;

#define className gKSlider

namespace {

  // The bounds of the token starting at or after `from`, or false when there is
  // none. Walked in place rather than through substr: this runs on whichever
  // thread the message arrived on, which is routinely the audio callback.
  bool NextToken(const char* text, std::size_t length, std::size_t from, std::size_t& begin,
                 std::size_t& end) {
    begin = from;
    while (begin < length && IsSelectorSeparator(text[begin]))
      begin++;
    end = begin;
    while (end < length && !IsSelectorSeparator(text[end]))
      end++;
    return end > begin;
  }

  // The next token that reads as a number, advancing `from` past it. Tokens that
  // are not numbers are *skipped* rather than ending the walk, which is
  // ExprParseFloatList's policy and the one `.multislider` and `.matrixctrl`
  // walk their long lists with.
  bool NextNumber(const char* text, std::size_t length, std::size_t& from, float& out) {
    std::size_t begin = 0;
    std::size_t end = 0;
    while (NextToken(text, length, from, begin, end)) {
      from = end;
      if (ReadNumericToken(text + begin, end - begin, out)) return true;
    }
    return false;
  }

  // How many numbers the whole message holds. Read before anything is stored,
  // because the count is what tells the whole state from Max's pair — and read
  // by walking the text a second time rather than into a buffer, a whole state
  // being 128 numbers that may arrive down a cord on the audio thread.
  std::size_t CountNumbers(const char* text, std::size_t length) {
    std::size_t cursor = 0;
    std::size_t count = 0;
    float number = 0.f;
    while (NextNumber(text, length, cursor, number))
      count++;
    return count;
  }

  // Whether the `length` characters at `text` are exactly `word`. Compared in
  // place for the same reason the tokens are walked in place.
  bool TokenIs(const char* text, std::size_t length, const char* word, std::size_t wordLength) {
    if (length != wordLength) return false;
    for (std::size_t i = 0; i < length; i++) {
      if (text[i] != word[i]) return false;
    }
    return true;
  }

  constexpr char kPitchInletDoc[] =
      "The pitch, and the half of the pair that acts - everything here emits. An int or a float "
      "presses that key at the stored velocity and sends the pair; a float is truncated, as it is "
      "in Max. A list of two numbers is Max's list method and '.flush''s inlet distribution "
      "written on one cord: '60 100' presses key 60 at velocity 100 and '60 0' releases it, 0 "
      "being how the whole MIDI world spells a release - which is also the shape '.midiparse''s "
      "note outlet sends. A single-token numeric list is the number it spells, so a '.m 60' "
      "reaches this inlet as a pitch. A list of exactly 128 numbers is the whole keyboard, issue "
      "#551's round trip and the string GetGuiValue() produced; only the keys whose velocity "
      "actually changed are sent, because a note controller's outlet drives events and only a "
      "transition is one. 'set <index> <value>' is #551's cell write and on this object it is the "
      "same message as the pair, the index being the pitch. 'clear' releases every key it is "
      "holding, Max's message and '.flush''s bang. A bang re-sends every key still held, in "
      "ascending pitch order. A pitch outside 0-127 is dropped rather than clamped or passed on: "
      "unlike '.flush', which watches a cord and must not rewrite what crosses it, this object is "
      "a source, so a pitch it cannot display is a key it cannot press.";

  constexpr char kVelocityInletDoc[] =
      "Sets the velocity that pitches arriving *after* it will be played at, and outputs nothing "
      "by itself - only a pitch completes a note. Max has no such inlet because its kslider is "
      "played with a mouse, which supplies a velocity from where the key was clicked; headless "
      "there is nothing else a bare pitch could be paired with, so this is '.flush''s and "
      "'.stripnote''s right inlet. Ints, floats and a list whose leading token is a number all "
      "set it, a float being truncated. Clamped into 0-127, unlike '.flush' which passes what it "
      "is handed: this object *holds* the velocity in a cell a host draws, so it may not hold a "
      "number a MIDI velocity could not be.";

  constexpr char kPitchOutletDoc[] =
      "The pitch of the key that moved: once as it is pressed or released, again for every held "
      "key a bang re-sends, and once more with velocity 0 for every key a 'clear' lets go. Sent "
      "after the velocity, Max's outlets firing right to left. An int, and deliberately the same "
      "pair-of-ints port shape '.noteon', '.makenote', '.stripnote' and '.flush' have, so the pair "
      "is wired across rather than packed and unpacked again.";

  constexpr char kVelocityOutletDoc[] =
      "The velocity the key moved to - the played velocity for a press, and 0 for a release, "
      "whether the release came from a '<pitch> 0' pair, from a 'clear' or from a whole-state "
      "write that lifted the key. Sent before the pitch, which is not cosmetic: everything "
      "downstream that takes a pair takes velocity on a cold inlet and pitch on a hot one, so a "
      "pitch sent first would carry the previous note's velocity.";

} // namespace

CONSTRUCT() {
  // Every key explicitly up. The array's members are atomics, which merely
  // default-constructed hold no defined value under C++17, and "every key
  // always holds a velocity" is what lets a bare keyboard be read and dumped.
  for (int i = 0; i < PITCHES; i++)
    keys[i].store(0, std::memory_order_relaxed);

  ADD_IN_0;
  REG_INT_IN(IntIn);
  REG_FLOAT_IN(FloatIn);
  REG_LIST_IN(ListIn);
  REG_BANG_IN(BangIn);

  // Cold, so a velocity written here is stored without playing anything —
  // `.flush`'s and `.stripnote`'s right inlet exactly. The same handlers,
  // routing on `inlet`.
  ADD_IN_1;
  REG_INT_IN(IntIn);
  REG_FLOAT_IN(FloatIn);
  REG_LIST_IN(ListIn);

  ADD_OUT_INT; // 0: pitch
  ADD_OUT_INT; // 1: velocity

  // In place before Register() hands the field to the parameter system: a
  // creation argument overwrites it, no argument leaves it alone.
  velocity = DEFAULT_VELOCITY;
  ADD_PARAM(velocity);

  ADD_DESCRIPTION(
      "A piano keyboard as one control - Max's kslider, 'output pitch and velocity from a keyboard "
      "display'. The patcher already has every way of expressing a number as a control (.slider a "
      "position, .dial a value, .rslider a span, .multislider a bank) and none of expressing a "
      "note, which is not a number that happens to run 0-127 but one of 128 named things, five of "
      "every twelve of which are black. This is the surface a musician picks one from, and it "
      "feeds the patcher's note objects - .noteon, .makenote, .flush, .midiformat - in the "
      "pair-of-ints they already speak. The state is the keyboard itself: one GUI cell per MIDI "
      "pitch holding that key's velocity, 0 meaning the key is up, so the cell count is always 128 "
      "and a host draws key i held exactly when cell i is non-zero, with no decoding and no "
      "separate 'how many are down' answer that could disagree with the cells. A velocity per key "
      "rather than a bare bit, because the object's whole output is a pair: a bitmap could say "
      "which keys are down but not what to re-send for one, so a bang could not replay what it "
      "holds. Unlike .multislider the count is fixed - a keyboard is 128 keys because MIDI is - so "
      "there is no size, no listresize and no live re-count, and every read is a plain indexed "
      "load. Two inlets, .flush's and .stripnote's shape rather than Max's single one, because a "
      "pitch/velocity pair is written that way everywhere in this patcher: the left inlet takes "
      "the pitch and acts, the right one stores the velocity for pitches arriving after it and "
      "sends nothing. Max needs no velocity inlet because its kslider is clicked and the click "
      "supplies one. Every message that changes a key emits the pair for the key it changed, since "
      "headless the outlet is the only way a change reaches anything; but the bulk messages emit "
      "what actually *changed* rather than every cell - a whole-state write sends attacks for keys "
      "that went down and releases for keys that came up, 'clear' sends a release per held key, "
      "and a bang sends a pair per held key. Replaying all 128 the way .matrixctrl dumps its grid "
      "would send 120-odd releases for keys nobody touched and re-attack the ones already "
      "sounding, which downstream of a synth is a retrigger rather than a repaint. The single-key "
      "forms emit even when the velocity is unchanged, because there the patch said 'play this' "
      "and a repeated note-on at the same velocity is something a musician does on purpose. Pitch "
      "goes out outlet 0 and velocity out outlet 1, right to left, which is load-bearing: "
      "everything downstream that takes a pair takes velocity on a cold inlet and pitch on a hot "
      "one, so a pitch sent first would carry the previous note's velocity. The bounds are MIDI's "
      "and are not creation arguments - the family's minimum/maximum describe a range a patch "
      "chooses, and nothing chooses how many keys a keyboard has - so a pitch outside 0-127 is "
      "dropped rather than clamped or passed on: .flush passes an untrackable pitch through "
      "because it watches a cord, while this object is a source, and filing a pitch under one that "
      "is not its own would light the wrong key. Max's mode attribute (monophonic / polyphonic) is "
      "not ported: monophony exists there because a kslider is clicked and a mouse can only be in "
      "one place, a control whose entire state is a 128-key held set is not usefully restricted to "
      "one key, and issue #551's 128-number restore could not be honoured in a mode that permits "
      "one - a patch that wants monophony puts .poly or .flush in the cord. Nor is anything about "
      "drawing, the patcher being headless. Only the velocity parameter persists; held keys are "
      "run-time state, and the form a host stores them in is GetGuiValue(), which inlet 0 takes "
      "straight back. Nothing on any path allocates, locks or blocks: the keyboard is a fixed "
      "member array, the outlets carry ints so no send builds a string at all, and the list "
      "handler compares its keywords and walks its numbers in place rather than into a 128-wide "
      "stack buffer.");
  ADD_CATEGORY(pCategory::GUI);
  INLET_DOC(0, "pitch", kPitchInletDoc, "0-127");
  INLET_DOC(1, "velocity", kVelocityInletDoc, "0-127");
  OUTLET_DOC(0, "pitch", kPitchOutletDoc, "0-127");
  OUTLET_DOC(1, "velocity", kVelocityOutletDoc, "0-127");
  PARAM_DOC("velocity", "100",
            "The velocity a bare pitch is played at until something is sent to the right inlet, "
            "which then changes it for the rest of the object's life - the same arrangement "
            ".multislider's size has, a creation argument and live state being one store. Max "
            "needs no such default because its kslider takes its velocity from where the key was "
            "clicked; headless there is no click, and 0 - .flush's starting velocity - would make "
            "every bare pitch a release, so the object has to start somewhere a key can actually "
            "go down. 100 is what .makenote and the MIDI world use for 'played normally'. Clamped "
            "into 0-127, a cell holding a MIDI velocity. Held keys are not parameters and do not "
            "survive a save: GetGuiValue() is the form a host stores them in.",
            "0-127");
}

// ─── the keyboard ─────────────────────────────────────────────────────────────

int gKSlider::BoundVelocity(int value) {
  if (value < 0) return 0;
  if (value > MAX_VELOCITY) return MAX_VELOCITY;
  return value;
}

bool gKSlider::IsKey(int pitch) {
  return pitch >= 0 && pitch < PITCHES;
}

int gKSlider::VelocityOf(int pitch) const {
  if (!IsKey(pitch)) return 0;
  return keys[pitch].load(std::memory_order_relaxed);
}

int gKSlider::Held() const {
  int count = 0;
  for (int i = 0; i < PITCHES; i++) {
    if (keys[i].load(std::memory_order_relaxed) != 0) count++;
  }
  return count;
}

// ─── output ───────────────────────────────────────────────────────────────────

void gKSlider::Emit(int pitch, int noteVelocity, YSE::THREAD thread) {
  // Right to left, and load-bearing: everything downstream that takes a pair
  // takes its velocity on a cold inlet and its pitch on a hot one, so a pitch
  // sent first would be paired with the previous note's velocity. `.flush`'s
  // Emit, for `.flush`'s reason.
  outputs[1].SendInt(noteVelocity, thread);
  outputs[0].SendInt(pitch, thread);
}

void gKSlider::Play(int pitch, int noteVelocity, YSE::THREAD thread) {
  // A pitch the keyboard does not have is dropped whole — not clamped onto a
  // key that is not its own, and not passed on either. See the class comment.
  if (!IsKey(pitch)) return;

  const int bounded = BoundVelocity(noteVelocity);
  // Stored *before* the pair is sent, so the object's own idea of what is down
  // is settled before anything downstream can read it back.
  keys[pitch].store(bounded, std::memory_order_relaxed);
  Emit(pitch, bounded, thread);
}

void gKSlider::SendHeld(bool release, YSE::THREAD thread) {
  // Outlet 0's pitches are exactly what inlet 0 accepts, so a patch can wire the
  // control back into itself in one cord. Without this a pitch arriving there
  // during a dump would start a second full dump inside the first, and each of
  // *its* pairs a third: the send-depth guard bounds the depth, but the breadth
  // is the held count per level and multiplies. `.matrixctrl`'s guard.
  if (dumping) return;
  dumping = true;

  for (int pitch = 0; pitch < PITCHES; pitch++) {
    const int noteVelocity = keys[pitch].load(std::memory_order_relaxed);
    if (noteVelocity == 0) continue;
    // Lifted before anything is sent, so an outlet wired somewhere that reads
    // this object back sees a keyboard already emptied of what is on its way.
    if (release) keys[pitch].store(0, std::memory_order_relaxed);
    Emit(pitch, release ? 0 : noteVelocity, thread);
  }

  dumping = false;
}

void gKSlider::RestoreAll(const char* text, std::size_t length, YSE::THREAD thread) {
  // Issue #551's round trip, in two passes: the first reads the message in
  // place and writes every cell, the second emits. Split that way because every
  // cell must be written *before* anything is sent — a patch that re-enters
  // through an outlet has to see the restored keyboard whole rather than half
  // of it — and because "what moved" is only knowable while the old velocity is
  // still there to compare against.
  //
  // The one stack buffer on this path, and it is what the split costs: 128 ints
  // of which key moved and where to, -1 for one that did not. Half a kilobyte,
  // fixed with the message rather than the list, and no allocation. The
  // *message* is still never copied into a buffer — that is the part that would
  // scale, and `.multislider` and `.matrixctrl` walk their long lists in place
  // for the same reason.
  int changed[PITCHES];
  std::size_t cursor = 0;
  float number = 0.f;
  for (int pitch = 0; pitch < PITCHES; pitch++) {
    if (!NextNumber(text, length, cursor, number)) {
      changed[pitch] = -1;
      continue;
    }
    const int wanted = BoundVelocity(ExprToInt(number));
    const int previous = keys[pitch].load(std::memory_order_relaxed);
    if (wanted == previous) {
      changed[pitch] = -1;
      continue;
    }
    keys[pitch].store(wanted, std::memory_order_relaxed);
    changed[pitch] = wanted;
  }

  // Only the keys that moved: attacks for those that went down, releases for
  // those that came up, and nothing for the ones that did not move. See the
  // class comment on why this is not `.matrixctrl`'s full replay.
  if (dumping) return;
  dumping = true;
  for (int pitch = 0; pitch < PITCHES; pitch++) {
    if (changed[pitch] < 0) continue;
    Emit(pitch, changed[pitch], thread);
  }
  dumping = false;
}

// ─── inlets ───────────────────────────────────────────────────────────────────

BANG_IN(BangIn) {
  // Re-send what is held, which is the keyboard's own state going out again.
  // Only inlet 0 registers a bang handler at all.
  (void)inlet;
  SendHeld(false, thread);
}

INT_IN(IntIn) {
  if (inlet == 0) {
    Play(value, Velocity(), thread);
    return;
  }
  // The right inlet: the velocity for pitches arriving after it. Clamped on the
  // way in, unlike `.flush`'s — see the header.
  velocity.store(BoundVelocity(value), std::memory_order_relaxed);
}

FLOAT_IN(FloatIn) {
  // Max converts a float to an int in both inlets. ExprToInt rather than a cast:
  // a value outside the int range is undefined behaviour to cast.
  IntIn(ExprToInt(value), inlet, thread);
}

LIST_IN(ListIn) {
  const char* text = value.c_str();
  const std::size_t length = value.size();

  std::size_t begin = 0;
  std::size_t end = 0;
  // An empty list has nothing to address and is not a bang.
  if (!NextToken(text, length, 0, begin, end)) return;

  if (inlet != 0) {
    // The cold inlet takes a velocity, and a list whose leading token is a
    // number is that number — `.flush`'s reading of the same message.
    float number = 0.f;
    std::size_t cursor = 0;
    if (NextNumber(text, length, cursor, number)) {
      velocity.store(BoundVelocity(ExprToInt(number)), std::memory_order_relaxed);
    }
    return;
  }

  if (TokenIs(text + begin, end - begin, "set", 3)) {
    // "set <index> <value>" — issue #551's cell write, and on this object the
    // same message as the pair below, since the index *is* the pitch. The happy
    // case `.multislider` also found and `.rslider` could not.
    std::size_t cursor = end;
    float index = 0.f;
    float cellValue = 0.f;
    if (!NextNumber(text, length, cursor, index)) return;
    if (!NextNumber(text, length, cursor, cellValue)) return;
    // Range-checked rather than indexed, as the protocol requires: an index
    // outside the keyboard — or a NaN, which fails the first compare — is
    // dropped. A fractional index truncates towards zero, as every other index
    // in the patcher does.
    if (!(index >= 0.f)) return;
    Play(ExprToInt(index), ExprToInt(cellValue), thread);
    return;
  }

  if (TokenIs(text + begin, end - begin, "clear", 5)) {
    // Max: "turns off all currently displayed notes". A release per held key
    // rather than a silent erase, which is `.flush`'s bang and not its `clear`:
    // headless the outlet is the only way the change reaches the synth that is
    // sounding them, and a keyboard that forgot its notes without saying so
    // would leave them hanging with nothing able to find them again.
    SendHeld(true, thread);
    return;
  }

  // A bare list is one of three things, and its length is what says which: the
  // whole state (issue #551's round trip) at exactly 128 numbers, Max's pair at
  // 2, and a lone pitch at 1 — the form a `.m 60` arrives as. Any other length
  // addresses nothing and is ignored, the family's reading of a message with
  // nothing to address.
  const std::size_t numbers = CountNumbers(text, length);

  if (numbers == (std::size_t)PITCHES) {
    RestoreAll(text, length, thread);
    return;
  }

  std::size_t cursor = 0;
  float pitch = 0.f;
  if (!NextNumber(text, length, cursor, pitch)) return;

  if (numbers == 1) {
    Play(ExprToInt(pitch), Velocity(), thread);
    return;
  }

  if (numbers == 2) {
    // Max's list method and `.flush`'s inlet distribution on one cord: the
    // velocity is stored first, exactly as if it had arrived on inlet 1, and
    // then the pitch is played with it.
    float noteVelocity = 0.f;
    if (!NextNumber(text, length, cursor, noteVelocity)) return;
    velocity.store(BoundVelocity(ExprToInt(noteVelocity)), std::memory_order_relaxed);
    Play(ExprToInt(pitch), Velocity(), thread);
  }
}

// ─── the GUI value protocol (issue #551) ──────────────────────────────────────

GUI_VALUE() {
  // One call, one allocation — what the protocol asks a bulk read to be. On the
  // host thread, so a local rather than a member: nothing here is shared with
  // the message path.
  std::string out;
  out.reserve((std::size_t)PITCHES * ((std::size_t)FORMAT_INT_WIDTH + 1));

  char digits[FORMAT_INT_WIDTH];
  for (int pitch = 0; pitch < PITCHES; pitch++) {
    if (pitch > 0) out.push_back(' ');
    const std::size_t written = WriteInt(keys[pitch].load(std::memory_order_relaxed), digits);
    out.append(digits, written);
  }
  return out;
}

GUI_VALUE_COUNT() {
  // Fixed: a keyboard is 128 keys because MIDI is. Nothing a message or a
  // parameter can do changes it, which is the whole difference from
  // `.multislider`'s live count.
  return (unsigned int)PITCHES;
}

GUI_VALUE_AT() {
  // Range-checked against the count rather than indexed, as the protocol
  // requires: past the end is "", never the whole state again.
  if (index >= (unsigned int)PITCHES) return std::string();
  char digits[FORMAT_INT_WIDTH];
  const std::size_t written = WriteInt(keys[index].load(std::memory_order_relaxed), digits);
  return std::string(digits, written);
}

#undef className
