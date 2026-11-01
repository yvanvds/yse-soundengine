#include "dZigzag.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include "../math/gExprEval.h"
#include "../pListArgs.h"
#include "../pSelector.h"

using namespace YSE::PATCHER;

namespace {
  constexpr UInt POINTS = YSE::DSP::zigzag::DEFAULT_POINTS;
  // Room for the longest list (a clear, every point and a play) and then
  // some; a power of two so the wrap is a mask.
  constexpr UInt QUEUE = 4096;
  constexpr UInt QUEUE_MASK = QUEUE - 1;
  // The most indices one 'delete' takes.
  constexpr UInt MAX_DELETE = 64;
  // The most numbers a fixed-size message reads.
  constexpr int MAX_ARGS = 3;

  // The commands, as they travel in the queue. Prefixed: YSE's OUT_TYPE
  // has a BANG that member functions would find first.
  enum OP : int {
    OP_CLEAR,
    OP_ADD, // a point at the end, not played
    OP_PLAY,
    OP_APPEND, // a point at the end, played
    OP_INSERT,
    OP_REMOVE,
    OP_SET,
    OP_BANG,
    OP_BANGDELTA,
    OP_START,
    OP_END,
    OP_JUMP,
    OP_JUMPSTART,
    OP_JUMPEND,
    OP_SKIP,
    OP_STOP,
  };

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

  // Up to `max` numbers from `pos`, into `out`; how many in `n`. False when
  // a token is not a finite number. Tokens past `max` are ignored.
  bool ReadArgs(const char* text, std::size_t length, std::size_t pos, float* out, int max,
                int& n) {
    n = 0;
    std::size_t begin = 0;
    std::size_t end = pos;
    while (n < max) {
      NextToken(text, length, end, begin, end);
      if (begin == end) break;
      if (!ReadNumericToken(text + begin, end - begin, out[n]) || !std::isfinite(out[n])) {
        return false;
      }
      n++;
    }
    return true;
  }

  // Counts the numbers from `pos`; false when a token is not a finite number.
  bool CountNumbers(const char* text, std::size_t length, std::size_t pos, UInt& count) {
    count = 0;
    std::size_t begin = 0;
    std::size_t end = pos;
    for (;;) {
      NextToken(text, length, end, begin, end);
      if (begin == end) return true;
      float number = 0.f;
      if (!ReadNumericToken(text + begin, end - begin, number) || !std::isfinite(number)) {
        return false;
      }
      count++;
    }
  }

  // The next number from `end`, already known to be one.
  float Next(const char* text, std::size_t length, std::size_t& end) {
    std::size_t begin = 0;
    NextToken(text, length, end, begin, end);
    float number = 0.f;
    (void)ReadNumericToken(text + begin, end - begin, number);
    return number;
  }

  Int ToIndex(float value) {
    return static_cast<Int>(std::lround(value));
  }

  int ToMode(float value) {
    const long mode = std::lround(value);
    return mode < 0 ? 0 : (mode > 2 ? 2 : static_cast<int>(mode));
  }

  void Apply(YSE::DSP::zigzag& fn, int op, Int index, Flt a, Flt b) {
    const UInt at = index < 0 ? 0u : static_cast<UInt>(index);
    switch (op) {
    case OP_CLEAR:
      fn.clear();
      break;
    case OP_ADD:
      (void)fn.append(a, b, false);
      break;
    case OP_PLAY:
      fn.play();
      break;
    case OP_APPEND:
      (void)fn.append(a, b, true);
      break;
    case OP_INSERT:
      (void)fn.insert(at, a, b);
      break;
    case OP_REMOVE:
      if (index >= 0) (void)fn.remove(at);
      break;
    case OP_SET:
      if (index >= 0) (void)fn.set(at, a, b);
      break;
    case OP_BANG:
      fn.bang();
      break;
    case OP_BANGDELTA:
      fn.bangDelta(a);
      break;
    case OP_START:
      fn.start(at);
      break;
    case OP_END:
      fn.end(index);
      break;
    case OP_JUMP:
      fn.jump(index, a);
      break;
    case OP_JUMPSTART:
      fn.jumpStart(a);
      break;
    case OP_JUMPEND:
      fn.jumpEnd(a);
      break;
    case OP_SKIP:
      fn.skip(index, a);
      break;
    case OP_STOP:
      fn.stop();
      break;
    default:
      break;
    }
  }
} // namespace

