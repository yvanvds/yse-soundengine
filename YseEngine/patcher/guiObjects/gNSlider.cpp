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
      "The note, and the only inlet - everything here emits, which is what a hot inlet in this "
      "patcher does. An int or a float is a pitch and resets the spelling to the 'spelling' "
      "parameter's choice: a note arriving from elsewhere in the patch carries no opinion about "
      "how to write it. A list is '<pitch> [<accidental>]' - the first number is the pitch and an "
      "optional second is the accidental to spell it with, 1 for a sharp and -1 for a flat - so "
      "two numbers is exactly the string GetGuiValue() produces, which is issue #551's round trip; "
      "further numbers are ignored, as .rslider ignores them. 'set <index> <value>' writes one "
      "cell, #551's cell write: 'set 0 <pitch>' moves the note and leaves the spelling alone, "
      "unlike a bare int, because a cell write touches one cell by definition, and 'set 1 "
      "<accidental>' re-spells the note without moving it. A bang re-sends. The pitch is clamped "
      "into the minimum-maximum bounds, and an accidental on a white key is ignored - there is no "
      "such thing as a sharpened E drawn as an E.";

  constexpr char kPitchOutletDoc[] =
      "The pitch, as an int, clamped into the minimum-maximum bounds. Sent after the accidental, "
      "the patcher's right-to-left ordering, so anything downstream that takes the two on a hot "
      "and a cold inlet has the spelling in hand before the pitch arrives. Sampled once at the top "
      "of the send together with the accidental, so a note re-spelled mid-send cannot make the two "
      "outlets describe two different notes.";

  constexpr char kAccidentalOutletDoc[] =
      "How the pitch is spelled: 1 a sharp, -1 a flat, 0 a natural. Always 0 for a white key. For "
      "one of the five black keys it is the per-note override the last message set, or the "
      "'spelling' parameter when it set none. With this and the pitch a host has the whole note: "
      "the sign to draw, and - by subtracting it from the pitch - the natural it attaches to, "
      "which is the line or space to draw on. Sent first.";

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

  ADD_DESCRIPTION(
      "A pitch on a staff - Max's nslider, 'display or output a pitch on a musical staff', and the "
      "other half of the pair .kslider opens: one expresses a pitch as a key on a keyboard, this "
      "one as a note on a stave, and both hand a host a musically meaningful surface instead of a "
      "bare number box. It is not .i with a different name, and the accidental is why: a pitch "
      "alone is what .i already holds, while what a staff needs is how to *spell* the pitch, "
      "because MIDI pitch 61 is C sharp or D flat - the same key, two different notes, drawn on "
      "two different lines with two different signs. Nothing in the pitch decides which; the key "
      "the music is in does, and headless that has to come from somewhere the patch can set. So "
      "the state is two cells under issue #551's protocol: cell 0 the pitch, cell 1 the accidental "
      "(1 sharp, -1 flat, 0 natural). With those two a host has the whole note - the sign to draw, "
      "and by subtracting it from the pitch the natural it attaches to, which fixes the line or "
      "space. The accidental is derived and overridable: always 0 for a white key, since there is "
      "no such thing as a sharpened E drawn as an E, and for one of the five black keys it is the "
      "'spelling' parameter's choice unless the message that set the pitch said otherwise. That "
      "override lasts until the next bare pitch arrives, so a patch in F minor sets 'spelling -1' "
      "once and a single note re-spelled as a sharp does not change the key it is in. Black and "
      "white are computed from the pitch class rather than looked up - classes 1, 3, 6, 8 and 10 "
      "are the black keys - which is one compare chain and no table on either side: YSE::scale "
      "answers 'is this pitch in the key', a different question, and MIDI::M_PITCH is a name enum "
      "in which CSM1 and DFM1 are aliases of the same value, so by construction it cannot say how "
      "a pitch should be spelled. The one inlet is hot and everything on it emits: an int or float "
      "is a pitch and resets the spelling to the parameter's choice, a list is '<pitch> "
      "[<accidental>]' which at two numbers is exactly what GetGuiValue() produces, 'set 0 "
      "<pitch>' moves the note without touching the spelling and 'set 1 <accidental>' re-spells it "
      "without moving it, and a bang re-sends. Pitch leaves outlet 0 and the accidental outlet 1, "
      "firing right to left so the spelling is in hand before the pitch lands on a hot inlet "
      "downstream, and both carry one sample of the state taken at the top of the send. The "
      "minimum and maximum are the pitches the stave covers, the family's ordered parameter pair, "
      "clamped on the way out as well as in and themselves confined to 0-127, a stave here drawing "
      "MIDI pitches. Not ported: Max's 'clear', which would have to invent an empty state that "
      "every reader of the two cells then had to handle, there being no pitch that means nothing - "
      "0 is C minus one, a real note; everything about drawing, the patcher being headless; and "
      "chords, which are a list of pitches, and the patcher's answer to a list of pitches is the "
      "object that holds one. Nothing on any path allocates, locks or blocks: the state is two "
      "atomic ints, the outlets carry ints so no send builds a string, and the list handler "
      "compares its keyword in place.");
  ADD_CATEGORY(pCategory::GUI);
  INLET_DOC(0, "note", kInletDoc, "minimum-maximum");
  OUTLET_DOC(0, "pitch", kPitchOutletDoc, "minimum-maximum");
  OUTLET_DOC(1, "accidental", kAccidentalOutletDoc, "-1, 0 or 1");
  PARAM_DOC("minimum", "0",
            "Lowest pitch the stave covers. Taken as an ordered pair with the maximum, so a "
            "reversed argument pair still bounds against the right two numbers, and confined to "
            "0-127 - a stave here draws MIDI pitches, and a pitch outside that range is not one. "
            "Clamping happens on the way out as well as in, .incdec's rule, so a live re-range "
            "shows immediately.",
            "0-127");
  PARAM_DOC("maximum", "127", "Highest pitch the stave covers. See the minimum.", "0-127");
  PARAM_DOC("spelling", "1",
            "How a black key is written when nothing overrides it: 1 or anything above it a sharp, "
            "anything below 0 a flat. This is the key the music is in rather than a property of "
            "any one note, which is why it is a creation argument and the per-note choice is a "
            "message - a patch in F minor sets 'spelling -1' once and a single note re-spelled as "
            "a sharp does not change it back. White keys are unaffected: their accidental is 0 "
            "whatever this says.",
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
