#include "gRSlider.h"
#include <cstring>

using namespace YSE::PATCHER;

#define className gRSlider

namespace {

  // Does `text` start with the protocol's `set` keyword? Compared in place
  // rather than by building a token, because a list may arrive on the audio
  // thread. The word only counts as the keyword when a separator (or the end of
  // the string) follows it, so a list beginning with "settle" is not one.
  bool IsSetKeyword(const char* text) {
    if (std::strncmp(text, "set", 3) != 0) return false;
    const char after = text[3];
    return after == '\0' || after == ' ' || after == '\t';
  }

} // namespace

CONSTRUCT() {
  ADD_IN_0;
  REG_INT_IN(IntIn);
  REG_FLOAT_IN(FloatIn);
  REG_LIST_IN(ListIn);
  REG_BANG_IN(BangIn);

  // Cold, so the end written here is stored without firing the outlets — Max's
  // right inlet exactly. Registering the same two handlers: they route on
  // `inlet`.
  ADD_IN_1;
  REG_INT_IN(IntIn);
  REG_FLOAT_IN(FloatIn);

  ADD_OUT_FLOAT; // 0: the lower end
  ADD_OUT_FLOAT; // 1: the higher end
  ADD_OUT_LIST; // 2: the pair

  first = 0.f;
  second = 0.f;

  // In place before Register() hands the fields to the parameter system: a
  // creation argument overwrites them, no argument leaves them alone. 0-127 is
  // Max's default size of 128, counted from 0.
  minimum = 0.f;
  maximum = 127.f;

  ADD_PARAM(minimum);
  ADD_PARAM(maximum);

  listText.reserve(LIST_CAPACITY);

  ADD_DESCRIPTION(
      "Range control: holds a span rather than a single value, which is what a velocity layer, a "
      "key split or a random spread actually is. Each inlet sets one end of the range - the left "
      "one emits, the right one stores silently - and a list on the left inlet sets both ends at "
      "once, as a bang re-sends them. Neither inlet is the low end: both ends are clamped into "
      "the minimum-maximum bounds and reported in order, so the lower of the two always leaves "
      "outlet 0 and the higher outlet 1, with the pair as a two-element list on outlet 2. The "
      "three fire right to left. The GUI value is the two ends as cells (issue #551): \"low "
      "high\" as a whole, and inlet 0 takes that string back verbatim or \"set <index> <value>\" "
      "to move one end.");
  ADD_CATEGORY(pCategory::GUI);
  INLET_DOC(0, "end/range",
            "A float or int sets one end of the range; a list sets both ends from its first two "
            "numbers; \"set <index> <value>\" moves one end (0 is the low end of the current "
            "range, 1 the high end); a bang re-sends. Everything here emits.",
            "minimum-maximum");
  INLET_DOC(1, "end", "Sets the other end of the range and stores it without sending anything out.",
            "minimum-maximum");
  OUTLET_DOC(0, "low", "The lower of the two ends. Sent last, after outlets 2 and 1.",
             "minimum-maximum");
  OUTLET_DOC(1, "high", "The higher of the two ends.", "minimum-maximum");
  OUTLET_DOC(2, "range", "Both ends as a two-element list, the lower one first. Sent first.",
             "minimum-maximum");
  PARAM_DOC("minimum", "0", "Lowest value either end of the range can reach.", "any float");
  PARAM_DOC("maximum", "127", "Highest value either end of the range can reach.", "any float");
}

float gRSlider::Bound(float value) const {
  // The bounds are taken as an ordered pair, .pong's and .incdec's rule, so a
  // reversed argument pair still bounds against the right two numbers. A NaN
  // fails both compares and is answered with the lower bound rather than
  // passed through, since every value this object reports must be inside the
  // range it documents.
  const float lowBound = minimum < maximum ? minimum : maximum;
  const float highBound = minimum < maximum ? maximum : minimum;
  if (!(value >= lowBound)) return lowBound;
  if (value > highBound) return highBound;
  return value;
}

