#include "gSwap.h"
#include "../pListArgs.h"
#include "../pObjectList.hpp"
#include "gExprEval.h"
#include <cstddef>
#include <string>

using namespace YSE::PATCHER;

#define className gSwap

namespace {

  // How many numbers Max's list method writes: the first into the left slot,
  // the second into the right one. Elements past these are dropped, as Max
  // drops them.
  constexpr int LIST_VALUES = 2;

} // namespace

CONSTRUCT() {
  // Inlet 0 is hot: it stores and then releases the pair. Inlet 1 only stores,
  // as the right inlet of every other two-inlet object here does.
  ADD_IN_0;
  REG_BANG_IN(SetLeftBang);
  REG_INT_IN(SetLeftInt);
  REG_FLOAT_IN(SetLeftFloat);
  REG_LIST_IN(SetLeftList);

  ADD_IN_1;
  REG_INT_IN(SetRightInt);
  REG_FLOAT_IN(SetRightFloat);

  // ANY rather than INT or FLOAT: a slot releases whichever of the two kinds it
  // was given, which is the whole of why this is one object and not Max's
  // `swap` / `fswap` pair. See the header.
  ADD_OUT_ANY;
  ADD_OUT_ANY;

  // The argument's *spelling* decides the kind, so it is read as text rather
  // than parsed straight into a float parameter. The clear callback is what
  // makes `SetParams("")` return the object to Max's no-argument shape rather
  // than leaving the previous value in place.
  REG_PARM_CLEAR;
  REG_PARM_PARSE;
  ADD_PARAM(argument);

  // With no argument the right slot holds int 0, which is also the shape
  // ClearParams() restores.
  ApplyArgument();

  ADD_DESCRIPTION(
      "Sends two numbers out in swapped order. A number on the left inlet leaves the "
      "right outlet, then the number stored on the right inlet leaves the left outlet.");
  ADD_CATEGORY(pCategory::MATH);
  INLET_DOC(0, "left",
            "Number: store it and send the pair. Bang: send the pair again. List of two numbers: "
            "store both and send. Leaves on the right outlet.",
            "any number");
  INLET_DOC(1, "right", "Number for the left outlet, stored without sending.", "any number");
  OUTLET_DOC(0, "fromRight",
             "The number from the right inlet, as the int or float it arrived as. Sent second.",
             "any number");
  OUTLET_DOC(1, "fromLeft",
             "The number from the left inlet, as the int or float it arrived as. Sent first.",
             "any number");
  PARAM_DOC("right", "0",
            "Starting number for the left outlet. Written as 5 it is an int; written as 5. or "
            "5e0 it is a float.",
            "any number");
}

bool gSwap::ReadValue(const char* text, std::size_t length, Value& slot) {
  float number = 0.f;
  // Strict on purpose, as the rest of the family is: ExprParseFloatList would
  // read `5abc` as 5 and fold `1e999` to 0, and neither answers "is this token
  // a number at all", which is the question both the argument and the list
  // method have to ask before storing anything.
  if (!ReadNumericToken(text, length, number)) return false;

  // The int-atom / float-atom test lives in pListArgs.h next to the reader that
  // agreed the token is a number; `.trigger` classifies its constants and
  // `.bondo` stores its tokens through the same answer.
  if (TokenLooksLikeFloat(text, length)) {
    slot.kind = Kind::FLOAT;
    slot.floatValue = number;
  } else {
    // Spelled as an int, so it is an int atom — but the token may still be
    // wider than an int (ReadNumericToken only promised a finite float), so
    // truncate through the range-checked conversion rather than casting.
    slot.kind = Kind::INT;
    slot.intValue = ExprToInt(number);
  }
  return true;
}

void gSwap::ApplyArgument() {
  // Max's no-argument object: the right slot holds int 0.
  right = Value();

  // Parameters::Set hands a STRING parameter exactly one token, so there is no
  // splitting to do here — a second argument is surplus and was already dropped
  // (Max documents one). A token that is not a number leaves the default.
  if (argument.empty()) return;
  ReadValue(argument.c_str(), argument.size(), right);
}

