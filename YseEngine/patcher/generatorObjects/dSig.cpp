#include "dSig.h"
#include "../pListArgs.h"
#include "../pSelector.h"

using namespace YSE::PATCHER;

#define className dSig

CONSTRUCT_DSP() {
  for (aFlt& v : values)
    v = 0.f;

  REG_PARM_CLEAR;
  REG_PARM_PARSE;
  ADD_PARAM(creationArgs);

  ShapePorts();

  ADD_DESCRIPTION("Turns numbers into constant audio signals: one outlet per argument, each "
                  "holding its value until a number changes it.");
  ADD_CATEGORY(pCategory::OSC);
  PARAM_DOC("values", "0",
            "The starting value of each outlet, one argument per outlet (at most 64). The number "
            "of arguments is the number of outlets.",
            "one or more numbers");
}

void dSig::ShapePorts() {
  // Rebuilt rather than resized: the outlet count comes from the arguments.
  // Every caller runs before the object is wired or published.
  int count = 0;
  for (const std::string& token : creationArgs) {
    float number = 0.f;
    if (token.empty() || !ReadNumericToken(token, number)) continue;
    if (count >= MAX_OUTLETS) break;
    values[static_cast<std::size_t>(count)] = number;
    count++;
  }
  if (count == 0) {
    values[0] = 0.f;
    count = 1;
  }

  inputs.clear();
  outputs.clear();
  signals.clear();
  signals.resize(static_cast<std::size_t>(count));

  ADD_IN_0;
  REG_SIGNAL_IN;
  REG_FLOAT_IN(SetFloat);
  REG_INT_IN(SetInt);
  REG_LIST_IN(SetList);
  INLET_DOC(0, "in",
            "A number sets the leftmost outlet; a list sets the outlets from the leftmost on; "
            "'set <index> <value>' sets one outlet (0 is the leftmost). A connected signal is "
            "ignored and only makes ~sig run after its source.",
            "any float");

  for (int i = 0; i < count; i++) {
    ADD_OUT_BUFFER;
    const std::string n = std::to_string(i);
    outputs.back().SetDoc("out " + n, "Constant signal holding the value of outlet " + n + ".",
                          "any float");
  }
}

PARM_CLEAR() {
  creationArgs.clear();
  ShapePorts();
}

PARM_PARSE() {
  ShapePorts();
}

FLOAT_IN(SetFloat) {
  (void)inlet;
  (void)thread;
  values[0] = value;
}

INT_IN(SetInt) {
  (void)inlet;
  (void)thread;
  values[0] = static_cast<float>(value);
}

// A list of numbers sets the outlets from the leftmost on; "set <index>
// <value>" sets one. Walked in place so nothing allocates on whichever thread
// delivers it.
LIST_IN(SetList) {
  (void)inlet;
  (void)thread;
  const char* text = value.c_str();
  const std::size_t length = value.size();
  const int count = OutletCount();

  const auto next = [&](std::size_t& pos, std::size_t& end) {
    while (pos < length && IsSelectorSeparator(text[pos]))
      pos++;
    end = pos;
    while (end < length && !IsSelectorSeparator(text[end]))
      end++;
    return end > pos;
  };

  std::size_t pos = 0;
  std::size_t end = 0;
  if (!next(pos, end)) return;

  if (end - pos == 3 && value.compare(pos, 3, "set") == 0) {
    float index = 0.f;
    float number = 0.f;
    pos = end;
    if (!next(pos, end) || !ReadNumericToken(text + pos, end - pos, index)) return;
    pos = end;
    if (!next(pos, end) || !ReadNumericToken(text + pos, end - pos, number)) return;
    const int at = static_cast<int>(index);
    if (index < 0.f || at >= count || static_cast<float>(at) != index) return;
    values[static_cast<std::size_t>(at)] = number;
    return;
  }

  int outlet = 0;
  do {
    float number = 0.f;
    if (ReadNumericToken(text + pos, end - pos, number)) {
      values[static_cast<std::size_t>(outlet)] = number;
    }
    outlet++;
    pos = end;
  } while (outlet < count && next(pos, end));
}

CALC() {
  // The inlet's signal, if any, is ignored: it only orders the graph.
  const int count = OutletCount();
  for (int i = 0; i < count; i++) {
    const auto at = static_cast<std::size_t>(i);
    signals[at] = values[at].load();
    outputs[at].SendBuffer(&signals[at], thread);
  }
}

#undef className