float gRSlider::Low() const {
  const float a = Bound(first.load());
  const float b = Bound(second.load());
  return a < b ? a : b;
}

float gRSlider::High() const {
  const float a = Bound(first.load());
  const float b = Bound(second.load());
  return a < b ? b : a;
}

void gRSlider::StoreFirst(float value) {
  first = Bound(value);
}

void gRSlider::StoreSecond(float value) {
  second = Bound(value);
}

std::size_t gRSlider::Render(float value, char* out) {
  const int written = ExprFormatValue(ExprValue::Float(value), out, kExprValueTextMax);
  return written > 0 ? (std::size_t)written : 0;
}

BANG_IN(BangIn) {
  // Nothing to store; the hot inlet calculates on the way out, which is the
  // re-send. Only inlet 0 registers a bang handler at all.
}

INT_IN(IntIn) {
  FloatIn((float)value, inlet, thread);
}

FLOAT_IN(FloatIn) {
  if (inlet == 0) {
    StoreFirst(value);
  } else {
    StoreSecond(value);
  }
}

LIST_IN(ListIn) {
  // Registered on inlet 0 only, but routed anyway: a later inlet must never
  // start writing the first end because someone added a handler above.
  if (inlet != 0) return;

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
    // end. A fractional index truncates towards zero, as every other index in
    // the patcher does.
    const float index = parsed[0];
    if (!(index >= 0.f) || index >= 2.f) return;
    if ((int)index == 0) {
      StoreFirst(parsed[1]);
    } else {
      StoreSecond(parsed[1]);
    }
    return;
  }

  // Whole state: the exact string GetGuiValue() produced, and Max's list
  // message — "the first two numbers in the list are used". A single number
  // moves this inlet's end alone, exactly as a float on this inlet does; an
  // empty list changes nothing and falls through to the re-send a bang gives.
  float parsed[2];
  const int count = ExprParseFloatList(text, parsed, 2);
  if (count >= 1) StoreFirst(parsed[0]);
  if (count >= 2) StoreSecond(parsed[1]);
}

GUI_VALUE() {
  char lowText[kExprValueTextMax];
  char highText[kExprValueTextMax];
  const std::size_t lowLength = Render(Low(), lowText);
  const std::size_t highLength = Render(High(), highText);

  std::string out;
  out.reserve(lowLength + highLength + 1);
  out.assign(lowText, lowLength);
  out.push_back(' ');
  out.append(highText, highLength);
  return out;
}

GUI_VALUE_COUNT() {
  return 2;
}

GUI_VALUE_AT() {
  // Range-checked against the count rather than indexed, as the protocol
  // requires: past the end is "", never the whole state again.
  if (index > 1) return std::string();
  char text[kExprValueTextMax];
  const std::size_t length = Render(index == 0 ? Low() : High(), text);
  return std::string(text, length);
}

CALC() {
  // Sampled once, so all three outlets carry the same range even if a message
  // arriving down one of the cords moves an end while this send is in flight.
  const float lowValue = Low();
  const float highValue = High();

  char lowText[kExprValueTextMax];
  char highText[kExprValueTextMax];
  const std::size_t lowLength = Render(lowValue, lowText);
  const std::size_t highLength = Render(highValue, highText);

  // Filled immediately before the send rather than kept between sends: the send
  // path is synchronous, so a patch looping an outlet back into an inlet
  // re-enters here inside SendList, and a buffer filled any earlier would be
  // the inner message's by the time this one was read (.funnel's lesson). Into
  // memory reserved at construction, so nothing here allocates.
  listText.assign(lowText, lowLength);
  listText.push_back(' ');
  listText.append(highText, highLength);

  // Right to left, .trigger's ordering guarantee: the pair first, then the high
  // end, then the low end.
  outputs[2].SendList(listText, thread);
  outputs[1].SendFloat(highValue, thread);
  outputs[0].SendFloat(lowValue, thread);
}
