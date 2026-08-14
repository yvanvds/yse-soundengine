#include "gFloat.h"
#include "../math/gExprEval.h"
#include "../pListArgs.h"
#include "../pObjectList.hpp"
#include "../pSelector.h"
#include <cstddef>

using namespace YSE::PATCHER;

#define className gFloat

namespace {

  // The bounds of the token starting at or after `from`, or false when there is
  // none. Walked in place rather than through substr: this runs on whichever
  // thread the message arrived on.
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

  // The next token that reads as a number, advancing `from` past it. Tokens
  // that are not numbers are skipped rather than ending the walk — the
  // family's policy.
  bool NextNumber(const char* text, std::size_t length, std::size_t& from, float& out) {
    std::size_t begin = 0;
    std::size_t end = 0;
    while (NextToken(text, length, from, begin, end)) {
      from = end;
      if (ReadNumericToken(text + begin, end - begin, out)) return true;
    }
    return false;
  }

  // Whether the `length` characters at `text` are exactly `word`, compared in
  // place for the same reason the tokens are walked in place.
  bool TokenIs(const char* text, std::size_t length, const char* word, std::size_t wordLength) {
    if (length != wordLength) return false;
    for (std::size_t i = 0; i < length; i++) {
      if (text[i] != word[i]) return false;
    }
    return true;
  }

} // namespace

CONSTRUCT() {
  ADD_IN_0;
  REG_INT_IN(SetInt);
  REG_FLOAT_IN(SetFloat);
  REG_BANG_IN(Bang);
  REG_LIST_IN(SetList);

  ADD_IN_1;
  REG_INT_IN(SetInt);
  REG_FLOAT_IN(SetFloat);

  ADD_OUT_FLOAT;

  ADD_PARAM(value);

  value = 0;

  ADD_DESCRIPTION("Float number box. Stores a float; inlet 0 sets-and-fires (bang re-emits the "
                  "current value), inlet 1 sets silently. Settable per the GUI value protocol "
                  "(issues #551/#846): a list of one number on inlet 0 is the whole state - the "
                  "exact string GetGuiValue() produces - and 'set 0 <value>' writes the one cell, "
                  "which is what lets .preset capture and restore the number box.");
  ADD_CATEGORY(pCategory::GUI);
  INLET_DOC(0, "set/bang",
            "Sets the value and emits it; bang re-emits the current value. A list of one number "
            "is the whole state and 'set 0 <value>' the cell write.",
            "any float");
  INLET_DOC(1, "silent set", "Silently updates the stored value without emitting.", "any float");
  OUTLET_DOC(0, "out", "Current float value.", "any float");
  PARAM_DOC("value", "0", "Initial value.", "any float");
}

FLOAT_IN(SetFloat) {
  this->value = value;
}

INT_IN(SetInt) {
  this->value = (float)value;
}

BANG_IN(Bang) {
  // nothing to do here, but needed to trigger output of value
}

LIST_IN(SetList) {
  // Registered on inlet 0 only, but routed anyway: the silent inlet must never
  // start taking lists because someone added a handler above.
  if (inlet != 0) return;
  (void)thread;

  const char* text = value.c_str();
  const std::size_t length = value.size();

  std::size_t begin = 0;
  std::size_t end = 0;
  // An empty list addresses nothing and falls through to the re-send a bang
  // gives.
  if (!NextToken(text, length, 0, begin, end)) return;

  if (TokenIs(text + begin, end - begin, "set", 3)) {
    // "set <index> <value>" — issue #551's cell write. Range-checked rather
    // than indexed: any cell but the one there is — or a NaN, which fails the
    // compare — is dropped, never folded onto cell 0.
    std::size_t cursor = end;
    float cell = 0.f;
    float cellValue = 0.f;
    if (!NextNumber(text, length, cursor, cell)) return;
    if (!NextNumber(text, length, cursor, cellValue)) return;
    if (!(cell >= 0.f)) return;
    if (ExprToInt(cell) != 0) return;
    this->value = cellValue;
    return;
  }

  float number = 0.f;
  if (!ReadNumericToken(text + begin, end - begin, number)) {
    // A leading token that is not a number addresses nothing. Dropped.
    return;
  }

  // The whole state, which is exactly the string GetGuiValue() produced.
  // Further numbers are ignored, `.rslider`'s policy for a longer list.
  this->value = number;
}

GUI_VALUE() {
  return std::to_string(value.load());
}

CALC() {
  outputs[0].SendFloat(value, thread);
}
