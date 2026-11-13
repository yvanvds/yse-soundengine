#include "pLine.h"

#include <cmath>
#include <cstring>
#include "../../internal/rtLogQueue.h"
#include "../pListArgs.h"
#include "../pSelector.h"

using namespace YSE::PATCHER;

namespace {
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

  // The numbers of a segment list, read in place. Max's initial-value form
  // `<value>, <target> <time> ...` is a first number followed by a comma,
  // attached (`0,`) or on its own (`0 ,`); `lead` says one was found.
  struct NumberReader {
    const char* text;
    std::size_t length;
    std::size_t pos = 0;
    unsigned int token = 0;
    bool lead = false;

    NumberReader(const char* t, std::size_t l) : text(t), length(l) {}

    // 1: a number in `out`; 0: the end; -1: something that is not a number.
    int Next(float& out) {
      for (;;) {
        std::size_t begin = 0;
        std::size_t end = 0;
        NextToken(text, length, pos, begin, end);
        pos = end;
        if (begin == end) return 0;
        std::size_t size = end - begin;
        const unsigned int t = token++;
        if (t == 0 && size > 1 && text[end - 1] == ',') {
          size--;
          lead = true;
        } else if (t == 1 && !lead && size == 1 && text[begin] == ',') {
          lead = true;
          continue;
        }
        // Rejects NaN and infinities too.
        if (!ReadNumericToken(text + begin, size, out)) return -1;
        return 1;
      }
    }
  };

  // The transport messages, as they travel in `control`.
  enum COMMAND : unsigned int {
    NONE = 0,
    STOP = 1,
    PAUSE = 2,
    RESUME = 3,
  };
  constexpr unsigned int COMMAND_MASK = 3;

