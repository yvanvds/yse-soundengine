#include "gSel.h"
#include "../pListArgs.h"
#include "../pObjectList.hpp"
#include <cstddef>
#include <string>

using namespace YSE::PATCHER;

#define className gSel

namespace {

  std::string MatchDoc(int index, const std::string& selector) {
    char digits[FORMAT_INT_WIDTH];
    const std::size_t written = WriteInt(index, digits);
    return "A bang when the input matches selector " + std::string(digits, written) + " (" +
           selector + ").";
  }

} // namespace

CONSTRUCT() {
  // The hot inlet. Every message type Max routes through `anything`, which is
  // all of them.
  ADD_IN_0;
  REG_BANG_IN(SetBang);
  REG_INT_IN(SetInt);
  REG_FLOAT_IN(SetFloat);
  REG_LIST_IN(SetList);

  // The outlets and the conditional right inlet are built from the selector
  // table, so a saved `.sel 1 2 3` comes back with four outlets. The clear
  // callback is what makes `SetParams("")` return the object to a bare `.sel`
  // rather than leaving the previous argument list in place.
  REG_PARM_CLEAR;
  REG_PARM_PARSE;

  ADD_PARAM(selectorArgs);

  // Max's no-argument case: "there is only one other outlet, which is assigned
  // the integer number 0". ShapePorts() turns that into two outlets and the
  // right inlet, which is also the shape ClearParams() restores.
  selectors.ResetToZero();
  ShapePorts();

  ADD_DESCRIPTION(
      "Bangs the outlet whose selector matches the input. Anything that matches no selector "
      "leaves the rightmost outlet unchanged.");
  ADD_CATEGORY(pCategory::ROUTING);
  INLET_DOC(0, "in",
            "Bang, int, float or list to match. A list is matched on its first item, and the rest "
            "is dropped on a match.",
            "any");
  PARAM_DOC("selectors", "0",
            "The selectors, one outlet each, at most 256. A number matches by value, anything else "
            "by exact text. With no argument the object matches 0.",
            "any list of numbers and symbols");
}

void gSel::ShapePorts() {
  // Rebuilt rather than resized, because the rightmost outlet has a different
  // type from the match outlets and appending would put a bang outlet where the
  // pass-through belongs. Safe because every caller runs before the object is
  // wired or published: the constructor, and the two parameter callbacks, which
  // patcherImplementation::CreateObjectUnlocked runs before AssignGraphIds. A
  // *live* SetParams never reaches here on a published object — registering the
  // callbacks makes ParamsNeedRebuild() true, so #234 replaces the object.
  outputs.clear();
  for (std::size_t i = 0; i < selectors.Size(); i++) {
    ADD_OUT_BANG;
    outputs.back().SetDoc(MatchOutletLabel((int)i), MatchDoc((int)i, selectors.Text(i)),
                          selectors.Text(i));
  }

  // Max: "will output non-matching messages out its right-most outlet". ANY,
  // because the input is passed on in whatever type it arrived as.
  ADD_OUT_ANY;
  outputs.back().SetDoc("rest",
                        "The input, unchanged, when it matched no selector. Wire it into another "
                        ".sel to test further.",
                        "any");

  // Max's conditional right inlet, with numeric in place of int — see the
  // header for why the int-ness cannot survive this patcher's parameter model.
  const bool wantsValueInlet = selectors.Size() == 1 && selectors.IsNumber(0);
  while (inputs.size() > 1)
    inputs.pop_back();

  if (wantsValueInlet) {
    ADD_IN_1;
    REG_INT_IN(SetInt);
    REG_FLOAT_IN(SetFloat);
    inputs.back().SetDoc("value",
                         "Replaces the selector's value without sending anything. Present only "
                         "when there is exactly one selector and it is a number.",
                         "any float");
  }
}

PARM_CLEAR() {
  // Runs on the control thread before the parameter string is re-read, and is
  // the whole of `SetParams("")`: Parameters::Set returns without calling the
  // parse callback for an empty argument, so this has to leave a usable object
  // behind rather than one with no selectors and a single outlet.
  selectorArgs.clear();
  selectors.ResetToZero();
  ShapePorts();
}

PARM_PARSE() {
  selectors.Clear();
  for (const std::string& token : selectorArgs) {
    if (selectors.Size() >= (std::size_t)MAX_SELECTORS) break;
    // Parameters::Set splits on single spaces, so a run of them yields empty
    // tokens. An empty selector could never be matched by anything and would
    // only cost an outlet nobody can reach.
    if (token.empty()) continue;

    selectors.Add(token);
  }

  // An argument list of nothing but separators is a bare `.sel`.
  if (selectors.Empty()) selectors.ResetToZero();

  ShapePorts();
}

bool gSel::SelectorIsNumber(int index) const {
  return selectors.IsNumber(index);
}

float gSel::SelectorValue(int index) const {
  return selectors.Value(index);
}

std::string gSel::SelectorText(int index) const {
  return selectors.SymbolText(index);
}

BANG_IN(SetBang) {
  if (inlet != 0) return;

  // Max: "The bang message matches a 'bang' symbol in the arguments." With no
  // such selector the bang passes through, still a bang.
  const int hit = selectors.MatchBang();
  if (hit >= 0) {
    outputs[hit].SendBang(thread);
    return;
  }
  outputs.back().SendBang(thread);
}

INT_IN(SetInt) {
  if (inlet == 1) {
    // The cold inlet exists only when selector 0 is the object's single numeric
    // selector, so there is exactly one thing a number here can mean. Silent.
    selectors.SetValue(0, (float)value);
    return;
  }
  if (inlet != 0) return;

  // Widened and compared as a float: this patcher has one numeric type, so
  // `.sel 5` has to answer the int 5 and the float 5.0 alike.
  const int hit = selectors.MatchNumber((float)value);
  if (hit >= 0) {
    outputs[hit].SendBang(thread);
    return;
  }
  outputs.back().SendInt(value, thread);
}

FLOAT_IN(SetFloat) {
  if (inlet == 1) {
    selectors.SetValue(0, value);
    return;
  }
  if (inlet != 0) return;

  const int hit = selectors.MatchNumber(value);
  if (hit >= 0) {
    outputs[hit].SendBang(thread);
    return;
  }
  outputs.back().SendFloat(value, thread);
}

LIST_IN(SetList) {
  if (inlet != 0) return;

  // Max: "if the first element in the list matches the object argument(s)".
  // Only the first element is ever examined, and on a match the rest is
  // dropped — `.route` is the object that keeps the remainder, and it is the
  // overload of this call that hands the offset back which lets it.
  const int hit = selectors.MatchLeadingToken(value);

  if (hit >= 0) {
    outputs[hit].SendBang(thread);
    return;
  }
  // Including a message with no first element at all: an empty message matches
  // nothing, and the reject outlet is where everything that matches nothing
  // goes.
  outputs.back().SendList(value, thread);
}
