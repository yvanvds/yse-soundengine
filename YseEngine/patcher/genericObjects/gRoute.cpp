#include "gRoute.h"
#include "../pListArgs.h"
#include "../pObjectList.hpp"
#include <cstddef>
#include <string>

using namespace YSE::PATCHER;
#define className gRoute

namespace {

  // The separators Parameters::Set and the list outlets use. Hand-rolled rather
  // than std::isspace, which reads locale state another thread may be mutating
  // and is undefined for a negative char. The same helper .sel, .routepass and
  // .match keep.
  bool IsSeparator(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
  }

  // A remainder that is a whole number is only sent as an int when the int can
  // hold it: casting a float outside the int range is undefined behaviour, and
  // a patch that spelled a huge integer is better served by the float that
  // still carries its value. -2^31 and 2^31 are both exactly representable, so
  // this comparison is exact.
  bool FitsInt(float value) {
    return value >= -2147483648.f && value < 2147483648.f;
  }

  // "match0", "match1", ... — the label of a match outlet. Built through the
  // shared WriteInt rather than std::to_string; control-thread only either way,
  // but the patcher has one way of turning an int into text and this is it. The
  // labels .sel and .routepass give the same outlets.
  std::string MatchLabel(int index) {
    char digits[FORMAT_INT_WIDTH];
    const std::size_t written = WriteInt(index, digits);
    return "match" + std::string(digits, written);
  }

  std::string MatchDoc(int index, const std::string& selector) {
    char digits[FORMAT_INT_WIDTH];
    const std::size_t written = WriteInt(index, digits);
    return "What is left of the message once its first item is taken off, when that item matches "
           "selector " +
           std::string(digits, written) + " (" + selector +
           "). A remainder of two or more items leaves as a list, a remainder of one number as "
           "that int or float, a remainder of one symbol as a one-element list, and nothing left "
           "at all as a bang. Exactly one outlet fires per input, and a selector repeated in the "
           "argument list uses the leftmost of its outlets only.";
  }

} // namespace

CONSTRUCT() {
  ADD_IN_0;
  REG_BANG_IN(SetBangValue);
  REG_INT_IN(SetIntValue);
  REG_FLOAT_IN(SetFloatValue);
  REG_LIST_IN(SetListValue);

  REG_PARM_CLEAR;
  REG_PARM_PARSE;

  ADD_PARAM(list);

  // Max's no-argument case: "If there is no argument, there is one other outlet,
  // which is assigned the number 0." ShapePorts() turns that into two outlets,
  // which is also the shape ClearParams() restores (issue #679).
  ResetToDefaultSelector();
  ShapePorts();

  ADD_DESCRIPTION(
      "Routes a message by what its first item matches and takes that item off — Max's route, "
      "whose 'the rest of the message is sent out the outlet that corresponds to that argument' is "
      "the whole object. One outlet per creation argument plus a rightmost fall-through for "
      "everything that matched none of them, and a bare .route has two outlets and matches the "
      "single number 0, which is Max's no-argument case and the shape .sel already reproduces "
      "(issue #679). Until then a bare .route had no outlets at all and silently swallowed "
      "everything sent to it, so a patch that dropped one in before wiring its arguments lost "
      "messages rather than passing them on; .routepass is the object that deliberately does not "
      "invent a default selector, because Max documents one for route and select and none for "
      "routepass. The stripping is what makes this a dispatcher rather than a splitter: 'note 60 "
      "100' leaves the note branch as '60 100', so the branch works in bare values and never sees "
      "the "
      "word that got it there. Before issue #672 this object forwarded the whole message instead, "
      "which is Max's routepass and not Max's route, and the engine's own .sel documentation had "
      "described the stripping behaviour all along — the code was the odd one out. A patch that "
      "wants the matched word to stay on the message has .routepass, whose contract guarantees "
      "exactly that, and swapping the object is the migration. What leaves a matched outlet is "
      "whatever is left, in the kind that remainder is: 'foo 1 2' through .route foo sends the "
      "list '1 2'; 'foo 1' sends the int 1 rather than a one-element list, and 'foo 1.5' the float "
      "1.5, decided by the spelling as .trigger and .match decide it; 'foo bar' sends the list "
      "'bar', since a lone symbol has no type of its own here; and 'foo' alone sends a bang, Max's "
      "'if the first item of a message matches one of the arguments, but the message has no "
      "additional items, bang is sent out the specified outlet'. A bare number matched by a "
      "numeric selector is that same case — the number was the whole message — so .route 5 bangs "
      "for the int 5 and for the float 5.0 alike. Anything unmatched leaves the rightmost outlet "
      "whole and in its own type, so a chain of .route objects strings together with each "
      "fall-through feeding the next inlet. The matcher is .sel's and .routepass's: a selector is "
      "a number or a symbol, decided once when the argument is read, and the two never match each "
      "other — a number matches by value with an int widened to a float, a symbol by exact text, a "
      "bang matches a selector spelled 'bang', and a list is matched on its first element alone, "
      "which is a number if it reads as one and a symbol otherwise. That shared matcher is the "
      "other half of #672: the object used to compare std::to_string(value) against the selector "
      "text, and std::to_string(5.f) is '5.000000', a spelling no hand-written selector ever has, "
      "so .route 5 could never match the float 5.0. The comparison is exact, so a computed float "
      "may miss a selector it looks equal to — 0.1 + 0.2 is not 0.3 in binary floating point, and "
      "a .round upstream is the fix — and a NaN matches nothing and leaves the rightmost outlet. A "
      "selector repeated in the argument list uses the leftmost of its outlets only. Exactly one "
      "outlet fires per input. Calculate() does nothing, and no message path allocates except for "
      "a remainder of two or more items, which is one string — the cost stripping has and "
      "pass-through does not.");
  ADD_CATEGORY(pCategory::GENERIC);
  INLET_DOC(0, "in",
            "Value inlet — accepts bang / int / float / list. The first item decides: a list's "
            "leading token, a bare number itself, or the symbol 'bang'. What leaves the matching "
            "outlet is the rest of the message, as a list, as an int or float when it is a single "
            "number, or as a bang when the selector was the whole message. Anything matching no "
            "selector leaves the fall-through outlet whole and in its own type.",
            "");
  PARAM_DOC("list", "0",
            "Space-separated list of match tokens; one outlet is created per token plus one "
            "fall-through outlet. A token that reads as a finite number is a numeric selector and "
            "matches by value, with an int widened to a float; anything else is a symbol and "
            "matches by exact text. With no argument the object matches the single number 0, which "
            "is Max's no-argument case.",
            "any tokens");
}