#define className dZigzag

CONSTRUCT_DSP() {
  initial = 0.f;
  loopmode = 0;
  mode = 0;
  speed = 1.f;
  ramptime = 0.f;
  queue.resize(QUEUE);
  scratch.reserve(3 * (kExprValueTextMax + 1));

  // in 0: an index or trigger (signal), a bang, a value (float, int), pairs
  // (list) and the word messages, which arrive as lists
  ADD_IN_0;
  REG_SIGNAL_IN;
  REG_BANG_IN(Bang);
  REG_FLOAT_IN(SetValue);
  REG_INT_IN(SetValueInt);
  REG_LIST_IN(SetList);

  // in 1: the speed (signal, float or int)
  ADD_IN_1;
  REG_SIGNAL_IN;
  REG_FLOAT_IN(SetSpeed);
  REG_INT_IN(SetSpeedInt);

  // out 0: the function; out 1: the index; out 2: dump's lists; out 3: a bang
  // at the end of the range, sent after the block (issue #1037)
  ADD_OUT_BUFFER;
  ADD_OUT_BUFFER;
  ADD_OUT_LIST;
  ADD_OUT_BANG;

  ADD_PARAM(initial);
  ADD_PARAM(loopmode);
  ADD_PARAM(mode);

  ADD_DESCRIPTION("Breakpoint function: a list of points, each a value and a time in "
                  "milliseconds, played as straight ramps forward or backward at any speed, "
                  "looped or swung back and forth, and edited while it plays.");
  ADD_CATEGORY(pCategory::OSC);
  INLET_DOC(0, "points",
            "A list of 'value time' pairs replaces the points and plays them; a number is one "
            "point reached over the ramptime. A bang restarts or steps, as the mode says. A "
            "signal is an index into the points (mode 0) or a trigger where it starts to fall "
            "(modes 1 and 2). Also 'append', 'insert', 'delete', 'set', 'setindex', 'line', "
            "'setline', 'jump', 'jumpstart', 'jumpend', 'next', 'prev', 'skip', 'start', "
            "'end', 'bound', 'bangdelta', 'ramptime', 'speed', 'stop', 'dump', 'loopmode' and "
            "'mode'.",
            "any float");
  INLET_DOC(1, "speed",
            "Play rate: 1 as written, 0.5 half as fast, negative backward, 0 holds. A signal "
            "(read once per block) or a number used while no signal is connected.",
            "any float");
  OUTLET_DOC(0, "out", "The function.", "any float");
  OUTLET_DOC(1, "index",
             "The index of the point being played towards, or the input index in mode 0.", "0+");
  OUTLET_DOC(2, "dump", "On 'dump', one 'index value time' list per point.", "list");
  OUTLET_DOC(3, "end",
             "A bang after the block in which play reached the end of its range and stopped, "
             "looped or turned.",
             "bang");
  PARAM_DOC("initial", "0", "The output before the first point.", "any float");
  PARAM_DOC("loopmode", "0",
            "0 stops at the end, 1 starts again from the other end, 2 turns round.", "0, 1 or 2");
  PARAM_DOC("mode", "0",
            "0: a bang restarts, a signal is an index. 1: a bang restarts, a falling signal "
            "restarts. 2: a bang or a falling signal steps to the next point.",
            "0, 1 or 2");
}

BANG_IN(Bang) {
  (void)inlet;
  (void)thread;
  Single(OP_BANG, 0, 0.f, 0.f);
}

