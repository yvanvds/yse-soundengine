#include "gSel.h"
#include "../pListArgs.h"
#include "../pObjectList.hpp"
#include <cstddef>
#include <string>

using namespace YSE::PATCHER;

#define className gSel

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
    return "Bang when the input matches selector " + std::string(digits, written) + " (" +
           selector +
           "). Exactly one outlet fires per input, and a selector repeated in the argument list "
           "bangs the leftmost of its outlets only.";
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
  ResetToDefaultSelector();
  ShapePorts();

  ADD_DESCRIPTION(
      "Bangs the outlet whose selector the input matches — Max's select, and with .trigger the "
      "backbone of message dispatch. One outlet per creation argument plus a rightmost "
      "pass-through, so a stream of numbers or symbols fans out into one branch per value: .route "
      "matches the leading symbol of a list and forwards the remainder, .split routes by range, "
      "and neither can branch on a bare number. A bare .sel has two outlets and matches the single "
      "number 0, which is Max's no-argument case. Exactly one outlet fires per input — the object "
      "looks like it might fan out and does not — and Max settles the only case where the question "
      "could arise: a selector repeated in the argument list bangs the leftmost of its outlets "
      "only. That is what spares this object the right-to-left firing order .mean and .peak have "
      "to respect. A match sends a bang and nothing else, since the outlet's position already "
      "carries the value; anything unmatched leaves the rightmost outlet unchanged and in its own "
      "type — an int as an int, a float as a float, a list as the same list, a bang as a bang — so "
      "a chain of .sel objects strings together with each reject outlet feeding the next inlet. On "
      "a match the rest of a list is dropped, as in Max; .route is the object that keeps the "
      "remainder. A selector is a number or a symbol, decided once when the argument is read, and "
      "the two never match each other: a number matches by value with an int widened to a float, "
      "so .sel 5 answers both the int 5 and the float 5.0; a symbol matches by exact text; a bang "
      "matches a selector spelled 'bang'; and a list is matched on its first element alone, which "
      "is a number if it reads as one and a symbol otherwise, so .sel 5 bangs for the list '5 6'. "
      "The comparison is exact. Max's matchfloat attribute, whose default of 0 makes select ignore "
      "floats altogether, has no meaning in a patcher with a single numeric type and reproducing "
      "it would silently drop half the messages the object is sent; Max's fuzzy attribute is a "
      "real convenience and a real hazard, since a tolerance makes .sel match values it was not "
      "given and two selectors closer together than the tolerance would both be right with the "
      "leftmost silently winning — put a .round on the way in, where the rounding is visible. What "
      "remains is Max's 'matchfloat 1, fuzzy 0.'. One consequence is worth knowing: a computed "
      "float may miss a selector it looks equal to, because 0.1 + 0.2 is not 0.3 in binary "
      "floating point, and a .round upstream is the fix. A NaN matches nothing at all and leaves "
      "the rightmost outlet; the patcher's usual 'read a non-finite as 0' substitution is "
      "deliberately not applied here, because it would make a stray NaN bang the outlet a patch "
      "wired for a real 0. Max's settable right inlet is reproduced with 'numeric' in place of "
      "'int': a single numeric selector gets the inlet, a single symbolic one does not, and two or "
      "more do not. The deviation is forced, because .sel 5 and .sel 5.0 are the same parameter "
      "string here so there is no int-ness left to test, and keying the inlet count off whether "
      "the argument text happens to contain a decimal point would make the object's shape depend "
      "on spelling a JSON round trip is free to normalise. The inlet is silent, takes an int or a "
      "float and replaces the value of the single selector. With more than one selector there is "
      "no way to change them, which is Max's answer too: the argument list is the object's shape, "
      "and changing it changes the outlet count. At most 256 selectors are held.");
  ADD_CATEGORY(pCategory::GENERIC);
  INLET_DOC(0, "in",
            "Bang, int, float or list to match against the selectors. A match bangs the "
            "corresponding outlet; anything else leaves the rightmost outlet unchanged and in its "
            "own type. A list is matched on its first element alone and the rest is dropped — "
            ".route is the object that keeps the remainder.",
            "any");
  PARAM_DOC("selectors", "0",
            "The values to match against, as a list. Each one gets an outlet, in order, and the "
            "rightmost outlet takes everything that matches none of them. A token that reads as a "
            "finite number is a numeric selector and matches by value; anything else is a symbol "
            "and matches by exact text. With no argument the object matches the single number 0. "
            "At most 256 are held.",
            "any list of numbers and symbols");
}