void gRoute::ResetToDefaultSelector() {
  selectors.clear();
  selectors.push_back(Selector{"0", 0.f, true});
}

void gRoute::ShapePorts() {
  // Rebuilt rather than resized, because the fall-through outlet has to stay
  // rightmost: appending a match outlet would put it after the fall-through and
  // every saved cord past that point would land on the wrong port. Safe because
  // every caller runs before the object is wired or published: the constructor,
  // and the two parameter callbacks, which
  // patcherImplementation::CreateObjectUnlocked runs before
  // AssignGraphIds. A *live* SetParams never reaches here on a published
  // object — registering the callbacks makes ParamsNeedRebuild() true, so #234
  // replaces the object.
  outputs.clear();

  for (std::size_t i = 0; i < selectors.size(); i++) {
    // ANY, because what leaves is a bang, an int, a float or a list depending
    // on what the remainder turned out to be.
    ADD_OUT_ANY;
    outputs.back().SetDoc(MatchLabel((int)i), MatchDoc((int)i, selectors[i].text),
                          selectors[i].text);
  }

  // Max's rightmost outlet, present whatever the argument count. Nothing is
  // stripped here, which is what lets a chain of .route objects be strung
  // together with each fall-through feeding the next inlet.
  ADD_OUT_ANY;
  outputs.back().SetDoc("rest",
                        "The message, unchanged and in its own type, when its first item matched "
                        "no selector. Nothing is taken off it, so chaining this into the next "
                        ".route carries on testing the message the first one saw.",
                        "any");
}

PARM_CLEAR() {
  // Runs on the control thread before the parameter string is re-read, and is
  // the whole of `SetParams("")`: Parameters::Set returns without calling the
  // parse callback for an empty argument, so this has to leave Max's
  // no-argument object behind rather than one with no outlets at all, which
  // would swallow every message sent to it (#679).
  list.clear();
  ResetToDefaultSelector();
  ShapePorts();
}