FLOAT_IN(SetValue) {
  (void)inlet;
  (void)thread;
  if (!std::isfinite(value) || !Begin(3)) {
    if (!std::isfinite(value)) dropped.fetch_add(1, std::memory_order_relaxed);
    return;
  }
  Put(OP_CLEAR, 0, 0.f, 0.f);
  Put(OP_ADD, 0, value, ramptime.load());
  Put(OP_PLAY, 0, 0.f, 0.f);
  End();
}

INT_IN(SetValueInt) {
  SetValue(static_cast<float>(value), inlet, thread);
}

FLOAT_IN(SetSpeed) {
  (void)inlet;
  (void)thread;
  if (std::isfinite(value)) speed = value;
}

INT_IN(SetSpeedInt) {
  (void)inlet;
  (void)thread;
  speed = static_cast<float>(value);
}

LIST_IN(SetList) {
  (void)inlet;
  std::size_t begin = 0;
  std::size_t end = 0;
  NextToken(value.c_str(), value.size(), 0, begin, end);
  if (begin == end) return;
  float number = 0.f;
  if (ReadNumericToken(value.c_str() + begin, end - begin, number)) {
    Pairs(value.c_str(), value.size(), 0, true);
  } else {
    Command(value, thread);
  }
}

// Takes the senders' flag and room for `commands`; false, counted, when
// either is not there.
bool dZigzag::Begin(UInt commands) {
  if (writing.test_and_set(std::memory_order_acquire)) {
    dropped.fetch_add(1, std::memory_order_relaxed);
    return false;
  }
  const UInt start = head.load(std::memory_order_relaxed);
  const UInt used = start - tail.load(std::memory_order_acquire);
  if (commands > QUEUE - used) {
    writing.clear(std::memory_order_release);
    dropped.fetch_add(1, std::memory_order_relaxed);
    return false;
  }
  cursor = start;
  return true;
}

// Queues one command and applies it to the sending side's copy.
void dZigzag::Put(int op, Int index, Flt a, Flt b) {
  command& c = queue[cursor & QUEUE_MASK];
  c.op = op;
  c.index = index;
  c.a = a;
  c.b = b;
  cursor++;
  Apply(shadow, op, index, a, b);
}

// Publishes what was put since Begin() and releases the flag.
void dZigzag::End() {
  head.store(cursor, std::memory_order_release);
  writing.clear(std::memory_order_release);
}

void dZigzag::Single(int op, Int index, Flt a, Flt b) {
  if (!Begin(1)) return;
  Put(op, index, a, b);
  End();
}

// 'value time' pairs from `pos`, a last single value with time 0, replacing
// the list. Read twice in place, once to check and once to queue, so a
// malformed list is refused whole.
void dZigzag::Pairs(const char* text, std::size_t length, std::size_t pos, bool play) {
  UInt numbers = 0;
  if (!CountNumbers(text, length, pos, numbers) || numbers == 0) {
    dropped.fetch_add(1, std::memory_order_relaxed);
    return;
  }
  UInt points = (numbers + 1) / 2;
  if (points > POINTS) {
    points = POINTS;
    truncated.fetch_add(1, std::memory_order_relaxed);
  }
  if (!Begin(points + 2)) return;
  Put(OP_CLEAR, 0, 0.f, 0.f);
  std::size_t end = pos;
  for (UInt i = 0; i < points; ++i) {
    const float v = Next(text, length, end);
    const float t = 2 * i + 1 < numbers ? Next(text, length, end) : 0.f;
    Put(OP_ADD, 0, v, t);
  }
  if (play) Put(OP_PLAY, 0, 0.f, 0.f);
  End();
}