PARM_CLEAR() {
  // Runs on the control thread before the parameter string is re-read, and is
  // the whole of `SetParams("")`: Parameters::Set returns without calling the
  // parse callback for an empty argument, so this has to leave Max's
  // no-argument object behind rather than one still holding the previous value.
  argument.clear();
  ApplyArgument();
}

PARM_PARSE() {
  ApplyArgument();
}

void gSwap::Send(std::size_t index, const Value& value, YSE::THREAD thread) {
  // The forwarding rule in one place: the kind that arrived is the kind that
  // leaves. Two branches on a register — no allocation, no lock, no I/O.
  if (value.kind == Kind::INT) {
    outputs[index].SendInt(value.intValue, thread);
  } else {
    outputs[index].SendFloat(value.floatValue, thread);
  }
}

void gSwap::Emit(YSE::THREAD thread) {
  // Snapshotted before the first send rather than read out of the slots as we
  // go. The send path is synchronous and re-entrant, so a patch looping outlet
  // 1 back into inlet 1 arrives in SetRightInt *inside* the Send below; the
  // second outlet must still carry the value the release began with, or a
  // feedback cord would silently change what the same release sends.
  const Value fromLeft = left;
  const Value fromRight = right;

  // Max: "The number is sent out the right outlet, then the number in the right
  // inlet is sent out the left outlet." Right to left, and each Send returns
  // only once the whole subgraph behind that outlet has run — see the header on
  // why the order is a guarantee this object exists to exploit.
  Send(1, fromLeft, thread);
  Send(0, fromRight, thread);
}

BANG_IN(SetLeftBang) {
  if (inlet != 0) return;
  // Max: "Swaps and outputs the currently stored numbers." A replay — neither
  // slot moves, so a second bang sends the same pair again.
  Emit(thread);
}

INT_IN(SetLeftInt) {
  if (inlet != 0) return;
  left.kind = Kind::INT;
  left.intValue = value;
  Emit(thread);
}

FLOAT_IN(SetLeftFloat) {
  if (inlet != 0) return;
  // Kept as a float rather than truncated: this object forwards, and Max's
  // int-only `swap` exists solely because a Max outlet cannot carry both.
  left.kind = Kind::FLOAT;
  left.floatValue = value;
  Emit(thread);
}

LIST_IN(SetLeftList) {
  if (inlet != 0) return;

  // Read into a stack pair first: a list that turns out not to start with a
  // number must leave the slots exactly as they were, and a half-written pair
  // would be worse than an ignored message.
  Value parsed[LIST_VALUES];
  int read = 0;
  std::size_t i = 0;
  while (read < LIST_VALUES && i < value.size()) {
    while (i < value.size() && (value[i] == ' ' || value[i] == '\t'))
      i++;
    const std::size_t start = i;
    while (i < value.size() && value[i] != ' ' && value[i] != '\t')
      i++;
    if (i == start) break;
    // Stops at the first token that is not a number, so `1 hello` is the
    // one-number list `1` rather than an error or a guess.
    if (!ReadValue(value.c_str() + start, i - start, parsed[read])) break;
    read++;
  }

  // No number at the front at all: a word this object does not know, ignored
  // rather than treated as a release of the old pair.
  if (read == 0) return;

  // Max: "The numbers are stored in swap. The first number is sent out the
  // right outlet, then the second number is sent out the left outlet." A
  // one-number list is Max's int / float method and leaves the right slot alone.
  left = parsed[0];
  if (read == LIST_VALUES) right = parsed[1];
  Emit(thread);
}

INT_IN(SetRightInt) {
  if (inlet != 1) return;
  // Max: stored for output out the left outlet when inlet 0 next fires. Silent.
  right.kind = Kind::INT;
  right.intValue = value;
}

FLOAT_IN(SetRightFloat) {
  if (inlet != 1) return;
  right.kind = Kind::FLOAT;
  right.floatValue = value;
}