void gSel::ResetToDefaultSelector() {
  selectors.clear();
  selectors.push_back(Selector{"0", 0.f, true});
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
  for (std::size_t i = 0; i < selectors.size(); i++) {
    ADD_OUT_BANG;
    outputs.back().SetDoc(MatchLabel((int)i), MatchDoc((int)i, selectors[i].text),
                          selectors[i].text);
  }

  // Max: "will output non-matching messages out its right-most outlet". ANY,
  // because the input is passed on in whatever type it arrived as.
  ADD_OUT_ANY;
  outputs.back().SetDoc("rest",
                        "The input, unchanged and in its own type, when it matched no selector. "
                        "Chain this into the next .sel to carry on testing.",
                        "any");

  // Max's conditional right inlet, with numeric in place of int — see the
  // header for why the int-ness cannot survive this patcher's parameter model.
  const bool wantsValueInlet = selectors.size() == 1 && selectors[0].numeric;
  while (inputs.size() > 1)
    inputs.pop_back();

  if (wantsValueInlet) {
    ADD_IN_1;
    REG_INT_IN(SetInt);
    REG_FLOAT_IN(SetFloat);
    inputs.back().SetDoc("value",
                         "Replaces the value of the single selector, silently. Present only when "
                         "there is exactly one selector and it is a number, which is Max's rule.",
                         "any float");
  }
}

PARM_CLEAR() {
  // Runs on the control thread before the parameter string is re-read, and is
  // the whole of `SetParams("")`: Parameters::Set returns without calling the
  // parse callback for an empty argument, so this has to leave a usable object
  // behind rather than one with no selectors and a single outlet.
  selectorArgs.clear();
  ResetToDefaultSelector();
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
    if (ReadNumericToken(token.c_str(), token.size(), number)) {
      selectors.push_back(Selector{token, number, true});
    } else {
      selectors.push_back(Selector{token, 0.f, false});
    }
  }

  // An argument list of nothing but separators is a bare `.sel`.
  if (selectors.empty()) ResetToDefaultSelector();

  ShapePorts();
}

bool gSel::SelectorIsNumber(int index) const {
  if (index < 0 || index >= (int)selectors.size()) return false;
  return selectors[index].numeric;
}

float gSel::SelectorValue(int index) const {
  if (index < 0 || index >= (int)selectors.size()) return 0.f;
  if (!selectors[index].numeric) return 0.f;
  return selectors[index].value;
}

std::string gSel::SelectorText(int index) const {
  if (index < 0 || index >= (int)selectors.size()) return std::string();
  if (selectors[index].numeric) return std::string();
  return selectors[index].text;
}

int gSel::MatchNumber(float value) const {
  for (std::size_t i = 0; i < selectors.size(); i++) {
    if (!selectors[i].numeric) continue;
    // Exact, and exactness is the object — see the header on why Max's fuzzy
    // attribute is not ported. A NaN on either side compares unequal, which is
    // what sends it out the rightmost outlet.
    if (selectors[i].value == value) return (int)i;
  }
  return -1;
}

int gSel::MatchSymbol(const char* text, std::size_t length) const {
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

  // Max: "The bang message matches a 'bang' symbol in the arguments." With no
  // such selector the bang passes through, still a bang.
  const int hit = MatchSymbol("bang", 4);
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
    if (!selectors.empty()) selectors[0].value = (float)value;
    return;
  }
  if (inlet != 0) return;

  // Widened and compared as a float: this patcher has one numeric type, so
  // `.sel 5` has to answer the int 5 and the float 5.0 alike.
  const int hit = MatchNumber((float)value);
  if (hit >= 0) {
    outputs[hit].SendBang(thread);
    return;
  }
  outputs.back().SendInt(value, thread);
}

FLOAT_IN(SetFloat) {
  if (inlet == 1) {
    if (!selectors.empty()) selectors[0].value = value;
    return;
  }
  if (inlet != 0) return;

  const int hit = MatchNumber(value);
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
  // dropped — `.route` is the object that keeps the remainder.
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
    outputs[hit].SendBang(thread);
    return;
  }
  // Including a message with no first element at all: an empty message matches
  // nothing, and the reject outlet is where everything that matches nothing
  // goes.
  outputs.back().SendList(value, thread);
}
