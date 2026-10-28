#include "dZplane.h"
#include "../math/gExprEval.h"
#include "../pListArgs.h"
#include "../pObjectList.hpp"
#include "../pSelector.h"

#include <cmath>
#include <string_view>

using namespace YSE::PATCHER;
using YSE::DSP::zPlane;

namespace {
  // The most numbers a message is read for: a whole cascade.
  constexpr unsigned int MAX_NUMBERS = zPlane::MAX_SECTIONS * 5;

  // The longest list the object sends: the cascade, each number at most
  // kExprValueTextMax characters with its separator.
  constexpr std::size_t SCRATCH_CAPACITY = MAX_NUMBERS * (YSE::PATCHER::kExprValueTextMax + 1);

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
    return !token.empty() && ReadNumericToken(token.data(), token.size(), out) &&
           std::isfinite(out);
  }

  // The numbers from `from` on, at most `capacity` stored. -1 when a token
  // is not a number: such a message is not read at all.
  int ReadNumbers(const std::string& text, std::size_t from, float* out, unsigned int capacity) {
    int count = 0;
    std::size_t next = from;
    std::string_view token = Token(text, next, next);
    while (!token.empty()) {
      float value = 0.f;
      if (!ReadNumber(token, value)) return -1;
      if (static_cast<unsigned int>(count) < capacity) out[count] = value;
      ++count;
      token = Token(text, next, next);
    }
    return count;
  }

  void AppendNumber(std::string& out, float value) {
    char text[kExprValueTextMax];
    const int written = ExprFormatValue(ExprValue::Float(value), text, kExprValueTextMax);
    if (written <= 0) return;
    if (!out.empty()) out.push_back(' ');
    out.append(text, static_cast<std::size_t>(written));
  }

  void AppendInt(std::string& out, int value) {
    char text[FORMAT_INT_WIDTH];
    const std::size_t written = WriteInt(value, text);
    if (!out.empty()) out.push_back(' ');
    out.append(text, written);
  }

  void AppendList(std::string& out, const float* values, unsigned int n) {
    out.clear();
    for (unsigned int i = 0; i < n; ++i)
      AppendNumber(out, values[i]);
  }

  // A whole number in [0, limit), or -1.
  int ToIndex(float value, unsigned int limit) {
    if (!std::isfinite(value) || value < 0.f) return -1;
    const long rounded = std::lround(value);
    return rounded < static_cast<long>(limit) ? static_cast<int>(rounded) : -1;
  }

  unsigned int ToOrder(float value) {
    return value < 2.f ? 2u : static_cast<unsigned int>(std::lround(value));
  }
} // namespace

#define className dZplane

CONSTRUCT_DSP() {
  // in 0: messages, bang, a0
  ADD_IN_0;
  REG_BANG_IN(BangIn);
  REG_FLOAT_IN(FloatIn);
  REG_INT_IN(IntIn);
  REG_LIST_IN(ListIn);
  // in 1-4: a1 a2 b1 b2
  for (int i = 1; i < 5; ++i) {
    inputs.emplace_back(this, false, i);
    REG_FLOAT_IN(FloatIn);
    REG_INT_IN(IntIn);
    REG_LIST_IN(ListIn);
  }

  ADD_OUT_LIST; // 0: coefficients
  ADD_OUT_LIST; // 1: zeroes
  ADD_OUT_LIST; // 2: poles
  ADD_OUT_LIST; // 3: gains

  ADD_PARAM(settings);
  REG_PARM_CLEAR;
  REG_PARM_PARSE;

  scratch.reserve(SCRATCH_CAPACITY);

  ADD_DESCRIPTION("Shows a filter as poles and zeroes on the z plane: holds one to 24 biquad "
                  "sections, given as the coefficient list ~biquad and ~cascade take, sends "
                  "their zeroes, poles and gains, and turns moved poles and zeroes back into "
                  "coefficients. A host draws the plane; the sections are saved with the patch.");
  ADD_CATEGORY(pCategory::FILTER);
  INLET_DOC(0, "in",
            "Bang: send the coefficients, zeroes, poles and gains. A number: a0 of the first "
            "section. A list of numbers: a cascade, five coefficients per section. 'zero <index> "
            "<real> <imaginary>' and 'pole <index> <real> <imaginary>' move one root and send "
            "the new coefficients; order and pconstrain set the attributes.",
            "");
  INLET_DOC(1, "a1", "a1 of the first section; sends everything.", "");
  INLET_DOC(2, "a2", "a2 of the first section; sends everything.", "");
  INLET_DOC(3, "b1", "b1 of the first section; sends everything.", "");
  INLET_DOC(4, "b2", "b2 of the first section; sends everything.", "");
  OUTLET_DOC(0, "coefficients",
             "The coefficients of every section, a0 a1 a2 b1 b2 per section, for ~biquad (one "
             "section) or ~cascade; sent on a bang, a coefficient inlet or a moved root.",
             "a list of coefficients in sets of five");
  OUTLET_DOC(1, "zeroes", "The zeroes, two per section, as real and imaginary parts.",
             "real imaginary pairs");
  OUTLET_DOC(2, "poles", "The poles, two per section, as real and imaginary parts.",
             "real imaginary pairs");
  OUTLET_DOC(3, "gains", "The gain (a0) of each section.", "one number per section");
  PARAM_DOC("settings", "",
            "'order <n>', the number of poles (2-48, two per section, default 2), and "
            "'pconstrain <0|1>', keep moved poles inside the unit circle (default 0). The "
            "sections are saved with the patch.",
            "words and values");
}

