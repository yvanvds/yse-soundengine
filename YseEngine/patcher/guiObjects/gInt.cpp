
#include "gInt.h"
#include "../pObjectList.hpp"
#include "../../implementations/logImplementation.h"
#include "../pListArgs.h"
#include "../pSelector.h"
#include <cstddef>

using namespace YSE::PATCHER;

#define className gInt

namespace {

  // The bounds of one whitespace-separated token, found in place: a substr
  // here would allocate on whichever thread the message arrived on.
  bool NextToken(const std::string& text, std::size_t from, std::size_t& begin, std::size_t& end) {
    begin = from;
    while (begin < text.size() && IsSelectorSeparator(text[begin]))
      begin++;
    end = begin;
    while (end < text.size() && !IsSelectorSeparator(text[end]))
      end++;
    return end > begin;
  }

  // A token compared against a command word, without building a string to do
  // it.
  bool TokenIs(const std::string& text, std::size_t begin, std::size_t length, const char* word,
               std::size_t wordLength) {
    return length == wordLength && text.compare(begin, length, word, wordLength) == 0;
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

  ADD_OUT_INT;

  ADD_PARAM(value);

  value = 0;

  ADD_DESCRIPTION("Integer number box. Stores an int; inlet 0 sets-and-fires (bang re-emits the "
                  "current value), inlet 1 sets silently. Settable per the GUI value protocol "
                  "(issues #551/#846): a list of one number on inlet 0 is the whole state - the "
                  "exact string GetGuiValue() produces - and 'set 0 <value>' writes the one cell; "
                  "both read a decimal integer, truncating a fractional part, which is what lets "
                  ".preset capture and restore the number box.");
  ADD_CATEGORY(pCategory::GUI);
  INLET_DOC(0, "set/bang",
            "Sets the value and emits it; bang re-emits the current value. A list of one number "
            "is the whole state and 'set 0 <value>' the cell write.",
            "any int");
  INLET_DOC(1, "silent set", "Silently updates the stored value without emitting.", "any int");
  OUTLET_DOC(0, "out", "Current integer value.", "any int");
  PARAM_DOC("value", "0", "Initial value.", "any int");
}

INT_IN(SetInt) {
  this->value = value;
  // INTERNAL::LogImpl().emit(E_DEBUG, "Patcher: gInt input " + std::to_string(value));
}

FLOAT_IN(SetFloat) {
  this->value = (int)value;
  // INTERNAL::LogImpl().emit(E_DEBUG, "Patcher: gInt input " + std::to_string(value));
}

BANG_IN(Bang) {
  // nothing to do here, but needed to trigger output of value
}

LIST_IN(SetList) {
  // Registered on inlet 0 only, but routed anyway: the silent inlet must never
  // start taking lists because someone added a handler above.
  if (inlet != 0) return;
  (void)thread;

  std::size_t begin = 0;
  std::size_t end = 0;
  // An empty list addresses nothing and falls through to the re-send a bang
  // gives.
  if (!NextToken(value, 0, begin, end)) return;

  if (TokenIs(value, begin, end - begin, "set", 3)) {
    // "set <index> <value>" — issue #551's cell write. Read as decimal
    // integers rather than through a float, `.incdec`'s reason: a float has 24
    // bits of mantissa where an int has 31, so `set 0 2000000001` would not
    // survive the trip. Range-checked: any cell but the one there is, is
    // dropped, never folded onto cell 0.
    std::size_t offset = end;
    int cell = 0;
    int cellValue = 0;
    if (!ReadIntArgAt(value, offset, cell)) return;
    if (!ReadIntArgAt(value, offset, cellValue)) return;
    if (cell != 0) return;
    this->value = cellValue;
    return;
  }

  // The whole state, which is exactly the string GetGuiValue() produced. A
  // fractional part is truncated as it is everywhere in Max, and a token that
  // is not a number at all is ignored.
  std::size_t offset = begin;
  int number = 0;
  if (!ReadIntArgAt(value, offset, number)) return;
  this->value = number;
}

GUI_VALUE() {
  return std::to_string(value.load());
}

CALC() {
  outputs[0].SendInt(value, thread);
  // INTERNAL::LogImpl().emit(E_DEBUG, "Patcher: gInt output " + std::to_string(value));
}
