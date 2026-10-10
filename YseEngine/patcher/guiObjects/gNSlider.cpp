// `.nslider` (issue #555). See gNSlider.h for the design; this file is the
// grammar, the two cells and the accidental.
#include "gNSlider.h"

#include "../math/gExprEval.h"
#include "../pListArgs.h"

#include <cstring>

using namespace YSE::PATCHER;

#define className gNSlider

namespace {

  // Does `text` start with the protocol's `set` keyword? Compared in place
  // rather than by building a token, because a list may arrive on the audio
  // thread. The word only counts as the keyword when a separator (or the end of
  // the string) follows it, so a list beginning with "settle" is not one.
  // `.rslider`'s reader, for `.rslider`'s reason.
  bool IsSetKeyword(const char* text) {
    if (std::strncmp(text, "set", 3) != 0) return false;
    const char after = text[3];
    return after == '\0' || after == ' ' || after == '\t';
  }

  constexpr char kInletDoc[] =
      "A number is a pitch, spelled the default way; '<pitch> <accidental>' chooses the "
      "spelling; 'set 0 <pitch>' moves the note and 'set 1 <accidental>' re-spells it; a bang "
      "sends again. Everything here sends.";

  constexpr char kPitchOutletDoc[] = "The pitch. Sent after the accidental.";

  constexpr char kAccidentalOutletDoc[] =
      "How the pitch is spelled: 1 sharp, -1 flat, 0 natural (always 0 for a white key). Sent "
      "first.";

} // namespace

CONSTRUCT() {
  ADD_IN_0;
  REG_INT_IN(IntIn);
  REG_FLOAT_IN(FloatIn);
  REG_LIST_IN(ListIn);
  REG_BANG_IN(BangIn);

  ADD_OUT_INT; // 0: the pitch
  ADD_OUT_INT; // 1: the accidental

  pitch = MIN_PITCH;
  accidental = 0;

  // In place before Register() hands the fields to the parameter system: a
  // creation argument overwrites them, no argument leaves them alone. The whole
  // MIDI range, and Max's own default spelling.
  minimum = MIN_PITCH;
  maximum = MAX_PITCH;
  spelling = DEFAULT_SPELLING;

  ADD_PARAM(minimum);
  ADD_PARAM(maximum);
  ADD_PARAM(spelling);

  ADD_DESCRIPTION("A note on a staff: a pitch and how it is spelled. It sends the pitch and its "
                  "accidental (1 sharp, -1 flat, 0 natural); a black key follows the 'spelling' "
                  "argument unless a message chooses otherwise.");
  ADD_CATEGORY(pCategory::GUI);
  INLET_DOC(0, "note", kInletDoc, "minimum-maximum");
  OUTLET_DOC(0, "pitch", kPitchOutletDoc, "minimum-maximum");
  OUTLET_DOC(1, "accidental", kAccidentalOutletDoc, "-1, 0 or 1");
  PARAM_DOC("minimum", "0", "The lowest pitch the staff covers.", "0-127");
  PARAM_DOC("maximum", "127", "The highest pitch the staff covers.", "0-127");
  PARAM_DOC("spelling", "1",
            "How a black key is spelled when no message chooses: 0 or above for a sharp, below "
            "0 for a flat.",
            "1 for sharps, -1 for flats");
}

// ─── the note ─────────────────────────────────────────────────────────────────

int gNSlider::Bound(int value, int low, int high) {
  // The bounds are taken as an ordered pair, `.rslider`'s and `.incdec`'s rule,
  // so a reversed argument pair still bounds against the right two numbers, and
  // each end is itself confined to the MIDI range before it is used — a stave
  // that claimed to cover pitch 500 would report a note nothing can play.
  int lowBound = low < high ? low : high;
  int highBound = low < high ? high : low;
  if (lowBound < MIN_PITCH) lowBound = MIN_PITCH;
  if (lowBound > MAX_PITCH) lowBound = MAX_PITCH;
  if (highBound < MIN_PITCH) highBound = MIN_PITCH;
  if (highBound > MAX_PITCH) highBound = MAX_PITCH;

  if (value < lowBound) return lowBound;
  if (value > highBound) return highBound;
  return value;
}

bool gNSlider::IsBlack(int value) {
  // Pitch classes 1, 3, 6, 8 and 10 — C#, D#, F#, G# and A#. The modulo is
  // written to stay non-negative for a negative pitch, which Bound() rules out
  // but which this helper must not depend on it for.
  const int pitchClass = ((value % 12) + 12) % 12;
  return pitchClass == 1 || pitchClass == 3 || pitchClass == 6 || pitchClass == 8 ||
         pitchClass == 10;
}

int gNSlider::Pitch() const {
  return Bound(pitch.load(std::memory_order_relaxed), minimum, maximum);
}

