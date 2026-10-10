#include "gRoute.h"
#include "../pListArgs.h"
#include "../pObjectList.hpp"
#include <cstddef>
#include <string>

using namespace YSE::PATCHER;
#define className gRoute

namespace {

  // A remainder that is a whole number is only sent as an int when the int can
  // hold it: casting a float outside the int range is undefined behaviour, and
  // a patch that spelled a huge integer is better served by the float that
  // still carries its value. -2^31 and 2^31 are both exactly representable, so
  // this comparison is exact.
  bool FitsInt(float value) {
    return value >= -2147483648.f && value < 2147483648.f;
  }

  std::string MatchDoc(int index, const std::string& selector) {
    char digits[FORMAT_INT_WIDTH];
    const std::size_t written = WriteInt(index, digits);
    return "The rest of the message when its first item matches selector " +
           std::string(digits, written) + " (" + selector +
           "): a list, a single int or float, or a bang when nothing is left.";
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
  selectors.ResetToZero();
  ShapePorts();

  ADD_DESCRIPTION(
      "Sends a message out the outlet whose selector matches its first item, with that item "
      "removed. A message that matches no selector leaves the rightmost outlet unchanged.");
  ADD_CATEGORY(pCategory::ROUTING);
  INLET_DOC(0, "in", "Bang, int, float or list. Its first item is matched against the selectors.",
            "");
  PARAM_DOC("list", "0",
            "The selectors, one outlet each. A number matches by value, anything else by exact "
            "text. With no argument the object matches 0.",
            "any tokens");
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

  for (std::size_t i = 0; i < selectors.Size(); i++) {
    // ANY, because what leaves is a bang, an int, a float or a list depending
    // on what the remainder turned out to be.
    ADD_OUT_ANY;
    outputs.back().SetDoc(MatchOutletLabel((int)i), MatchDoc((int)i, selectors.Text(i)),
                          selectors.Text(i));
  }

  // Max's rightmost outlet, present whatever the argument count. Nothing is
  // stripped here, which is what lets a chain of .route objects be strung
  // together with each fall-through feeding the next inlet.
  ADD_OUT_ANY;
  outputs.back().SetDoc("rest",
                        "The message, unchanged, when its first item matched no selector. Wire it "
                        "into another .route to test further.",
                        "any");
}

PARM_CLEAR() {
  // Runs on the control thread before the parameter string is re-read, and is
  // the whole of `SetParams("")`: Parameters::Set returns without calling the
  // parse callback for an empty argument, so this has to leave Max's
  // no-argument object behind rather than one with no outlets at all, which
  // would swallow every message sent to it (#679).
  list.clear();
  selectors.ResetToZero();
  ShapePorts();
}

PARM_PARSE() {
  // One selector per token, empty ones included, so an index into `list` is an
  // index into `selectors` and into `outputs` alike. An empty token can never
  // be matched — a leading token is only ever looked up when it has characters
  // in it — so it costs an unreachable outlet and nothing else, which is what
  // it cost before as well.
  selectors.Clear();
  selectors.Reserve(list.size());
  for (const std::string& token : list) {
    // Numeric or symbolic is decided by the shared table's strict reader, as
    // the rest of the family is: ExprParseFloatList would read `5abc` as 5 and
    // fold `1e999` to 0, and neither answers "is this selector a number at
    // all".
    selectors.Add(token);
  }

  // An argument list with no tokens in it at all is a bare `.route`, and a bare
  // `.route` matches 0 rather than nothing. Parameters::Set only reaches here
  // with at least one token, so this is the guard that keeps ShapePorts()'s
  // "there is always at least one selector" invariant true no matter who calls.
  if (selectors.Empty()) selectors.ResetToZero();

  ShapePorts();
}

void gRoute::SendRemainder(int index, const std::string& value, std::size_t offset,
                           YSE::THREAD thread) {
  std::size_t begin = offset;
  while (begin < value.size() && IsSelectorSeparator(value[begin]))
    begin++;
  std::size_t end = value.size();
  while (end > begin && IsSelectorSeparator(value[end - 1]))
    end--;

  // Max: "If the first item of a message matches one of the arguments, but the
  // message has no additional items, bang is sent out the specified outlet."
  if (begin == end) {
    outputs[(std::size_t)index].SendBang(thread);
    return;
  }

  std::size_t tokenEnd = begin;
  while (tokenEnd < end && !IsSelectorSeparator(value[tokenEnd]))
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
  const int hit = selectors.MatchBang();
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
  const int hit = selectors.MatchNumber((float)value);
  if (hit >= 0) {
    outputs[(std::size_t)hit].SendBang(thread);
    return;
  }
  outputs.back().SendInt(value, thread);
}

FLOAT_IN(SetFloatValue) {
  const int hit = selectors.MatchNumber(value);
  if (hit >= 0) {
    outputs[(std::size_t)hit].SendBang(thread);
    return;
  }
  outputs.back().SendFloat(value, thread);
}

LIST_IN(SetListValue) {
  // Only the first element is ever examined, and on a match it is taken off:
  // Max's "the rest of the message is sent out the outlet that corresponds to
  // that argument". `tokenEnd` is where that item ends, which is where the
  // remainder starts — the whole of this object's difference from .routepass,
  // which asks the same matcher the same question and throws the offset away.
  std::size_t tokenEnd = 0;
  const int hit = selectors.MatchLeadingToken(value, tokenEnd);

  if (hit >= 0) {
    SendRemainder(hit, value, tokenEnd, thread);
    return;
  }
  // Max: "If the first item does not match any of the arguments, the entire
  // message is passed out the rightmost outlet." Including a message with no
  // first element at all.
  outputs.back().SendList(value, thread);
}
