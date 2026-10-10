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
      "A pitch presses that key at the stored velocity; '<pitch> <velocity>' plays it at that "
      "velocity, and velocity 0 releases it. Also 128 numbers (the whole keyboard), "
      "'set <pitch> <velocity>', 'clear' and bang.";

  constexpr char kVelocityInletDoc[] =
      "The velocity for pitches that arrive after it. Sends nothing. Clamped to 0-127; a float "
      "is truncated.";

  constexpr char kPitchOutletDoc[] = "The pitch of the key that moved. Sent after the velocity.";

  constexpr char kVelocityOutletDoc[] =
      "The key's new velocity: 0 for a release. Sent before the pitch, so it pairs with the "
      "pitch in the next object.";

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

  ADD_DESCRIPTION("A piano keyboard. A pitch on the left inlet presses that key at the stored "
                  "velocity and sends velocity and pitch; velocity 0 releases it. The state is "
                  "the velocity held on each of the 128 keys.");
  ADD_CATEGORY(pCategory::GUI);
  INLET_DOC(0, "pitch", kPitchInletDoc, "0-127");
  INLET_DOC(1, "velocity", kVelocityInletDoc, "0-127");
  OUTLET_DOC(0, "pitch", kPitchOutletDoc, "0-127");
  OUTLET_DOC(1, "velocity", kVelocityOutletDoc, "0-127");
  PARAM_DOC("velocity", "100",
            "The velocity a bare pitch is played at, until the right inlet or a "
            "'<pitch> <velocity>' list changes it.",
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