// Control thread: a list parameter rebuilds the object, so these run on one
// no other thread has seen.
PARM_CLEAR() {
  settings.clear();
  plane = zPlane();
}

PARM_PARSE() {
  std::string text;
  for (const std::string& token : settings) {
    text += token;
    text += ' ';
  }
  ApplySettings(text);
}

void dZplane::ApplySettings(const std::string& text) {
  std::size_t next = 0;
  std::string_view word = Token(text, 0, next);
  while (!word.empty()) {
    std::size_t after = next;
    float number = 0.f;
    if (ReadNumber(Token(text, after, after), number)) {
      if (word == "order") {
        plane.order(ToOrder(number));
        next = after;
      } else if (word == "pconstrain") {
        plane.constrainPoles(number != 0.f);
        next = after;
      }
    }
    word = Token(text, next, next);
  }
}

void dZplane::SendRoots(YSE::THREAD thread) {
  float values[MAX_NUMBERS];
  AppendList(scratch, values, plane.gains(values, MAX_NUMBERS));
  outputs[3].SendList(scratch, thread);
  AppendList(scratch, values, plane.poles(values, MAX_NUMBERS));
  outputs[2].SendList(scratch, thread);
  AppendList(scratch, values, plane.zeroes(values, MAX_NUMBERS));
  outputs[1].SendList(scratch, thread);
}

void dZplane::SendAll(YSE::THREAD thread) {
  SendRoots(thread);
  float values[MAX_NUMBERS];
  AppendList(scratch, values, plane.coefficients(values, MAX_NUMBERS));
  outputs[0].SendList(scratch, thread);
}

void dZplane::Handle(const std::string& text, YSE::THREAD thread) {
  std::size_t next = 0;
  const std::string_view word = Token(text, 0, next);
  if (word.empty()) return;
  float numbers[MAX_NUMBERS];

  // Numbers alone: one is a0, more a cascade.
  float first = 0.f;
  if (ReadNumber(word, first)) {
    const int n = ReadNumbers(text, 0, numbers, MAX_NUMBERS);
    if (n == 1) {
      plane.coefficient(0, 0, first);
      SendAll(thread);
    } else if (n > 1 && plane.cascade(numbers, static_cast<unsigned int>(n)) > 0) {
      SendRoots(thread);
    }
    return;
  }

  if (word == "bang") {
    SendAll(thread);
    return;
  }

  const int n = ReadNumbers(text, next, numbers, MAX_NUMBERS);
  if (n < 0) return;
  const unsigned int count = static_cast<unsigned int>(n);

  if (word == "zero" || word == "pole") {
    if (count != 3) return;
    const int index = ToIndex(numbers[0], plane.order());
    if (index < 0) return;
    const auto i = static_cast<unsigned int>(index);
    const bool moved = word == "zero" ? plane.moveZero(i, numbers[1], numbers[2])
                                      : plane.movePole(i, numbers[1], numbers[2]);
    if (moved) SendAll(thread);
  } else if (word == "order") {
    if (count >= 1) plane.order(ToOrder(numbers[0]));
  } else if (word == "pconstrain") {
    if (count >= 1) plane.constrainPoles(numbers[0] != 0.f);
  }
}

BANG_IN(BangIn) {
  (void)inlet;
  if (busy.exchange(true, std::memory_order_acquire)) {
    dropped.fetch_add(1, std::memory_order_relaxed);
    return;
  }
  SendAll(thread);
  busy.store(false, std::memory_order_release);
}

