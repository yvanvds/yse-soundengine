#include "dCurve.h"

#include <cmath>
#include <cstring>
#include "../pListArgs.h"
#include "../pSelector.h"

using namespace YSE::PATCHER;

namespace {
  constexpr UInt POINTS = YSE::DSP::curveSegments::DEFAULT_POINTS;
  constexpr unsigned int SERIAL_MASK = 0x3FFFFFFFu; // what fits beside a command

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

  // The transport messages, as they travel in `control`.
  enum COMMAND : unsigned int {
    NONE = 0,
    STOP = 1,
    PAUSE = 2,
    RESUME = 3,
  };
  constexpr unsigned int COMMAND_MASK = 3;

  void Apply(YSE::DSP::curveSegments& fn, unsigned int command) {
    switch (command) {
    case STOP:
      fn.stop();
      break;
    case PAUSE:
      fn.pause();
      break;
    case RESUME:
      fn.resume();
      break;
    default:
      break;
    }
  }

  // Whether serial `a` came after serial `b`, both SERIAL_MASK wide.
  bool After(unsigned int a, unsigned int b) {
    return static_cast<int>((a - b) << 2) > 0;
  }
} // namespace

#define className dCurve

CONSTRUCT_DSP() {
  initial = 0.f;
  curve = 0.f;
  shapemode = 0;
  time = 0.f;
  for (slot& s : slots) {
    s.targets.assign(POINTS, 0.f);
    s.times.assign(POINTS, 0.f);
    s.curves.assign(POINTS, 0.f);
  }

  // in 0: a target (float, int), segments (list) and the messages
  ADD_IN_0;
  REG_FLOAT_IN(SetTarget);
  REG_INT_IN(SetTargetInt);
  REG_LIST_IN(SetList);

  // in 1: the time for the next target (float, int)
  ADD_IN_1;
  REG_FLOAT_IN(SetTime);
  REG_INT_IN(SetTimeInt);

  // in 2: the curvature (float, int)
  ADD_IN_2;
  REG_FLOAT_IN(SetCurve);
  REG_INT_IN(SetCurveInt);

  // out 0: the ramp; out 1: a bang when a list finishes; out 2: the active
  // state. Both control outlets are sent after the block (issue #1037).
  ADD_OUT_BUFFER;
  ADD_OUT_BANG;
  ADD_OUT_INT;

  ADD_PARAM(initial);
  ADD_PARAM(curve);
  ADD_PARAM(shapemode);

  ADD_DESCRIPTION("Curved ramp generator: plays a list of segments back to back, each a target, "
                  "a time in milliseconds and a curvature, for envelopes, fades and glides that "
                  "bend. Bangs when the list is done.");
  ADD_CATEGORY(pCategory::OSC);
  INLET_DOC(0, "target",
            "A number is a target, reached over the middle inlet's time with the right inlet's "
            "curve, or jumped to with no time. A list is 'target time curve' triplets (a last "
            "pair uses the right inlet's curve). Also 'stop', 'pause', 'resume', "
            "'factor <curve>' and 'shapemode <0|1>'.",
            "any float");
  INLET_DOC(1, "time",
            "Time in milliseconds for the next number at the left inlet; used once, then 0.",
            "0+ ms");
  INLET_DOC(2, "curve",
            "Curvature for numbers and pairs: 0 straight, positive starts slowly, negative "
            "starts quickly.",
            "-1 to 1");
  OUTLET_DOC(0, "out", "The ramp.", "any float");
  OUTLET_DOC(1, "done", "A bang after the block in which a list finished.", "bang");
  OUTLET_DOC(2, "active", "1 when a list starts or resumes, 0 when it finishes, stops or pauses.",
             "0 or 1");
  PARAM_DOC("initial", "0", "The value before the first target.", "any float");
  PARAM_DOC("curve", "0", "Initial curvature for numbers and pairs.", "-1 to 1");
  PARAM_DOC("shapemode", "0",
            "0: the power curve .function draws; 1: an exponential curve, gentler at the ends.",
            "0 or 1");
}

FLOAT_IN(SetTarget) {
  (void)inlet;
  (void)thread;
  Target(value);
}

INT_IN(SetTargetInt) {
  (void)inlet;
  (void)thread;
  Target(static_cast<Flt>(value));
}

FLOAT_IN(SetTime) {
  (void)inlet;
  (void)thread;
  time = value;
}

INT_IN(SetTimeInt) {
  (void)inlet;
  (void)thread;
  time = static_cast<Flt>(value);
}

FLOAT_IN(SetCurve) {
  (void)inlet;
  (void)thread;
  curve = value;
}

INT_IN(SetCurveInt) {
  (void)inlet;
  (void)thread;
  curve = static_cast<Flt>(value);
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
    Segments(value);
  } else {
    Command(value);
  }
}

MESSAGES() {
  (void)value;
  Command(message);
}