int gNSlider::Accidental() const {
  // A white key is a natural, whatever was asked for: there is no sign to draw
  // and the note is already on its own line.
  if (!IsBlack(Pitch())) return 0;

  // The per-note override first, then the key the music is in. Both are read as
  // a sign rather than a value, so `set 1 5` is a sharp rather than five of
  // them, and the object never reports an accidental a host cannot draw.
  const int chosen = accidental.load(std::memory_order_relaxed);
  if (chosen > 0) return 1;
  if (chosen < 0) return -1;
  return spelling < 0 ? -1 : 1;
}

void gNSlider::StorePitch(int value, bool respell) {
  pitch.store(Bound(value, minimum, maximum), std::memory_order_relaxed);
  // A bare pitch is "play this note", and a note arriving from elsewhere in the
  // patch carries no opinion about how to write it — so it goes back to the key
  // the music is in. A cell write does not, touching one cell by definition.
  if (respell) accidental.store(0, std::memory_order_relaxed);
}

void gNSlider::StoreAccidental(int value) {
  accidental.store(value, std::memory_order_relaxed);
}

// ─── inlets ───────────────────────────────────────────────────────────────────

BANG_IN(BangIn) {
  // Nothing to store; the hot inlet calculates on the way out, which is the
  // re-send. Only inlet 0 exists, and only it registers a bang handler.
  (void)inlet;
  (void)thread;
}

INT_IN(IntIn) {
  if (inlet != 0) return;
  (void)thread;
  StorePitch(value, true);
}

FLOAT_IN(FloatIn) {
  // A pitch is a whole note on a stave, so a float is truncated the way Max
  // truncates one. ExprToInt rather than a cast: a value outside the int range
  // is undefined behaviour to cast.
  IntIn(ExprToInt(value), inlet, thread);
}

LIST_IN(ListIn) {
  // Registered on inlet 0 only, but routed anyway: a later inlet must never
  // start writing the note because someone added a handler above.
  if (inlet != 0) return;
  (void)thread;

  const char* text = value.c_str();
  while (*text == ' ' || *text == '\t')
    text++;

  if (IsSetKeyword(text)) {
    // "set <index> <value>" — one cell. The keyword is what tells this from the
    // whole-state form, which for a two-cell control is the same two numbers
    // (issue #551).
    float parsed[2];
    if (ExprParseFloatList(text + 3, parsed, 2) < 2) return;
    // Range-checked rather than indexed: an index outside the two cells — or a
    // NaN, which fails the first compare — is dropped, never folded onto a real
    // cell. A fractional index truncates towards zero, as every other index in
    // the patcher does.
    const float index = parsed[0];
    if (!(index >= 0.f) || index >= 2.f) return;
    if ((int)index == 0) {
      // The pitch alone: a cell write moves the note and leaves the spelling.
      StorePitch(ExprToInt(parsed[1]), false);
    } else {
      StoreAccidental(ExprToInt(parsed[1]));
    }
    return;
  }

  // The whole state: the exact string GetGuiValue() produced. One number is a
  // bare pitch, exactly as an int on this inlet is; an empty list changes
  // nothing and falls through to the re-send a bang gives. Further numbers are
  // ignored, `.rslider`'s policy for a longer list.
  float parsed[2];
  const int count = ExprParseFloatList(text, parsed, 2);
  if (count < 1) return;
  // The accidental is written first, so the pitch's own reset cannot undo it.
  StoreAccidental(count >= 2 ? ExprToInt(parsed[1]) : 0);
  StorePitch(ExprToInt(parsed[0]), false);
}

// ─── the GUI value protocol (issue #551) ──────────────────────────────────────

GUI_VALUE() {
  char digits[FORMAT_INT_WIDTH];
  std::size_t written = WriteInt(Pitch(), digits);

  std::string out;
  out.reserve(2 * ((std::size_t)FORMAT_INT_WIDTH + 1));
  out.assign(digits, written);
  out.push_back(' ');
  written = WriteInt(Accidental(), digits);
  out.append(digits, written);
  return out;
}

GUI_VALUE_COUNT() {
  return 2;
}

GUI_VALUE_AT() {
  // Range-checked against the count rather than indexed, as the protocol
  // requires: past the end is "", never the whole state again.
  if (index > 1) return std::string();
  char digits[FORMAT_INT_WIDTH];
  const std::size_t written = WriteInt(index == 0 ? Pitch() : Accidental(), digits);
  return std::string(digits, written);
}

// ─── output ───────────────────────────────────────────────────────────────────

CALC() {
  // Sampled once, so both outlets carry the same note even if a message
  // arriving down one of the cords re-spells it while this send is in flight.
  const int notePitch = Pitch();
  const int noteAccidental = Accidental();

  // Right to left, `.trigger`'s ordering guarantee, and `.kslider`'s and
  // `.flush`'s reason for a pair: the spelling has to be in hand before the
  // pitch lands on a hot inlet downstream.
  outputs[1].SendInt(noteAccidental, thread);
  outputs[0].SendInt(notePitch, thread);
}

#undef className