// 'insert <index> <value> <time> ...': the pairs go in before the index.
void dZigzag::Insert(const char* text, std::size_t length, std::size_t pos) {
  UInt numbers = 0;
  if (!CountNumbers(text, length, pos, numbers) || numbers < 2) {
    dropped.fetch_add(1, std::memory_order_relaxed);
    return;
  }
  std::size_t end = pos;
  const Int index = ToIndex(Next(text, length, end));
  if (index < 0) {
    dropped.fetch_add(1, std::memory_order_relaxed);
    return;
  }
  const UInt rest = numbers - 1;
  UInt points = (rest + 1) / 2;
  if (points > POINTS) {
    points = POINTS;
    truncated.fetch_add(1, std::memory_order_relaxed);
  }
  if (!Begin(points)) return;
  for (UInt i = 0; i < points; ++i) {
    const float v = Next(text, length, end);
    const float t = 2 * i + 1 < rest ? Next(text, length, end) : 0.f;
    Put(OP_INSERT, index + static_cast<Int>(i), v, t);
  }
  End();
}

// 'delete <index> ...': the indices name points of the list as it was, so
// they are removed from the highest down.
void dZigzag::Delete(const char* text, std::size_t length, std::size_t pos) {
  UInt numbers = 0;
  if (!CountNumbers(text, length, pos, numbers) || numbers == 0 || numbers > MAX_DELETE) {
    dropped.fetch_add(1, std::memory_order_relaxed);
    return;
  }
  Int indices[MAX_DELETE];
  std::size_t end = pos;
  for (UInt i = 0; i < numbers; ++i)
    indices[i] = ToIndex(Next(text, length, end));
  std::sort(indices, indices + numbers, [](Int a, Int b) { return a > b; });
  const UInt unique = static_cast<UInt>(std::unique(indices, indices + numbers) - indices);
  if (!Begin(unique)) return;
  for (UInt i = 0; i < unique; ++i)
    Put(OP_REMOVE, indices[i], 0.f, 0.f);
  End();
}

// The word messages. A missing or malformed number refuses the message.
void dZigzag::Command(const std::string& message, THREAD thread) {
  const char* text = message.c_str();
  const std::size_t length = message.size();
  std::size_t begin = 0;
  std::size_t end = 0;
  NextToken(text, length, 0, begin, end);

  if (Is(text, begin, end, "line") || Is(text, begin, end, "list")) {
    Pairs(text, length, end, true);
    return;
  }
  if (Is(text, begin, end, "setline")) {
    Pairs(text, length, end, false);
    return;
  }
  if (Is(text, begin, end, "insert")) {
    Insert(text, length, end);
    return;
  }
  if (Is(text, begin, end, "delete")) {
    Delete(text, length, end);
    return;
  }
  if (Is(text, begin, end, "dump")) {
    Dump(thread);
    return;
  }

  float args[MAX_ARGS] = {0.f, 0.f, 0.f};
  int n = 0;
  if (!ReadArgs(text, length, end, args, MAX_ARGS, n)) {
    dropped.fetch_add(1, std::memory_order_relaxed);
    return;
  }

  // Messages with no number, or an optional time.
  if (Is(text, begin, end, "bang")) {
    Single(OP_BANG, 0, 0.f, 0.f);
  } else if (Is(text, begin, end, "stop")) {
    Single(OP_STOP, 0, 0.f, 0.f);
  } else if (Is(text, begin, end, "jumpstart")) {
    Single(OP_JUMPSTART, 0, args[0], 0.f);
  } else if (Is(text, begin, end, "jumpend")) {
    Single(OP_JUMPEND, 0, args[0], 0.f);
  } else if (Is(text, begin, end, "next")) {
    Single(OP_SKIP, 1, args[0], 0.f);
  } else if (Is(text, begin, end, "prev")) {
    Single(OP_SKIP, -1, args[0], 0.f);
  } else if (n == 0) {
    // Everything below needs a number.
    dropped.fetch_add(1, std::memory_order_relaxed);
  } else if (Is(text, begin, end, "append")) {
    Single(OP_APPEND, 0, args[0], args[1]);
  } else if (Is(text, begin, end, "set") || Is(text, begin, end, "setindex")) {
    if (n < 2) {
      dropped.fetch_add(1, std::memory_order_relaxed);
      return;
    }
    Single(OP_SET, ToIndex(args[0]), args[1], args[2]);
  } else if (Is(text, begin, end, "jump")) {
    Single(OP_JUMP, ToIndex(args[0]), args[1], 0.f);
  } else if (Is(text, begin, end, "skip")) {
    Single(OP_SKIP, ToIndex(args[0]), args[1], 0.f);
  } else if (Is(text, begin, end, "start")) {
    Single(OP_START, ToIndex(args[0]), 0.f, 0.f);
  } else if (Is(text, begin, end, "end")) {
    Single(OP_END, ToIndex(args[0]), 0.f, 0.f);
  } else if (Is(text, begin, end, "bound")) {
    if (n < 2 || !Begin(2)) {
      if (n < 2) dropped.fetch_add(1, std::memory_order_relaxed);
      return;
    }
    Put(OP_START, ToIndex(args[0]), 0.f, 0.f);
    Put(OP_END, ToIndex(args[1]), 0.f, 0.f);
    End();
  } else if (Is(text, begin, end, "bangdelta")) {
    Single(OP_BANGDELTA, 0, args[0], 0.f);
  } else if (Is(text, begin, end, "ramptime")) {
    ramptime = args[0];
  } else if (Is(text, begin, end, "speed")) {
    speed = args[0];
  } else if (Is(text, begin, end, "loopmode")) {
    loopmode = ToMode(args[0]);
  } else if (Is(text, begin, end, "mode")) {
    mode = ToMode(args[0]);
  }
}

