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
      "Reverses the order of a pair of numbers — Max's swap, 'swaps the values of its inlets, "
      "preserving right-to-left ordering'. What arrives on inlet 0 leaves on outlet 1, and what "
      "was stored on inlet 1 leaves on outlet 0, so the pair crosses over on its way through. It "
      "earns a box because every two-inlet object in this patcher is hot on the left and cold on "
      "the right — .-, ./, .%, .pow, .atan2, .split and .past all fire on inlet 0 and merely store "
      "on inlet 1 — which fixes the operand order at the box; a patch whose values arrive the "
      "other way round would otherwise have to cross the two cords, an edit that is invisible once "
      "the patch is saved and hard to read back, or build the reordering by hand out of a .trigger "
      "and a cold inlet. .!- and .!/ are the fused form of the same idea for the two operators "
      "where reversal is common enough to be worth its own box, and cost one object where this "
      "costs two; .swap is the general answer, and the only one for the operators with no reversed "
      "twin. There is deliberately no .fswap: Max needs two boxes because a Max outlet carries one "
      "fixed atom type, and its reference says the only difference is that 'the second outlet's "
      "(and first inlet's) type is int or float depending on whether you use the name swap or "
      "fswap'. Here an outlet can be declared ANY and an inlet can take both, so this one object "
      "forwards each number as the kind it arrived as — an int in comes out an int, a float in "
      "comes out a float, on either side independently — which is swap's behaviour for swap's "
      "values and fswap's for fswap's, and a second box would differ in nothing. That is not the "
      "call .maximum and .minimum made when they became float-only: those objects compute, so the "
      "result's type would be a guess, while this one forwards, and widening an int to a float in "
      "transit would be a side effect visible to every downstream object that tells the two apart "
      "(.route 1, .sel, .i, .match) inflicted by a box whose only job was to change order. The "
      "creation argument pre-loads the right slot — Max: it 'sets the initial value sent from the "
      "left outlet' — and its spelling decides the kind, so .swap 5 starts holding int 5 and "
      ".swap 5. float 5, which is Max's 'a float argument causes float output from the left "
      "outlet' said the way this object can say it; the argument string is stored verbatim, so the "
      "distinction survives a save and reload. A bang on inlet 0 releases the current pair without "
      "moving either slot — a replay, so two bangs send the same thing twice rather than "
      "alternating — and before anything has arrived that is int 0 and the argument. A list on "
      "inlet 0 is Max's 'the numbers are stored in swap': the first number goes to the left slot "
      "and the second to the right one, and then the pair is released, which is the only way to "
      "set both in one message; each element keeps its own spelling, reading stops at the first "
      "token that is not a whole finite number, and a message with no number at the front is "
      "ignored rather than releasing the old pair. Outlet 1 is served first and outlet 0 last, "
      "each send completing in full (the whole subgraph behind it, depth first) before the next "
      "starts, which is Max's right-to-left rule and load-bearing here rather than decorative: the "
      "idiom is outlet 1 into the cold inlet of the arithmetic and outlet 0 into its hot one, so "
      "the right operand is in place before the left value fires the operation, and the reverse "
      "order would leave every result one input stale. The pair is snapshotted before the first "
      "send so a patch looping an outlet back into an inlet cannot change what the second outlet "
      "carries mid-release. Calculate() does nothing and no message path allocates, locks or "
      "blocks.");
  ADD_CATEGORY(pCategory::MATH);
  INLET_DOC(0, "left",
            "Hot. An int or float is stored and the pair is then released; a bang releases the "
            "pair without storing anything; a list of two numbers writes both slots and releases. "
            "Whatever arrives here leaves on outlet 1.",
            "any number");
  INLET_DOC(1, "right",
            "Cold — the value is stored for the next release and nothing is sent. Whatever arrives "
            "here leaves on outlet 0.",
            "any number");
  OUTLET_DOC(0, "fromRight",
             "The number stored from inlet 1 (or the creation argument), as the kind it was given "
             "in. Sent second, after outlet 1 has completed.",
             "any number");
  OUTLET_DOC(1, "fromLeft",
             "The number that arrived at inlet 0, as the kind it was given in. Sent first, before "
             "outlet 0.",
             "any number");
  PARAM_DOC("right", "0",
            "Max's argument: the initial value of the right slot, which is what outlet 0 sends "
            "before inlet 1 has been written. Spelled as an int it is an int ('5'), spelled with a "
            "decimal point or an exponent it is a float ('5.', '5e0'). Anything that is not a "
            "whole finite number is ignored and leaves the default int 0 in place.",
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