PARM_PARSE() {
  // One selector per token, empty ones included, so an index into `list` is an
  // index into `selectors` and into `outputs` alike. An empty token can never
  // be matched — a leading token is only ever looked up when it has characters
  // in it — so it costs an unreachable outlet and nothing else, which is what
  // it cost before as well.
  selectors.clear();
  selectors.reserve(list.size());
  for (const std::string& token : list) {
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

  // An argument list with no tokens in it at all is a bare `.route`, and a bare
  // `.route` matches 0 rather than nothing. Parameters::Set only reaches here
  // with at least one token, so this is the guard that keeps ShapePorts()'s
  // "there is always at least one selector" invariant true no matter who calls.
  if (selectors.empty()) ResetToDefaultSelector();

  ShapePorts();
}

int gRoute::MatchNumber(float value) const {
  for (std::size_t i = 0; i < selectors.size(); i++) {
    if (!selectors[i].numeric) continue;
    // Exact, and exactness is the matcher .sel already applies — see its header
    // on why Max's fuzzy attribute is not ported. A NaN on either side compares
    // unequal, which is what sends it out the fall-through outlet.
    if (selectors[i].value == value) return (int)i;
  }
  return -1;
}

int gRoute::MatchSymbol(const char* text, std::size_t length) const {
  for (std::size_t i = 0; i < selectors.size(); i++) {
    if (selectors[i].numeric) continue;
    if (selectors[i].text.size() != length) continue;
    // Compared against the character range in place: a substr here would
    // allocate on whichever thread the message arrived on.
    if (selectors[i].text.compare(0, length, text, length) == 0) return (int)i;
  }
  return -1;
}

void gRoute::SendRemainder(int index, const std::string& value, std::size_t offset,
                           YSE::THREAD thread) {
  std::size_t begin = offset;
  while (begin < value.size() && IsSeparator(value[begin]))
    begin++;
  std::size_t end = value.size();
  while (end > begin && IsSeparator(value[end - 1]))
    end--;

  // Max: "If the first item of a message matches one of the arguments, but the
  // message has no additional items, bang is sent out the specified outlet."
  if (begin == end) {
    outputs[(std::size_t)index].SendBang(thread);
    return;
  }

  std::size_t tokenEnd = begin;
  while (tokenEnd < end && !IsSeparator(value[tokenEnd]))
    tokenEnd++;

  // A remainder of exactly one number is that number, not a list of one — in
  // Max `route foo` turns `foo 1` into the int 1. Int or float is decided by
  // the spelling, the test .trigger and .match already share, so a patch that
  // sent `foo 1` does not get `1.` back out.
  if (tokenEnd == end) {
    const std::size_t length = end - begin;
    float number = 0.f;
    if (ReadNumericToken(value.c_str() + begin, length, number)) {
      if (!TokenLooksLikeFloat(value.c_str() + begin, length) && FitsInt(number)) {
        outputs[(std::size_t)index].SendInt((int)number, thread);
      } else {
        outputs[(std::size_t)index].SendFloat(number, thread);
      }
      return;
    }
  }

  // Everything else is a list — including a lone symbol, which has no type of
  // its own in this patcher. This is the one allocation stripping costs; a
  // short remainder fits small-string optimisation and costs none.
  outputs[(std::size_t)index].SendList(value.substr(begin, end - begin), thread);
}

BANG_IN(SetBangValue) {
  // No emptiness check on `outputs` in any of the four handlers: the
  // constructor calls ShapePorts(), which always builds at least one match
  // outlet and the fall-through, and the parameter callbacks only ever rebuild
  // to the same invariant. Until #679 a bare object had no outlets at all and
  // every handler had to guard against reaching for one, which is the shape
  // that dropped messages.
  //
  // Max: the bang message matches a 'bang' symbol in the arguments. There is
  // nothing in a bang to strip, so a match sends a bang and so does a miss —
  // the outlet it leaves by is the whole answer.
  const int hit = MatchSymbol("bang", 4);
  if (hit >= 0) {
    outputs[(std::size_t)hit].SendBang(thread);
    return;
  }
  outputs.back().SendBang(thread);
}

INT_IN(SetIntValue) {
  // Widened and compared as a float: this patcher has one numeric type, so
  // `.route 5` has to answer the int 5 and the float 5.0 alike. A match
  // consumes the only item there was, so what leaves is a bang.
  const int hit = MatchNumber((float)value);
  if (hit >= 0) {
    outputs[(std::size_t)hit].SendBang(thread);
    return;
  }
  outputs.back().SendInt(value, thread);
}

FLOAT_IN(SetFloatValue) {
  const int hit = MatchNumber(value);
  if (hit >= 0) {
    outputs[(std::size_t)hit].SendBang(thread);
    return;
  }
  outputs.back().SendFloat(value, thread);
}

LIST_IN(SetListValue) {
  // Only the first element is ever examined, and on a match it is taken off:
  // Max's "the rest of the message is sent out the outlet that corresponds to
  // that argument".
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
    SendRemainder(hit, value, end, thread);
    return;
  }
  // Max: "If the first item does not match any of the arguments, the entire
  // message is passed out the rightmost outlet." Including a message with no
  // first element at all.
  outputs.back().SendList(value, thread);
}
