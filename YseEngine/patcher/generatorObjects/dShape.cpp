#include "dShape.h"

#include <cstring>
#include "../pListArgs.h"
#include "../pSelector.h"

using namespace YSE::PATCHER;

namespace {
  constexpr UInt POINTS = YSE::DSP::shapeEnvelope::DEFAULT_POINTS;

  // The next token of `text` from `pos`: [begin, end). Empty at the end.
  // Walked in place: no allocation on whichever thread sends.
  void NextToken(const char* text, std::size_t length, std::size_t pos, std::size_t& begin,
                 std::size_t& end) {
    while (pos < length && IsSelectorSeparator(text[pos]))
      pos++;
    begin = pos;
    while (pos < length && !IsSelectorSeparator(text[pos]))
      pos++;
    end = pos;
  }

  bool Is(const char* text, std::size_t begin, std::size_t end, const char* word) {
    const std::size_t length = std::strlen(word);
    return end - begin == length && std::strncmp(text + begin, word, length) == 0;
  }
} // namespace

#define className dShape

CONSTRUCT_DSP() {
  initialValue = 0.f;
  for (slot& s : slots) {
    s.values.assign(POINTS, 0.f);
    s.times.assign(POINTS, 0.f);
    s.curves.assign(POINTS, 0.f);
  }

  // in 0: the ramp (signal), the initial value (float, int), the envelope
  // (list) and the attribute messages
  ADD_IN_0;
  REG_SIGNAL_IN;
  REG_FLOAT_IN(SetInitial);
  REG_INT_IN(SetInitialInt);
  REG_LIST_IN(SetList);

  ADD_OUT_BUFFER;

  ADD_PARAM(constant);
  ADD_PARAM(curvemode);
  ADD_PARAM(syncupdate);

  ADD_DESCRIPTION("A breakpoint envelope played over the length of a ramp such as a ~phasor: one "
                  "cycle of the ramp is one pass through the envelope, so its length follows the "
                  "ramp's frequency. For envelopes whose shape is fixed and whose length follows "
                  "tempo or note length.");
  ADD_CATEGORY(pCategory::OSC);
  INLET_DOC(0, "ramp",
            "A 0-1 or 1-0 ramp signal; the envelope starts when it leaves 0 or resets. A number "
            "is the initial value for the next list. A list sets the envelope: 'value time' "
            "pairs (as .function sends them), or 'value time curve' triplets with curvemode 1, "
            "optionally led by the initial value. 'constant <n>', 'curvemode <n>' and "
            "'syncupdate <0|1>' set the attributes (not saved).",
            "0-1");
  OUTLET_DOC(0, "out", "The envelope's value at the ramp's position.", "any float");
  PARAM_DOC("constant", "0",
            "How many leading segments play at their own times in milliseconds (a rising ramp "
            "only); the rest are scaled into the remainder of the ramp.",
            "0 or more");
  PARAM_DOC("curvemode", "0",
            "0: lists are pairs, curves play linear; 1: lists are triplets with a curvature "
            "(-1 to 1); 2: lists are pairs, curves play.",
            "0-2");
  PARAM_DOC("syncupdate", "0",
            "1: a new envelope waits until the ramp starts again; 0: it applies at once.", "0-1");
}

FLOAT_IN(SetInitial) {
  (void)inlet;
  (void)thread;
  initialValue = value;
}

INT_IN(SetInitialInt) {
  (void)inlet;
  (void)thread;
  initialValue = static_cast<float>(value);
}

LIST_IN(SetList) {
  (void)inlet;
  (void)thread;
  // A word first is a message that came in as a list (through a receive).
  std::size_t begin = 0;
  std::size_t end = 0;
  NextToken(value.c_str(), value.size(), 0, begin, end);
  if (begin == end) return;
  float number = 0.f;
  if (ReadNumericToken(value.c_str() + begin, end - begin, number)) {
    Breakpoints(value);
  } else {
    Attribute(value);
  }
}

MESSAGES() {
  (void)value;
  Attribute(message);
}

void dShape::Attribute(const std::string& message) {
  const char* text = message.c_str();
  const std::size_t length = message.size();
  std::size_t begin = 0;
  std::size_t end = 0;
  NextToken(text, length, 0, begin, end);
  std::size_t arg = 0;
  std::size_t argEnd = 0;
  NextToken(text, length, end, arg, argEnd);
  float number = 0.f;
  if (!ReadNumericToken(text + arg, argEnd - arg, number)) return;
  const int setting = static_cast<int>(number);
  if (Is(text, begin, end, "constant")) {
    constant = setting;
  } else if (Is(text, begin, end, "curvemode")) {
    curvemode = setting;
  } else if (Is(text, begin, end, "syncupdate")) {
    syncupdate = setting;
  }
}

// A list of numbers: read twice in place, once to count and once to write,
// so a list is refused whole rather than half applied.
void dShape::Breakpoints(const std::string& message) {
  const char* text = message.c_str();
  const std::size_t length = message.size();
  UInt numbers = 0;
  std::size_t begin = 0;
  std::size_t end = 0;
  for (;;) {
    NextToken(text, length, end, begin, end);
    if (begin == end) break;
    float number = 0.f;
    if (!ReadNumericToken(text + begin, end - begin, number)) {
      dropped.fetch_add(1, std::memory_order_relaxed);
      return;
    }
    numbers++;
  }

  const UInt width = curvemode.load(std::memory_order_relaxed) == 1 ? 3 : 2;
  const UInt points = numbers / width;
  const UInt extra = numbers % width;
  if (extra > 1 || points > POINTS) {
    dropped.fetch_add(1, std::memory_order_relaxed);
    return;
  }

  if (writing.test_and_set(std::memory_order_acquire)) {
    dropped.fetch_add(1, std::memory_order_relaxed);
    return;
  }
  slot& s = slots[writeSlot];
  end = 0;
  float number = 0.f;
  // Every token was read as a number above: these reads cannot fail.
  if (extra == 1) {
    NextToken(text, length, end, begin, end);
    (void)ReadNumericToken(text + begin, end - begin, number);
    initialValue = number;
  }
  s.initial = initialValue;
  for (UInt i = 0; i < points; ++i) {
    NextToken(text, length, end, begin, end);
    (void)ReadNumericToken(text + begin, end - begin, s.values[i]);
    NextToken(text, length, end, begin, end);
    (void)ReadNumericToken(text + begin, end - begin, s.times[i]);
    s.curves[i] = 0.f;
    if (width == 3) {
      NextToken(text, length, end, begin, end);
      (void)ReadNumericToken(text + begin, end - begin, s.curves[i]);
    }
  }
  s.count = points;
  writeSlot = published.exchange(writeSlot | FRESH, std::memory_order_acq_rel) & INDEX;
  writing.clear(std::memory_order_release);
}

CALC() {
  const int segments = constant;
  const int mode = curvemode;
  fn.constant(segments > 0 ? static_cast<UInt>(segments) : 0)
      .curved(mode != 0)
      .syncUpdate(syncupdate != 0);
  // The latest envelope, if one arrived since the last block.
  if ((published.load(std::memory_order_acquire) & FRESH) != 0) {
    readSlot = published.exchange(readSlot, std::memory_order_acq_rel) & INDEX;
    const slot& s = slots[readSlot];
    (void)fn.breakpoints(s.initial, s.values.data(), s.times.data(), s.curves.data(), s.count);
  }
  // An unconnected inlet reads 0: a number is the initial value, not a
  // constant ramp (Max's float).
  DSP::buffer& out = fn(inputs[0].Signal(0.f));
  outputs[0].SendBuffer(&out, thread);
}

#undef className