FLOAT_IN(FloatIn) {
  if (busy.exchange(true, std::memory_order_acquire)) {
    dropped.fetch_add(1, std::memory_order_relaxed);
    return;
  }
  // Held across the send: the lists are written in `scratch`.
  plane.coefficient(0, static_cast<unsigned int>(inlet), value);
  SendAll(thread);
  busy.store(false, std::memory_order_release);
}

INT_IN(IntIn) {
  FloatIn(static_cast<float>(value), inlet, thread);
}

LIST_IN(ListIn) {
  if (busy.exchange(true, std::memory_order_acquire)) {
    dropped.fetch_add(1, std::memory_order_relaxed);
    return;
  }
  if (inlet == 0) {
    Handle(value, thread);
  } else {
    // A single number on another inlet is that number.
    float numbers[2];
    if (ReadNumbers(value, 0, numbers, 2) == 1) {
      busy.store(false, std::memory_order_release);
      FloatIn(numbers[0], inlet, thread);
      return;
    }
  }
  busy.store(false, std::memory_order_release);
}

// ─── the GUI value ───────────────────────────────────────────────────────────

std::string dZplane::Cell(const zPlane& p, unsigned int index) {
  std::string out;
  if (index == 0) {
    AppendInt(out, static_cast<int>(p.sections()));
    AppendInt(out, p.constrainPoles() ? 1 : 0);
    return out;
  }
  const unsigned int s = index - 1;
  const zPlane::coefficientSet c = p.coefficients(s);
  AppendNumber(out, c.a0);
  AppendNumber(out, c.a1);
  AppendNumber(out, c.a2);
  AppendNumber(out, c.b1);
  AppendNumber(out, c.b2);
  for (unsigned int r = 2 * s; r < 2 * s + 2; ++r) {
    AppendNumber(out, static_cast<float>(p.zero(r).real()));
    AppendNumber(out, static_cast<float>(p.zero(r).imag()));
  }
  for (unsigned int r = 2 * s; r < 2 * s + 2; ++r) {
    AppendNumber(out, static_cast<float>(p.pole(r).real()));
    AppendNumber(out, static_cast<float>(p.pole(r).imag()));
  }
  return out;
}

std::string dZplane::GetGuiValue() {
  // Host thread: copy under the guard, format after releasing it.
  if (busy.exchange(true, std::memory_order_acquire)) return std::string();
  const zPlane copy = plane;
  busy.store(false, std::memory_order_release);

  std::string out = Cell(copy, 0);
  for (unsigned int i = 1; i <= copy.sections(); ++i) {
    out.push_back(' ');
    out += Cell(copy, i);
  }
  return out;
}

unsigned int dZplane::GetGuiValueCount() const {
  if (busy.exchange(true, std::memory_order_acquire)) return 0;
  const unsigned int n = plane.sections() + 1;
  busy.store(false, std::memory_order_release);
  return n;
}

std::string dZplane::GetGuiValueAt(unsigned int index) {
  if (busy.exchange(true, std::memory_order_acquire)) return std::string();
  const zPlane copy = plane;
  busy.store(false, std::memory_order_release);
  // Range-checked against the count read now.
  if (index > copy.sections()) return std::string();
  return Cell(copy, index);
}

// ─── persistence ─────────────────────────────────────────────────────────────

bool dZplane::DumpState(nlohmann::json::value_type& json) {
  const saveGuard guard(busy);
  if (!guard.Held()) return false;
  json["pconstrain"] = plane.constrainPoles();
  for (unsigned int s = 0; s < plane.sections(); ++s) {
    const zPlane::coefficientSet c = plane.coefficients(s);
    json["section"].push_back({c.a0, c.a1, c.a2, c.b1, c.b2});
  }
  return true;
}

void dZplane::RestoreState(const nlohmann::json::value_type& json) {
  // ParseJSON, on a freshly built object no other thread has seen. A
  // hand-edited file must not throw on load: anything that is not a list of
  // whole sets of numbers is skipped.
  const auto constrain = json.find("pconstrain");
  if (constrain != json.end() && constrain->is_boolean())
    plane.constrainPoles(constrain->get<bool>());
  const auto stored = json.find("section");
  if (stored == json.end() || !stored->is_array()) return;
  float values[MAX_NUMBERS];
  unsigned int n = 0;
  for (const auto& section : *stored) {
    if (n + 5 > MAX_NUMBERS) break;
    if (!section.is_array() || section.size() != 5) return;
    for (const auto& v : section) {
      if (!v.is_number()) return;
      values[n++] = v.get<float>();
    }
  }
  plane.cascade(values, n);
}

#undef className