  void Apply(YSE::DSP::lineSegments& fn, unsigned int command) {
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

  // What arrived since the last block, to apply in arrival order.
  enum EVENT : int {
    EVENT_LIST = 0,
    EVENT_JUMP = 1,
    EVENT_COMMAND = 2,
  };
  struct Event {
    unsigned int serial;
    EVENT kind;
  };

  constexpr char TRUNCATED[] = "~line: a segment list longer than maxpoints was cut to maxpoints";
} // namespace

#define className pLine

CONSTRUCT_DSP() {
  for (slot& s : slots) {
    s.targets.assign(CAPACITY, 0.f);
    s.times.assign(CAPACITY, 0.f);
  }
  // A cut list logs through the real-time queue, whose first touch
  // allocates its ring: take it here, on the control thread.
  (void)INTERNAL::RtLog();

  // in 0: a target (float, int), segments (list) and the messages
  ADD_IN_0;
  REG_FLOAT_IN(SetTarget);
  REG_INT_IN(SetTargetInt);
  REG_LIST_IN(SetList);

  // in 1: the time for targets (float, int)
  ADD_IN_1;
  REG_FLOAT_IN(SetTime);
  REG_INT_IN(SetTimeInt);

  // out 0: the ramp; out 1: a bang when a list finishes, sent after the
  // block (issue #1037); out 2: the busy state as a signal.
  ADD_OUT_BUFFER;
  ADD_OUT_BANG;
  ADD_OUT_BUFFER;

  ADD_PARAM(initial);
  ADD_PARAM(time);
  ADD_PARAM(maxpoints);

  ADD_DESCRIPTION("Linear ramp generator: ramps toward a target over a time in milliseconds, or "
                  "plays a list of target-time pairs back to back for envelopes and glides. "
                  "Bangs when the list is done.");
  ADD_CATEGORY(pCategory::OSC);
  INLET_DOC(0, "target",
            "A number is a target, reached over the right inlet's time, or jumped to when that "
            "time is 0. A list is 'target time' pairs played back to back (a last single number "
            "is a jump); 'value, target time ...' jumps to the value first. Also 'stop', "
            "'pause' and 'resume'.",
            "any float");
  INLET_DOC(1, "time", "Ramp time in milliseconds for numbers at the left inlet; kept.", "0+ ms");
  OUTLET_DOC(0, "out", "Audio-rate ramp output.", "any float");
  OUTLET_DOC(1, "done", "A bang after the block in which a list or a jump arrived.", "bang");
  OUTLET_DOC(2, "active",
             "A signal: 1 while a list is heading for a target, 0 when it is done, stopped or "
             "paused.",
             "0 or 1");
  PARAM_DOC("initial", "0", "The value before the first target.", "any float");
  PARAM_DOC("time", "0", "Ramp time in milliseconds until one arrives at the right inlet.",
            "0+ ms");
  PARAM_DOC("maxpoints", "129",
            "The most segments a list keeps, an initial value included; a longer list is cut "
            "and logged.",
            "1 to 1024");
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

unsigned int pLine::NextSerial() {
  return (serial.fetch_add(1, std::memory_order_relaxed) + 1) & SERIAL_MASK;
}

void pLine::Command(const std::string& message) {
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
    control.store((NextSerial() << 2) | command, std::memory_order_release);
    return;
  }
  // `0, 1 1000` reaches the command channel: its first token is not a
  // number. A number before the comma makes it Max's initial-value list.
  float number = 0.f;
  if (end - begin > 1 && text[end - 1] == ',' &&
      ReadNumericToken(text + begin, end - begin - 1, number)) {
    Segments(message);
  }
}

// Numbers the filled write slot and hands it to the audio thread. The
// caller holds `writing`.
void pLine::Publish(slot& s) {
  s.serial = NextSerial();
  writeSlot = published.exchange(writeSlot | FRESH, std::memory_order_acq_rel) & INDEX;
  writing.clear(std::memory_order_release);
}

void pLine::Jump(Flt value) {
  jumpValue.store(value, std::memory_order_relaxed);
  jumpTag.store(NextSerial(), std::memory_order_release);
}

void pLine::Target(Flt value) {
  if (!std::isfinite(value)) {
    dropped.fetch_add(1, std::memory_order_relaxed);
    return;
  }
  const Flt ms = time.load();
  if (!(ms > 0.f) || !std::isfinite(ms)) {
    Jump(value);
    return;
  }
  if (writing.test_and_set(std::memory_order_acquire)) {
    dropped.fetch_add(1, std::memory_order_relaxed);
    return;
  }
  slot& s = slots[writeSlot];
  s.targets[0] = value;
  s.times[0] = ms;
  s.count = 1;
  Publish(s);
}

// One line per log tick at most, whoever sends: never blocks or allocates.
void pLine::ReportTruncation() {
  truncated.fetch_add(1, std::memory_order_relaxed);
  INTERNAL::rtLogQueue& log = INTERNAL::RtLog();
  const std::uint64_t now = log.tick();
  if (loggedTick.exchange(now, std::memory_order_relaxed) == now) return;
  (void)log.post(TRUNCATED, sizeof(TRUNCATED) - 1);
}

// A list of numbers: read twice in place, once to count and once to write,
// so a malformed list is refused whole rather than half applied.
void pLine::Segments(const std::string& message) {
  const char* text = message.c_str();
  const std::size_t length = message.size();
  NumberReader counter(text, length);
  UInt numbers = 0;
  for (;;) {
    float number = 0.f;
    const int read = counter.Next(number);
    if (read == 0) break;
    if (read < 0) {
      dropped.fetch_add(1, std::memory_order_relaxed);
      return;
    }
    numbers++;
  }
  if (numbers == 0) return;
  const bool lead = counter.lead;

  // An initial value is a segment of time 0; then pairs, a last single
  // number a jump.
  const UInt rest = numbers - (lead ? 1 : 0);
  UInt segments = (lead ? 1 : 0) + rest / 2 + rest % 2;
  int limit = maxpoints.load();
  if (limit < 1) limit = 1;
  if (static_cast<UInt>(limit) > CAPACITY) limit = static_cast<int>(CAPACITY);
  if (segments > static_cast<UInt>(limit)) {
    segments = static_cast<UInt>(limit);
    ReportTruncation();
  }

  NumberReader reader(text, length);
  if (segments == 1) {
    // A jump on its own travels as one, so it orders with a list.
    float a = 0.f;
    float b = 0.f;
    (void)reader.Next(a);
    if (!lead && reader.Next(b) == 1 && b > 0.f) {
      if (writing.test_and_set(std::memory_order_acquire)) {
        dropped.fetch_add(1, std::memory_order_relaxed);
        return;
      }
      slot& s = slots[writeSlot];
      s.targets[0] = a;
      s.times[0] = b;
      s.count = 1;
      Publish(s);
      return;
    }
    Jump(a);
    return;
  }

  if (writing.test_and_set(std::memory_order_acquire)) {
    dropped.fetch_add(1, std::memory_order_relaxed);
    return;
  }
  slot& s = slots[writeSlot];
  // Every token was read as a number above: these reads cannot fail.
  UInt i = 0;
  if (lead) {
    (void)reader.Next(s.targets[0]);
    s.times[0] = 0.f;
    i = 1;
  }
  for (; i < segments; ++i) {
    (void)reader.Next(s.targets[i]);
    if (reader.Next(s.times[i]) != 1) s.times[i] = 0.f;
  }
  s.count = segments;
  Publish(s);
}

CALC() {
  if (!started) {
    fn.jump(initial.load());
    started = true;
  }

  // The latest list, jump and command that arrived since the last block,
  // applied in the order they arrived.
  Event events[3];
  int n = 0;
  const bool haveList = (published.load(std::memory_order_acquire) & FRESH) != 0;
  if (haveList) {
    readSlot = published.exchange(readSlot, std::memory_order_acq_rel) & INDEX;
    events[n++] = {slots[readSlot].serial, EVENT_LIST};
  }
  const unsigned int j = jumpTag.load(std::memory_order_acquire);
  if (j != jumpSeen) {
    jumpSeen = j;
    events[n++] = {j, EVENT_JUMP};
  }
  const unsigned int c = control.load(std::memory_order_acquire);
  if (c != controlSeen) {
    controlSeen = c;
    events[n++] = {c >> 2, EVENT_COMMAND};
  }
  for (int a = 1; a < n; ++a) {
    for (int b = a; b > 0 && After(events[b - 1].serial, events[b].serial); --b) {
      const Event e = events[b];
      events[b] = events[b - 1];
      events[b - 1] = e;
    }
  }
  for (int e = 0; e < n; ++e) {
    switch (events[e].kind) {
    case EVENT_LIST: {
      const slot& s = slots[readSlot];
      (void)fn.segments(s.targets.data(), s.times.data(), s.count);
      break;
    }
    case EVENT_JUMP:
      fn.set(jumpValue.load(std::memory_order_relaxed), 0.f);
      break;
    case EVENT_COMMAND:
      Apply(fn, c & COMMAND_MASK);
      break;
    }
  }

  DSP::buffer& out = fn();
  // Never Send* a control message from CALC(): the patcher sends it after
  // the traversal.
  if (fn.finished()) outputs[1].ArmBang();
  outputs[0].SendBuffer(&out, thread);
  outputs[2].SendBuffer(&fn.activity(), thread);
}

#undef className
