#include "gRoute.h"
#include "../math/gExprEval.h"
#include "../pListArgs.h"
#include "../pObjectList.hpp"
#include <cstddef>
#include <string>

using namespace YSE::PATCHER;
#define className gRoute

static bool IsSeparator(char c) {
  return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

static std::string MatchLabel(int index) {
  char digits[FORMAT_INT_WIDTH];
  const std::size_t written = WriteInt(index, digits);
  return "match" + std::string(digits, written);
}

static std::string MatchDoc(int index, const std::string& selector) {
  char digits[FORMAT_INT_WIDTH];
  const std::size_t written = WriteInt(index, digits);
  return "When the first item matches selector " + std::string(digits, written) + " (" + selector +
         "), the selector is consumed. Nothing remaining sends a bang; one numeric item keeps "
         "its scalar type; one symbol remains a one-item list; more items leave as a list.";
}

CONSTRUCT() {
  ADD_IN_0;
  REG_BANG_IN(SetBangValue);
  REG_INT_IN(SetIntValue);
  REG_FLOAT_IN(SetFloatValue);
  REG_LIST_IN(SetListValue);

  REG_PARM_CLEAR;
  REG_PARM_PARSE;

  ADD_PARAM(selectorArgs);
  remainder.reserve(TEXT_CAPACITY);
  ShapePorts();

  ADD_DESCRIPTION(
      "Routes a message by its first item and removes that matched selector. One outlet is created "
      "per selector plus a rightmost outlet for unmatched messages. The pair .route and .routepass "
      "use the same numeric-or-symbol matcher: .route consumes the selector, while .routepass "
      "forwards the complete message. A matched list emits its remainder as a list when several "
      "items remain, as an int or float when one numeric item remains, or as a bang when none "
      "remain; one symbolic item remains a one-item list. A matched bare int or float likewise "
      "emits a bang. Unmatched input passes through unchanged. Numeric selectors match ints and "
      "floats by value; symbolic selectors match exact text. At most 256 selectors are held, and "
      "matching performs no number formatting.");
  ADD_CATEGORY(pCategory::GENERIC);
  INLET_DOC(0, "in",
            "Bang, int, float or list to route. A match consumes the first item; unmatched input "
            "leaves the rightmost outlet unchanged.",
            "any");
  PARAM_DOC("selectors", "",
            "Values to match against, in outlet order. Finite numeric tokens match ints and floats "
            "by value; other tokens match symbols by exact text.",
            "any list of numbers and symbols");
}

PARM_CLEAR() {
  selectorArgs.clear();
  selectors.clear();
  ShapePorts();
}

PARM_PARSE() {
  selectors.clear();
  for (const std::string& token : selectorArgs) {
    if (selectors.size() >= (std::size_t)MAX_SELECTORS) break;
    if (token.empty()) continue;

    float number = 0.f;
    if (ReadNumericToken(token.c_str(), token.size(), number)) {
      selectors.push_back(Selector{token, number, true});
    } else {
      selectors.push_back(Selector{token, 0.f, false});
    }
  }
  ShapePorts();
}

void gRoute::ShapePorts() {
  outputs.clear();
  for (std::size_t i = 0; i < selectors.size(); i++) {
    ADD_OUT_ANY;
    outputs.back().SetDoc(MatchLabel((int)i), MatchDoc((int)i, selectors[i].text),
                          selectors[i].text);
  }
  ADD_OUT_ANY;
  outputs.back().SetDoc("rest", "The original message when no selector matched.", "any");
}

int gRoute::MatchNumber(float value) const {
  for (std::size_t i = 0; i < selectors.size(); i++) {
    if (selectors[i].numeric && selectors[i].value == value) return (int)i;
  }
  return -1;
}

int gRoute::MatchSymbol(const char* text, std::size_t length) const {
  for (std::size_t i = 0; i < selectors.size(); i++) {
    if (selectors[i].numeric || selectors[i].text.size() != length) continue;
    if (selectors[i].text.compare(0, length, text, length) == 0) return (int)i;
  }
  return -1;
}

BANG_IN(SetBangValue) {
  if (inlet != 0) return;
  const int hit = MatchSymbol("bang", 4);
  if (hit >= 0) {
    outputs[(std::size_t)hit].SendBang(thread);
    return;
  }
  outputs.back().SendBang(thread);
}

INT_IN(SetIntValue) {
  if (inlet != 0) return;
  const int hit = MatchNumber((float)value);
  if (hit >= 0) {
    outputs[(std::size_t)hit].SendBang(thread);
    return;
  }
  outputs.back().SendInt(value, thread);
}

FLOAT_IN(SetFloatValue) {
  if (inlet != 0) return;
  const int hit = MatchNumber(value);
  if (hit >= 0) {
    outputs[(std::size_t)hit].SendBang(thread);
    return;
  }
  outputs.back().SendFloat(value, thread);
}

LIST_IN(SetListValue) {
  if (inlet != 0) return;

  std::size_t begin = 0;
  while (begin < value.size() && IsSeparator(value[begin]))
    begin++;
  std::size_t end = begin;
  while (end < value.size() && !IsSeparator(value[end]))
    end++;

  int hit = -1;
  if (end > begin) {
    const char* token = value.c_str() + begin;
    const std::size_t length = end - begin;
    float number = 0.f;
    if (ReadNumericToken(token, length, number)) {
      hit = MatchNumber(number);
    } else {
      hit = MatchSymbol(token, length);
    }
  }

  if (hit < 0) {
    outputs.back().SendList(value, thread);
    return;
  }

  while (end < value.size() && IsSeparator(value[end]))
    end++;

  if (end >= value.size()) {
    outputs[(std::size_t)hit].SendBang(thread);
    return;
  }

  std::size_t tokenEnd = end;
  while (tokenEnd < value.size() && !IsSeparator(value[tokenEnd]))
    tokenEnd++;
  std::size_t next = tokenEnd;
  while (next < value.size() && IsSeparator(value[next]))
    next++;

  if (next == value.size()) {
    float number = 0.f;
    const char* token = value.c_str() + end;
    const std::size_t length = tokenEnd - end;
    if (ReadNumericToken(token, length, number)) {
      if (TokenLooksLikeFloat(token, length)) {
        outputs[(std::size_t)hit].SendFloat(number, thread);
      } else {
        outputs[(std::size_t)hit].SendInt(ExprToInt(number), thread);
      }
      return;
    }
    remainder.assign(token, length);
  } else {
    remainder.assign(value.c_str() + end, value.size() - end);
  }
  outputs[(std::size_t)hit].SendList(remainder, thread);
}
