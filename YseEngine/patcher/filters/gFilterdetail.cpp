#include "gFilterdetail.h"
#include "../math/gExprEval.h"
#include "../pListArgs.h"
#include "../pObjectList.hpp"
#include "../pSelector.h"

#include <algorithm>
#include <cmath>
#include <string_view>

using namespace YSE::PATCHER;

namespace {
  // The most coefficients one list carries: five per section the DSP class
  // reads.
  constexpr unsigned int MAX_VALUES = YSE::DSP::filterResponse::MAX_SECTIONS * 5;

  // One curve: a number of at most kExprValueTextMax characters, with its
  // separator, per point.
  constexpr std::size_t SCRATCH_CAPACITY =
      static_cast<std::size_t>(gFilterdetail::MAX_POINTS) * (kExprValueTextMax + 1);

  // The token at or after `from`, walked in place so nothing allocates on
  // whichever thread delivers the message. `next` is where it ends; empty
  // when there is none.
  std::string_view Token(const std::string& text, std::size_t from, std::size_t& next) {
    const char* chars = text.c_str();
    const std::size_t size = text.size();
    std::size_t begin = from;
    while (begin < size && IsSelectorSeparator(chars[begin]))
      begin++;
    std::size_t end = begin;
    while (end < size && !IsSelectorSeparator(chars[end]))
      end++;
    next = end;
    return std::string_view(chars + begin, end - begin);
  }

  bool ReadNumber(std::string_view token, float& out) {
    return !token.empty() && ReadNumericToken(token.data(), token.size(), out);
  }

  unsigned int ClampPoints(float value) {
    if (!(value >= 1.f)) return 1;
    if (value >= static_cast<float>(gFilterdetail::MAX_POINTS)) return gFilterdetail::MAX_POINTS;
    return static_cast<unsigned int>(std::lround(value));
  }

  void Render(std::string& out, const float* values, unsigned int count) {
    out.clear();
    char text[kExprValueTextMax];
    for (unsigned int i = 0; i < count; ++i) {
      const int written = ExprFormatValue(ExprValue::Float(values[i]), text, kExprValueTextMax);
      if (written <= 0) continue;
      if (!out.empty()) out.push_back(' ');
      out.append(text, static_cast<std::size_t>(written));
    }
  }
} // namespace

#define className gFilterdetail

CONSTRUCT() {
  ADD_IN_0;
  REG_LIST_IN(List);

  ADD_OUT_LIST; // magnitude
  ADD_OUT_LIST; // phase
  ADD_OUT_LIST; // phase delay
  ADD_OUT_LIST; // group delay
  ADD_OUT_LIST; // impulse response
  ADD_OUT_LIST; // step response

  ADD_PARAM(numpoints);
  REG_PARM_CLEAR;
  REG_PARM_PARSE;

  scratch.reserve(SCRATCH_CAPACITY);

  ADD_DESCRIPTION("Details a filter given as second-order sections (the coefficient list "
                  ".filterdesign sends): magnitude, phase, phase delay and group delay across "
                  "the spectrum, and the impulse and step responses, each as a list.");
  ADD_CATEGORY(pCategory::FILTER);
  INLET_DOC(0, "sections",
            "A list of coefficients in sets of five, a0 a1 a2 b1 b2 per section: analyse the "
            "filter and send the six lists. 'numpoints <n>': the number of values per list "
            "(1-256), without sending.",
            "a list of coefficients in sets of five");
  OUTLET_DOC(0, "magnitude",
             "The gain at numpoints frequencies from 0 up to just below half the sample rate, "
             "in equal steps (1 is unity gain).",
             "0+");
  OUTLET_DOC(1, "phase", "The phase at the same frequencies, in radians, unwrapped.", "");
  OUTLET_DOC(2, "phase delay", "The phase delay at the same frequencies, in samples.", "");
  OUTLET_DOC(3, "group delay", "The group delay at the same frequencies, in samples.", "");
  OUTLET_DOC(4, "impulse", "The impulse response, one value per sample.", "");
  OUTLET_DOC(5, "step", "The step response, one value per sample.", "");
  PARAM_DOC("numpoints", "256",
            "The number of values per list, 1-256, optionally after the word numpoints "
            "('.filterdetail 128'). A later numpoints message does not change what is saved.",
            "1-256");
}

// Control thread: a list parameter rebuilds the object, so these run on one
// no other thread has seen.
PARM_CLEAR() {
  numpoints.clear();
  fn.points(MAX_POINTS);
}

PARM_PARSE() {
  for (const std::string& token : numpoints) {
    float number = 0.f;
    if (ReadNumber(token, number) && std::isfinite(number)) {
      fn.points(ClampPoints(number));
      return;
    }
  }
}

LIST_IN(List) {
  (void)inlet;
  // Held across the sends: the lists are written in `scratch`, which a
  // second message must not overwrite while a receiver reads it.
  if (busy.exchange(true, std::memory_order_acquire)) {
    dropped.fetch_add(1, std::memory_order_relaxed);
    return;
  }
  std::size_t next = 0;
  std::string_view token = Token(value, 0, next);
  float number = 0.f;
  if (token == "numpoints") {
    if (ReadNumber(Token(value, next, next), number) && std::isfinite(number))
      fn.points(ClampPoints(number));
  } else {
    // A coefficient list. A token that is not a number ends it.
    float values[MAX_VALUES];
    unsigned int count = 0;
    while (count < MAX_VALUES && ReadNumber(token, number)) {
      values[count++] = number;
      token = Token(value, next, next);
    }
    if (fn.analyse(values, count)) {
      const unsigned int n = fn.points();
      Render(scratch, fn.step(), n);
      outputs[5].SendList(scratch, thread);
      Render(scratch, fn.impulse(), n);
      outputs[4].SendList(scratch, thread);
      Render(scratch, fn.groupDelay(), n);
      outputs[3].SendList(scratch, thread);
      Render(scratch, fn.phaseDelay(), n);
      outputs[2].SendList(scratch, thread);
      Render(scratch, fn.phase(), n);
      outputs[1].SendList(scratch, thread);
      Render(scratch, fn.magnitude(), n);
      outputs[0].SendList(scratch, thread);
    }
  }
  busy.store(false, std::memory_order_release);
}

#undef className