// One 'index value time' list per point of the sending side's copy, each
// read under the flag and sent after releasing it, so a receiver can edit
// the list in return. The scratch text was reserved in the constructor.
void dZigzag::Dump(THREAD thread) {
  for (UInt i = 0;; ++i) {
    if (writing.test_and_set(std::memory_order_acquire)) {
      dropped.fetch_add(1, std::memory_order_relaxed);
      return;
    }
    const bool inside = i < shadow.size();
    const Flt v = shadow.pointValue(i);
    const Flt t = shadow.pointDelta(i);
    writing.clear(std::memory_order_release);
    if (!inside) return;

    char number[kExprValueTextMax];
    char index[FORMAT_INT_WIDTH];
    scratch.clear();
    scratch.append(index, WriteInt(static_cast<int>(i), index));
    int written = ExprFormatValue(ExprValue::Float(v), number, kExprValueTextMax);
    scratch.push_back(' ');
    if (written > 0) scratch.append(number, static_cast<std::size_t>(written));
    written = ExprFormatValue(ExprValue::Float(t), number, kExprValueTextMax);
    scratch.push_back(' ');
    if (written > 0) scratch.append(number, static_cast<std::size_t>(written));
    outputs[2].SendList(scratch, thread);
  }
}

CALC() {
  fn.loopMode(static_cast<DSP::zigzag::LOOP>(loopmode.load()));
  fn.mode(static_cast<DSP::zigzag::MODE>(mode.load()));
  if (!started) {
    fn.hold(initial.load());
    started = true;
  }

  // A connected signal wins over the number (issue #1036); an unconnected
  // left inlet means play. The speed is set first, so a list that arrived
  // with it plays in its direction.
  DSP::buffer* input = inputs[0].IsConstant() ? nullptr : &inputs[0].Signal(0.f);
  DSP::buffer* rate = inputs[1].IsConstant() ? nullptr : &inputs[1].Signal(0.f);
  fn.speed(rate != nullptr ? rate->getPtr()[0] : speed.load());

  // Every command that arrived since the last block, in order.
  const UInt end = head.load(std::memory_order_acquire);
  UInt next = tail.load(std::memory_order_relaxed);
  while (next != end) {
    const command& c = queue[next & QUEUE_MASK];
    Apply(fn, c.op, c.index, c.a, c.b);
    next++;
  }
  tail.store(next, std::memory_order_release);

  DSP::buffer& out = fn(input, nullptr);
  // Never Send* a control message from CALC(): the patcher sends it after
  // the traversal.
  if (fn.finished()) outputs[3].ArmBang();
  outputs[0].SendBuffer(&out, thread);
  outputs[1].SendBuffer(&fn.indices(), thread);
}

#undef className
