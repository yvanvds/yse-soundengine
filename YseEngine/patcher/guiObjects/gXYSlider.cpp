#include "gXYSlider.h"
#include <cstring>

using namespace YSE::PATCHER;

#define className gXYSlider

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

  // Cold, so y written here is stored without firing the outlets — the
  // patcher's hot/cold rule. Registering the same two handlers: they route on
  // `inlet`.
  ADD_IN_1;
  REG_INT_IN(IntIn);
  REG_FLOAT_IN(FloatIn);

  ADD_OUT_FLOAT; // 0: x
  ADD_OUT_FLOAT; // 1: y
  ADD_OUT_LIST; // 2: the position as a pair

  x = 0.f;
  y = 0.f;

  // In place before Register() hands the fields to the parameter system: a
  // creation argument overwrites them, no argument leaves them alone. 0-127
  // per axis is Max's default size of 128, counted from 0.
  xminimum = 0.f;
  xmaximum = 127.f;
  yminimum = 0.f;
  ymaximum = 127.f;

  ADD_PARAM(xminimum);
  ADD_PARAM(xmaximum);
  ADD_PARAM(yminimum);
  ADD_PARAM(ymaximum);

  listText.reserve(LIST_CAPACITY);

  ADD_DESCRIPTION("A two-dimensional pad holding an x and a y, each inside its own bounds, so two "
                  "parameters move as one gesture. It sends x, y and the pair as a list.");
  ADD_CATEGORY(pCategory::GUI);
  INLET_DOC(0, "x/position",
            "A number sets x; a list 'x y' sets both; 'set <0|1> <value>' sets x or y; a bang "
            "sends again. Everything here sends.",
            "xminimum-xmaximum");
  INLET_DOC(1, "y", "Sets y without sending.", "yminimum-ymaximum");
  OUTLET_DOC(0, "x", "The x value. Sent last.", "xminimum-xmaximum");
  OUTLET_DOC(1, "y", "The y value.", "yminimum-ymaximum");
  OUTLET_DOC(2, "position", "Both values as the list 'x y'. Sent first.", "per-axis bounds");
  PARAM_DOC("xminimum", "0", "The lowest value of x.", "any float");
  PARAM_DOC("xmaximum", "127", "The highest value of x.", "any float");
  PARAM_DOC("yminimum", "0", "The lowest value of y.", "any float");
  PARAM_DOC("ymaximum", "127", "The highest value of y.", "any float");
}

float gXYSlider::Bound(float value, float boundA, float boundB) {
  // The bounds are taken as an ordered pair, .rslider's rule, so a reversed
  // argument pair still bounds against the right two numbers. A NaN fails
  // both compares and is answered with the lower bound rather than passed
  // through, since every value this object reports must be inside the range
  // it documents.
  const float lowBound = boundA < boundB ? boundA : boundB;
  const float highBound = boundA < boundB ? boundB : boundA;
  if (!(value >= lowBound)) return lowBound;
  if (value > highBound) return highBound;
  return value;
}

float gXYSlider::BoundX(float value) const {
  return Bound(value, xminimum, xmaximum);
}

float gXYSlider::BoundY(float value) const {
  return Bound(value, yminimum, ymaximum);
}

float gXYSlider::X() const {
  return BoundX(x.load());
}

float gXYSlider::Y() const {
  return BoundY(y.load());
}

void gXYSlider::StoreX(float value) {
  x = BoundX(value);
}

void gXYSlider::StoreY(float value) {
  y = BoundY(value);
}

std::size_t gXYSlider::Render(float value, char* out) {
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
    StoreX(value);
  } else {
    StoreY(value);
  }
}

LIST_IN(ListIn) {
  // Registered on inlet 0 only, but routed anyway: a later inlet must never
  // start writing x because someone added a handler above.
  if (inlet != 0) return;

  const char* text = value.c_str();
  while (*text == ' ' || *text == '\t')
    text++;

  if (IsSetKeyword(text)) {
    // "set <index> <value>" — one cell. The keyword is what tells this from
    // the whole-state form, which for a two-cell control is the same two
    // numbers (issue #551).
    float parsed[2];
    if (ExprParseFloatList(text + 3, parsed, 2) < 2) return;
    // Range-checked rather than indexed: an index outside the two cells — or a
    // NaN, which fails the first compare — is dropped, never folded onto a
    // real axis. A fractional index truncates towards zero, as every other
    // index in the patcher does.
    const float index = parsed[0];
    if (!(index >= 0.f) || index >= 2.f) return;
    if ((int)index == 0) {
      StoreX(parsed[1]);
    } else {
      StoreY(parsed[1]);
    }
    return;
  }

  // Whole state: the exact string GetGuiValue() produced — "x y", the first
  // two numbers. A single number moves x alone, exactly as a float on this
  // inlet does; an empty list changes nothing and falls through to the
  // re-send a bang gives.
  float parsed[2];
  const int count = ExprParseFloatList(text, parsed, 2);
  if (count >= 1) StoreX(parsed[0]);
  if (count >= 2) StoreY(parsed[1]);
}

GUI_VALUE() {
  char xText[kExprValueTextMax];
  char yText[kExprValueTextMax];
  const std::size_t xLength = Render(X(), xText);
  const std::size_t yLength = Render(Y(), yText);

  std::string out;
  out.reserve(xLength + yLength + 1);
  out.assign(xText, xLength);
  out.push_back(' ');
  out.append(yText, yLength);
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
  const std::size_t length = Render(index == 0 ? X() : Y(), text);
  return std::string(text, length);
}

CALC() {
  // Sampled once, so all three outlets carry the same position even if a
  // message arriving down one of the cords moves an axis while this send is
  // in flight.
  const float xValue = X();
  const float yValue = Y();

  char xText[kExprValueTextMax];
  char yText[kExprValueTextMax];
  const std::size_t xLength = Render(xValue, xText);
  const std::size_t yLength = Render(yValue, yText);

  // Filled immediately before the send rather than kept between sends: the
  // send path is synchronous, so a patch looping an outlet back into an inlet
  // re-enters here inside SendList, and a buffer filled any earlier would be
  // the inner message's by the time this one was read (.funnel's lesson). Into
  // memory reserved at construction, so nothing here allocates.
  listText.assign(xText, xLength);
  listText.push_back(' ');
  listText.append(yText, yLength);

  // Right to left, .trigger's ordering guarantee: the pair first, then y,
  // then x.
  outputs[2].SendList(listText, thread);
  outputs[1].SendFloat(yValue, thread);
  outputs[0].SendFloat(xValue, thread);
}
