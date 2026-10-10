#include "gTextEdit.h"
#include "../../implementations/logImplementation.h"
#include "../math/gExprEval.h"
#include "../pObjectList.hpp"

#include <cstring>

using namespace YSE::PATCHER;
#define className gTextEdit

CONSTRUCT() {
  ADD_IN_0;
  REG_BANG_IN(EmitStored);
  REG_INT_IN(StoreInt);
  REG_FLOAT_IN(StoreFloat);
  REG_LIST_IN(StoreList);

  ADD_OUT_LIST;

  ADD_PARAM(initial);
  REG_PARM_CLEAR;
  REG_PARM_PARSE;

  // The one allocation the send path would otherwise need, taken here on the
  // control thread for the longest text the cell can hold.
  emitScratch.reserve(TEXT_CAPACITY);

  ADD_DESCRIPTION(
      "An editable string. A list on the inlet becomes the text, verbatim and with no reserved "
      "words, and goes straight back out; an int or a float becomes the text that spells it; a "
      "bang re-sends what is held. The value a host polls and pushes back through GetGuiValue, so "
      "a text field a user types into is one object. Text longer than 256 characters is refused "
      "and what is stored is kept. Put a .tosymbol after it to collapse the text into a single "
      "token, or a .fromsymbol to read it back as a number.");
  ADD_CATEGORY(pCategory::GUI);
  INLET_DOC(0, "in",
            "A list becomes the text and is sent on; an int or a float becomes the text that "
            "spells it; a bang re-sends what is held. Nothing is a keyword — any word the object "
            "reserved would be a word the field could not contain. An empty list clears it.",
            "any text up to 256 characters");
  OUTLET_DOC(0, "out",
             "The text, on every message that arrives. Sent as a list and never re-typed: a field "
             "holding \"440\" sends the text, not the number.",
             "");
  PARAM_DOC("text", "",
            "The text the field starts at — every argument joined with single spaces, so no word "
            "is a keyword here either. Live state is not saved with it; that is what GetGuiValue "
            "and the inlet are for.",
            "any text up to 256 characters");
}

// A re-parse must not leave the previous text standing: Parameters::Set() calls
// this before parsing, so this is what makes SetParams("") a real reset — an
// empty field — rather than a no-op that leaves the object on its old text.
PARM_CLEAR() {
  initial.clear();
  StoreText("", 0);
}

PARM_PARSE() {
  // Control thread, and on an object the audio thread cannot see yet (a live
  // SetParams on a published object is a #234 rebuild), so building the joined
  // text here is free of the constraints the message path has.
  std::string joined;
  for (std::size_t i = 0; i < initial.size(); i++) {
    if (i > 0) joined += ' ';
    joined += initial[i];
  }

  // Loud here where it is silent on the inlet: this is a creation argument a
  // patch author wrote, on the control thread, and a field that quietly came up
  // empty would be blamed on the object rather than on the argument.
  if (joined.size() > TEXT_CAPACITY) {
    INTERNAL::LogImpl().emit(E_ERROR, std::string("patcher: ") + Type() +
                                          " initial text is longer than 256 characters; ignored");
    return;
  }
  StoreText(joined.c_str(), joined.size());
}

bool gTextEdit::StoreText(const char* text, std::size_t length) {
  // Refused rather than truncated: half a string is a different string, and the
  // text a patch is reading must not silently become one. Silent because this
  // may be the audio thread.
  if (length > TEXT_CAPACITY) return false;

  const textCellGuard guard(busy, textCellGuard::mode::Try);
  if (!guard.Held()) return false;
  std::memcpy(cell, text, length);
  cell[length] = '\0';
  cellLength = length;
  return true;
}

void gTextEdit::Emit(YSE::THREAD thread) {
  {
    const textCellGuard guard(busy, textCellGuard::mode::Try);
    if (!guard.Held()) return;
    // Reserved to TEXT_CAPACITY in the constructor, so this refills the buffer
    // rather than allocating one.
    emitScratch.assign(cell, cellLength);
  }
  // Outside the guard on purpose: the send runs the whole downstream graph,
  // which may well store back into this same object, and inside the guard that
  // store would be the one thing the try-lock drops.
  outputs[0].SendList(emitScratch, thread);
}

void gTextEdit::StoreAndEmit(const char* text, std::size_t length, YSE::THREAD thread) {
  StoreText(text, length);
  // Emitted even when the store was refused or dropped: inlet 0 is hot, and a
  // message that changes nothing still emits — the re-send a bang performs.
  Emit(thread);
}

std::string gTextEdit::Text() {
  const textCellGuard guard(busy, textCellGuard::mode::Wait);
  return std::string(cell, cellLength);
}

// The protocol's whole-state read (issue #551) and, for this object, the whole
// of it: one cell, so pObject supplies the count and the per-cell read.
//
// This is the one reader that **waits** for the guard instead of dropping. It
// runs on the host thread, where waiting is allowed and the audio thread never
// does; and it is the only way a live value leaves this object, so a "" on
// contention would not be a dropped repaint — it would be a `.preset` snapshot
// silently recording an empty field. The wait is bounded: see textCellGuard.
GUI_VALUE() {
  return Text();
}

BANG_IN(EmitStored) {
  Emit(thread);
}

INT_IN(StoreInt) {
  // ExprFormatValue is the patcher's number-to-text writer: no allocation, no
  // locale (so the separator is always '.', which is what a reader on the other
  // end expects) and no exception.
  char text[kExprValueTextMax];
  const int length = ExprFormatValue(ExprValue::Int(value), text, kExprValueTextMax);
  StoreAndEmit(text, static_cast<std::size_t>(length), thread);
}

FLOAT_IN(StoreFloat) {
  // A float keeps its decimal point, so a field filled from a `.f` still reads
  // back as the float it was.
  char text[kExprValueTextMax];
  const int length = ExprFormatValue(ExprValue::Float(value), text, kExprValueTextMax);
  StoreAndEmit(text, static_cast<std::size_t>(length), thread);
}

LIST_IN(StoreList) {
  // Verbatim, whatever it says — no keyword is read out of it first. See the
  // class comment: any word this object reserved would be a word the field
  // could not hold.
  StoreAndEmit(value.c_str(), value.size(), thread);
}

#undef className
