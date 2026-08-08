#include "gRoutePass.h"
#include "../pListArgs.h"
#include "../pObjectList.hpp"
#include <cstddef>
#include <string>

using namespace YSE::PATCHER;

#define className gRoutePass

namespace {

  // The separators Parameters::Set and the list outlets use. Hand-rolled rather
  // than std::isspace, which reads locale state another thread may be mutating
  // and is undefined for a negative char.
  bool IsSeparator(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
  }

  // "match0", "match1", ... — the label of a match outlet. Built through the
  // shared WriteInt rather than std::to_string; control-thread only either way,
  // but the patcher has one way of turning an int into text and this is it.
  std::string MatchLabel(int index) {
    char digits[FORMAT_INT_WIDTH];
    const std::size_t written = WriteInt(index, digits);
    return "match" + std::string(digits, written);
  }

  std::string MatchDoc(int index, const std::string& selector) {
    char digits[FORMAT_INT_WIDTH];
    const std::size_t written = WriteInt(index, digits);
    return "The whole message, unchanged and with its first item still on it, when that item "
           "matches selector " +
           std::string(digits, written) + " (" + selector +
           "). This is the difference from .route: the matched item is not consumed, so the "
           "downstream object still sees the tag it was routed by. Exactly one outlet fires per "
           "input, and a selector repeated in the argument list uses the leftmost of its outlets "
           "only.";
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
      "Routes a complete message by what its first item matches, and leaves that item on the "
      "message — Max's routepass, which 'routes a complete incoming message based on input "
      "matching' and 'does not strip off the matched portion of the message'. One outlet per "
      "creation argument plus a rightmost outlet for everything that matched none of them. "
      "Stripping is what makes .route a dispatcher: it turns 'note 60 100' into '60 100' on the "
      "note branch, so the branch works in bare values and never sees the word that got it there. "
      "That is right until the downstream object needs the tag too — a .s that re-tags the "
      "message, a .m that echoes it, a second .routepass further down matching on the same word, a "
      "bank keyed by name — because then the patch has to put the word back on, which means "
      "knowing it at the far end of the cord, in a second place, where it can disagree with the "
      "first. Here the branch is chosen by the tag and the tag stays. Its relatives: .sel matches "
      "the same way but sends a bang, dropping the value because the outlet's position is the "
      "whole answer, so it is the object to test with; .route is the stripping form, using the "
      "same matcher but consuming the first item where .routepass keeps it; .split routes by "
      "numeric range rather than by a match; and .gate, .switch and .router route by state the "
      "object holds rather than by "
      "anything in the message, where here the message chooses its own destination and nothing "
      "about the object changes as it does. The shape follows Max: 'the number of arguments "
      "determines the number of outlets, in addition to the rightmost outlet', so .routepass note "
      "ctl has three outlets and a bare .routepass has one, through which everything passes "
      "unchanged. That degenerate object is built rather than papered over with an invented "
      "default selector, which would put a branch in a patch that did not ask for one. The matcher "
      "is .sel's, deliberately — two objects whose only job is to branch must branch alike or a "
      "patch that swaps one for the other changes meaning silently. A selector is a number or a "
      "symbol, decided once when the argument is read, and the two never match each other: a "
      "number matches by value with an int widened to a float, so .routepass 5 answers both the "
      "int 5 and the float 5.0; a symbol matches by exact text; a bang matches a selector spelled "
      "'bang'; and a list is matched on its first element alone, which is a number if it reads as "
      "one and a symbol otherwise. The comparison is exact, so a computed float may miss a "
      "selector it looks equal to — 0.1 + 0.2 is not 0.3 in binary floating point, and a .round "
      "upstream is the fix — and a NaN matches nothing and leaves the rightmost outlet. A selector "
      "repeated in the argument list uses the leftmost of its outlets only, Max's rule for select "
      "and the only sane reading here, since an object that fired both would fan out and this one "
      "routes. Exactly one outlet fires per input, and what leaves it is whatever arrived, in the "
      "kind it arrived as and spelled as it was spelled: a matched list as the same list with its "
      "first item included, a matched int as that int, a matched bang as a bang. The rightmost "
      "outlet does the same for everything unmatched, so a chain of .routepass objects strings "
      "together with each reject outlet feeding the next inlet — and because nothing is consumed, "
      "every object in the chain sees the message the first one saw. At most 256 selectors are "
      "held. Calculate() does nothing, and no message path allocates, locks or blocks: matching is "
      "a bounded walk doing a float compare or a length-checked string compare against a character "
      "range, one Send follows, and a matched list is forwarded by reference rather than rebuilt.");
  ADD_CATEGORY(pCategory::GENERIC);
  INLET_DOC(0, "in",
            "Bang, int, float or list to route. The first item decides — a list's leading token, a "
            "bare number itself, or the symbol 'bang' — and the whole message, that item still on "
            "it, leaves the matching outlet in the kind it arrived as. Anything matching no "
            "selector leaves the rightmost outlet, equally unchanged.",
            "any");
  PARAM_DOC("selectors", "",
            "The values to match against, as a list. Each one gets an outlet, in order, and the "
            "rightmost outlet takes everything that matches none of them. A token that reads as a "
            "finite number is a numeric selector and matches by value; anything else is a symbol "
            "and matches by exact text. With no argument there are no match outlets at all, only "
            "the rightmost one, which is Max's 'the number of arguments determines the number of "
            "outlets, in addition to the rightmost outlet'. At most 256 are held.",
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

  for (std::size_t i = 0; i < selectors.size(); i++) {
    // ANY, because the object forwards whichever of bang, int, float and list
    // arrived rather than producing a kind of its own.
    ADD_OUT_ANY;
    outputs.back().SetDoc(MatchLabel((int)i), MatchDoc((int)i, selectors[i].text),
                          selectors[i].text);
  }

  // Max's rightmost outlet, present whatever the argument count — and on a bare
  // `.routepass` it is the only outlet there is.
  ADD_OUT_ANY;
  outputs.back().SetDoc("rest",
                        "The message, unchanged and in its own type, when it matched no selector. "
                        "Chain this into the next .routepass to carry on testing — nothing was "
                        "consumed, so the next object sees exactly what this one saw.",
                        "any");
}

PARM_CLEAR() {
  // Runs on the control thread before the parameter string is re-read, and is
  // the whole of `SetParams("")`: Parameters::Set returns without calling the
  // parse callback for an empty argument, so this has to leave Max's
  // no-argument object behind rather than one still holding the previous
  // selectors.
  selectorArgs.clear();
  selectors.clear();
  ShapePorts();
}

PARM_PARSE() {
  selectors.clear();
  for (const std::string& token : selectorArgs) {
    if (selectors.size() >= (std::size_t)MAX_SELECTORS) break;
    // Parameters::Set splits on single spaces, so a run of them yields empty
    // tokens. An empty selector could never be matched by anything and would
    // only cost an outlet nobody can reach.
    if (token.empty()) continue;

    float number = 0.f;
    // Strict on purpose, as the rest of the family is: ExprParseFloatList would
    // read `5abc` as 5 and fold `1e999` to 0, and neither answers "is this
    // selector a number at all".
    if (ReadNumericToken(token.c_str(), token.size(), number)) {
      selectors.push_back(Selector{token, number, true});
    } else {
      selectors.push_back(Selector{token, 0.f, false});
    }
  }

  ShapePorts();
}

bool gRoutePass::SelectorIsNumber(int index) const {
  if (index < 0 || index >= (int)selectors.size()) return false;
  return selectors[index].numeric;
}

float gRoutePass::SelectorValue(int index) const {
  if (index < 0 || index >= (int)selectors.size()) return 0.f;
  if (!selectors[index].numeric) return 0.f;
  return selectors[index].value;
}

std::string gRoutePass::SelectorText(int index) const {
  if (index < 0 || index >= (int)selectors.size()) return std::string();
  if (selectors[index].numeric) return std::string();
  return selectors[index].text;
}

int gRoutePass::MatchNumber(float value) const {
  for (std::size_t i = 0; i < selectors.size(); i++) {
    if (!selectors[i].numeric) continue;
    // Exact, and exactness is the matcher .sel already applies — see its header
    // on why Max's fuzzy attribute is not ported. A NaN on either side compares
    // unequal, which is what sends it out the rightmost outlet.
    if (selectors[i].value == value) return (int)i;
  }
  return -1;
}

int gRoutePass::MatchSymbol(const char* text, std::size_t length) const {
  for (std::size_t i = 0; i < selectors.size(); i++) {
    if (selectors[i].numeric) continue;
    if (selectors[i].text.size() != length) continue;
    // Compared against the character range in place: a substr here would
    // allocate on whichever thread the message arrived on.
    if (selectors[i].text.compare(0, length, text, length) == 0) return (int)i;
  }
  return -1;
}

BANG_IN(SetBang) {
  if (inlet != 0) return;

  // Max: "The bang message matches a 'bang' symbol in the arguments." It leaves
  // as a bang either way — there is nothing to strip from a bang, so the two
  // objects would agree here even once .route strips (#672); the outlet it
  // leaves by is the whole answer.
  const int hit = MatchSymbol("bang", 4);
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
  // itself is what leaves the outlet — where Max's `route 5` would emit a bang,
  // having consumed the only item there was.
  const int hit = MatchNumber((float)value);
  if (hit >= 0) {
    outputs[(std::size_t)hit].SendInt(value, thread);
    return;
  }
  outputs.back().SendInt(value, thread);
}

FLOAT_IN(SetFloat) {
  if (inlet != 0) return;

  const int hit = MatchNumber(value);
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
  // reference — the string sent is the one that arrived, first item and all.
  std::size_t begin = 0;
  while (begin < value.size() && IsSeparator(value[begin]))
    begin++;
  std::size_t end = begin;
  while (end < value.size() && !IsSeparator(value[end]))
    end++;

  int hit = -1;
  if (end > begin) {
    const std::size_t length = end - begin;
    float number = 0.f;
    // A leading token that reads as a finite number is a number — in Max the
    // first element of the list `5 6` is an int, and this patcher carries lists
    // as text. Anything else is a symbol.
    if (ReadNumericToken(value.c_str() + begin, length, number)) {
      hit = MatchNumber(number);
    } else {
      hit = MatchSymbol(value.c_str() + begin, length);
    }
  }

  if (hit >= 0) {
    outputs[(std::size_t)hit].SendList(value, thread);
    return;
  }
  // Including a message with no first element at all: an empty message matches
  // nothing, and the rightmost outlet is where everything that matches nothing
  // goes.
  outputs.back().SendList(value, thread);
}
