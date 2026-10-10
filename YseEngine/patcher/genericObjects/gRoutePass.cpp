#include "gRoutePass.h"
#include "../pListArgs.h"
#include "../pObjectList.hpp"
#include <cstddef>
#include <string>

using namespace YSE::PATCHER;

#define className gRoutePass

namespace {

  std::string MatchDoc(int index, const std::string& selector) {
    char digits[FORMAT_INT_WIDTH];
    const std::size_t written = WriteInt(index, digits);
    return "The whole message, unchanged, when its first item matches selector " +
           std::string(digits, written) + " (" + selector + ").";
  }

} // namespace

CONSTRUCT() {
  // One inlet, as in Max. Every message kind, since Max routes them all through
  // `anything` and each of them has a first item to match on — a bang's being
  // the symbol `bang`.
  ADD_IN_0;
  REG_BANG_IN(SetBang);
  REG_INT_IN(SetInt);
  REG_FLOAT_IN(SetFloat);
  REG_LIST_IN(SetList);

  // The outlets are built from the selector table, so a saved `.routepass note
  // ctl` comes back with three of them. The clear callback is what makes
  // `SetParams("")` return the object to a bare `.routepass` rather than leaving
  // the previous argument list in place.
  REG_PARM_CLEAR;
  REG_PARM_PARSE;

  ADD_PARAM(selectorArgs);

  // Max's no-argument case: no selectors, and so the rightmost outlet alone.
  // Also the shape ClearParams() restores.
  ShapePorts();

  ADD_DESCRIPTION(
      "Sends a whole message out the outlet whose selector matches its first item, keeping that "
      "item on. A message that matches no selector leaves the rightmost outlet unchanged.");
  ADD_CATEGORY(pCategory::ROUTING);
  INLET_DOC(0, "in",
            "Bang, int, float or list. Its first item is matched against the selectors, and the "
            "message leaves unchanged.",
            "any");
  PARAM_DOC("selectors", "",
            "The selectors, one outlet each, at most 256. A number matches by value, anything else "
            "by exact text. With no argument there is only the rightmost outlet.",
            "any list of numbers and symbols");
}

void gRoutePass::ShapePorts() {
  // Rebuilt rather than resized, because the rightmost outlet has to stay
  // rightmost: appending a match outlet would put it after the pass-through and
  // every saved cord past that point would land on the wrong port. Safe because
  // every caller runs before the object is wired or published — the
  // constructor, and the two parameter callbacks, which
  // patcherImplementation::CreateObjectUnlocked runs before AssignGraphIds. A
  // *live* SetParams never reaches here on a published object: registering the
  // callbacks makes ParamsNeedRebuild() true, so #234 replaces the object.
  outputs.clear();

  for (std::size_t i = 0; i < selectors.Size(); i++) {
    // ANY, because the object forwards whichever of bang, int, float and list
    // arrived rather than producing a kind of its own.
    ADD_OUT_ANY;
    outputs.back().SetDoc(MatchOutletLabel((int)i), MatchDoc((int)i, selectors.Text(i)),
                          selectors.Text(i));
  }

  // Max's rightmost outlet, present whatever the argument count — and on a bare
  // `.routepass` it is the only outlet there is.
  ADD_OUT_ANY;
  outputs.back().SetDoc("rest",
                        "The message, unchanged, when it matched no selector. Wire it into another "
                        ".routepass to test further.",
                        "any");
}

PARM_CLEAR() {
  // Runs on the control thread before the parameter string is re-read, and is
  // the whole of `SetParams("")`: Parameters::Set returns without calling the
  // parse callback for an empty argument, so this has to leave Max's
  // no-argument object behind rather than one still holding the previous
  // selectors.
  selectorArgs.clear();
  selectors.Clear();
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

    // Numeric or symbolic is decided by the shared table's strict reader, as
    // the rest of the family is: ExprParseFloatList would read `5abc` as 5 and
    // fold `1e999` to 0, and neither answers "is this selector a number at
    // all".
    selectors.Add(token);
  }

  ShapePorts();
}

bool gRoutePass::SelectorIsNumber(int index) const {
  return selectors.IsNumber(index);
}

float gRoutePass::SelectorValue(int index) const {
  return selectors.Value(index);
}

std::string gRoutePass::SelectorText(int index) const {
  return selectors.SymbolText(index);
}

BANG_IN(SetBang) {
  if (inlet != 0) return;

  // Max: "The bang message matches a 'bang' symbol in the arguments." It leaves
  // as a bang either way — there is nothing to strip from a bang, so this is
  // the one case where .route agrees with this object; the outlet it leaves by
  // is the whole answer.
  const int hit = selectors.MatchBang();
  if (hit >= 0) {
    outputs[(std::size_t)hit].SendBang(thread);
    return;
  }
  outputs.back().SendBang(thread);
}

INT_IN(SetInt) {
  if (inlet != 0) return;

  // Widened and compared as a float: this patcher has one numeric type, so
  // `.routepass 5` has to answer the int 5 and the float 5.0 alike. The int
  // itself is what leaves the outlet — where `.route 5` emits a bang, having
  // consumed the only item there was.
  const int hit = selectors.MatchNumber((float)value);
  if (hit >= 0) {
    outputs[(std::size_t)hit].SendInt(value, thread);
    return;
  }
  outputs.back().SendInt(value, thread);
}

FLOAT_IN(SetFloat) {
  if (inlet != 0) return;

  const int hit = selectors.MatchNumber(value);
  if (hit >= 0) {
    outputs[(std::size_t)hit].SendFloat(value, thread);
    return;
  }
  outputs.back().SendFloat(value, thread);
}

LIST_IN(SetList) {
  if (inlet != 0) return;

  // Max: "If the first item of the message is the same as one of the arguments
  // of routepass, the entire message is sent out the specified outlet." Only
  // the first element is ever examined, and the message is forwarded by
  // reference — the string sent is the one that arrived, first item and all, so
  // this object has no use for the offset the matched item ends at. Wanting
  // that offset is `.route`'s whole difference from this one.
  const int hit = selectors.MatchLeadingToken(value);

  if (hit >= 0) {
    outputs[(std::size_t)hit].SendList(value, thread);
    return;
  }
  // Including a message with no first element at all: an empty message matches
  // nothing, and the rightmost outlet is where everything that matches nothing
  // goes.
  outputs.back().SendList(value, thread);
}