void dCurve::Command(const std::string& message) {
  const char* text = message.c_str();
  const std::size_t length = message.size();
  std::size_t begin = 0;
  std::size_t end = 0;
  NextToken(text, length, 0, begin, end);
  unsigned int command = NONE;
  if (Is(text, begin, end, "stop")) {
    command = STOP;
  } else if (Is(text, begin, end, "pause")) {
    command = PAUSE;
  } else if (Is(text, begin, end, "resume")) {
    command = RESUME;
  }
  if (command != NONE) {
    const unsigned int number = (serial.fetch_add(1, std::memory_order_relaxed) + 1) & SERIAL_MASK;
    control.store((number << 2) | command, std::memory_order_release);
    return;
  }

  std::size_t arg = 0;
  std::size_t argEnd = 0;
  NextToken(text, length, end, arg, argEnd);
  float number = 0.f;
  if (!ReadNumericToken(text + arg, argEnd - arg, number) || std::isnan(number)) return;
  if (Is(text, begin, end, "factor")) {
    curve = number;
  } else if (Is(text, begin, end, "shapemode")) {
    shapemode = number != 0.f ? 1 : 0;
  }
}

// Numbers the filled write slot and hands it to the audio thread. The
// caller holds `writing`.
void dCurve::Publish(slot& s) {
  s.serial = (serial.fetch_add(1, std::memory_order_relaxed) + 1) & SERIAL_MASK;
  writeSlot = published.exchange(writeSlot | FRESH, std::memory_order_acq_rel) & INDEX;
  writing.clear(std::memory_order_release);
}

void dCurve::Target(Flt value) {
  // The middle inlet's time is used once, even by a target refused below.
  const Flt ms = time.exchange(0.f);
  if (!std::isfinite(value) || writing.test_and_set(std::memory_order_acquire)) {
    dropped.fetch_add(1, std::memory_order_relaxed);
    return;
  }
  slot& s = slots[writeSlot];
  s.targets[0] = value;
  s.times[0] = ms;
  s.curves[0] = curve.load();
  s.count = 1;
  Publish(s);
}

// A list of numbers: read twice in place, once to count and once to write,
// so a malformed list is refused whole rather than half applied.
void dCurve::Segments(const std::string& message) {
  const char* text = message.c_str();
  const std::size_t length = message.size();
  UInt numbers = 0;
  std::size_t begin = 0;
  std::size_t end = 0;
  for (;;) {
    NextToken(text, length, end, begin, end);
    if (begin == end) break;
    float number = 0.f;
    if (!ReadNumericToken(text + begin, end - begin, number) || !std::isfinite(number)) {
      dropped.fetch_add(1, std::memory_order_relaxed);
      return;
    }
    numbers++;
  }

  // Triplets; a last pair takes the right inlet's curve, a last single
  // number is a jump.
  const UInt extra = numbers % 3;
  UInt segments = numbers / 3 + (extra != 0 ? 1 : 0);
  if (segments > POINTS) {
    segments = POINTS;
    truncated.fetch_add(1, std::memory_order_relaxed);
  }

  if (writing.test_and_set(std::memory_order_acquire)) {
    dropped.fetch_add(1, std::memory_order_relaxed);
    return;
  }
  slot& s = slots[writeSlot];
  const Flt fallback = curve.load();
  end = 0;
  UInt left = numbers;
  // Every token was read as a number above: these reads cannot fail.
  for (UInt i = 0; i < segments; ++i) {
    const UInt width = left >= 3 ? 3 : left;
    left -= width;
    NextToken(text, length, end, begin, end);
    (void)ReadNumericToken(text + begin, end - begin, s.targets[i]);
    s.times[i] = 0.f;
    s.curves[i] = fallback;
    if (width >= 2) {
      NextToken(text, length, end, begin, end);
      (void)ReadNumericToken(text + begin, end - begin, s.times[i]);
    }
    if (width == 3) {
      NextToken(text, length, end, begin, end);
      (void)ReadNumericToken(text + begin, end - begin, s.curves[i]);
    }
  }
  s.count = segments;
  Publish(s);
}

CALC() {
  const DSP::curveSegments::SHAPE wanted =
      shapemode.load() != 0 ? DSP::curveSegments::EXPONENTIAL : DSP::curveSegments::POWER;
  if (fn.shape() != wanted) fn.shape(wanted);
  if (!started) {
    fn.jump(initial.load());
    started = true;
  }
  const bool wasActive = fn.active();

  // The latest list, if one arrived since the last block, and the latest
  // command, applied in the order they arrived.
  const bool haveList = (published.load(std::memory_order_acquire) & FRESH) != 0;
  if (haveList) readSlot = published.exchange(readSlot, std::memory_order_acq_rel) & INDEX;
  const slot& s = slots[readSlot];
  const unsigned int c = control.load(std::memory_order_acquire);
  const bool haveCommand = c != controlSeen;
  controlSeen = c;
  const bool commandLast = haveList && haveCommand && After(c >> 2, s.serial);
  if (haveCommand && !commandLast) Apply(fn, c & COMMAND_MASK);
  if (haveList) (void)fn.segments(s.targets.data(), s.times.data(), s.curves.data(), s.count);
  if (commandLast) Apply(fn, c & COMMAND_MASK);

  DSP::buffer& out = fn();
  // Never Send* a control message from CALC(): the patcher sends it after
  // the traversal.
  if (fn.finished()) outputs[1].ArmBang();
  if (fn.active() != wasActive) outputs[2].ArmInt(fn.active() ? 1 : 0);
  outputs[0].SendBuffer(&out, thread);
}

#undef className
