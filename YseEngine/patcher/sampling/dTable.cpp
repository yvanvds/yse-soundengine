#include "dTable.h"
#include <cmath>
#include <cstring>
#include "../pListArgs.h"
#include "../pSelector.h"

using namespace YSE::PATCHER;

namespace {

  // The command queue. A power of two so the wrap is a mask; far more than
  // a block's worth of messages.
  constexpr UInt QUEUE = 64;
  constexpr UInt QUEUE_MASK = QUEUE - 1;

  // The commands, as they travel in the queue. Prefixed: YSE's OUT_TYPE
  // has a BANG that member functions would find first.
  enum OP : int {
    OP_SET, // a number for the input
    OP_GOTO,
    OP_NEXT,
    OP_PREV,
    OP_DRAW,
  };

  // One word of `text` from `pos`: [begin, end). Empty at the end. Walked in
  // place: no allocation on whichever thread sends.
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

  int Clamp(int value, int lo, int hi) {
    return value < lo ? lo : (value > hi ? hi : value);
  }

  int ToInt(float value) {
    if (!(value > -2147483648.f)) return -2147483647;
    if (!(value < 2147483648.f)) return 2147483647;
    return static_cast<int>(value);
  }

} // namespace

#define className dTable

CONSTRUCT_DSP() {
  interp = 0;
  extend = 0;
  inLo = 0.f;
  inHi = 0.f;
  outLo = 0.f;
  outHi = 0.f;
  inputmode = 0;
  triggermode = 0;
  range = DSP::tableLookup::DEFAULT_RANGE;
  isSigned = 0;
  queue.resize(QUEUE);

  // in 0: the input (signal, float or int), bang, and the word messages
  ADD_IN_0;
  REG_SIGNAL_IN;
  REG_FLOAT_IN(SetInput);
  REG_INT_IN(SetInputInt);
  REG_BANG_IN(Bang);

  // out 0: the values read
  ADD_OUT_BUFFER;

  REG_PARM_PARSE;
  ADD_PARAM(bufferName);
  ADD_PARAM(interp);
  ADD_PARAM(extend);
  ADD_PARAM(inLo);
  ADD_PARAM(inHi);
  ADD_PARAM(outLo);
  ADD_PARAM(outHi);
  ADD_PARAM(inputmode);
  ADD_PARAM(triggermode);
  ADD_PARAM(range);
  ADD_PARAM(isSigned);

  ADD_DESCRIPTION("Reads a named sample buffer as a table of values at signal rate: looked up "
                  "at an index, stepped through on triggers, or drawn from at random with each "
                  "entry's value as its weight.");
  ADD_CATEGORY(pCategory::SAMPLING);
  INLET_DOC(0, "input",
            "A signal, or a number used while no signal is connected. In inputmode 0 the index "
            "to read, in 1 and 2 a trigger. 'bang' jumps to a random entry; 'goto <index>', "
            "'next' and 'prev' move the position. 'interp', 'extend', 'inmap', 'outscale', "
            "'inputmode', 'triggermode', 'range' and 'signed' set the attributes, 'name <name>' "
            "or 'set <name>' switches buffers (none of them saved with the patch).",
            "any float");
  OUTLET_DOC(0, "out",
             "The entries read, scaled by outscale. Silence without a buffer, or while it is "
             "empty.",
             "any float");
  PARAM_DOC("name", "", "The buffer read as the table: its first channel.", "buffer name");
  PARAM_DOC("interp", "0",
            "0 reads the entry at or below the index, 1 a line between the two around it, 2 the "
            "nearest.",
            "0, 1 or 2");
  PARAM_DOC("extend", "0",
            "Outside the table: 0 reads 0, 1 the first or last entry, 2 wraps around, 3 keeps "
            "the output as it was.",
            "0 to 3");
  PARAM_DOC("inmap_lo", "0",
            "The input that reads entry 0. With inmap_hi it scales an input range to the table's "
            "length; equal ends turn the map off.",
            "any float");
  PARAM_DOC("inmap_hi", "0", "The input that reads one past the last entry.", "any float");
  PARAM_DOC("outscale_lo", "0",
            "What the bottom of the value range comes out as. With outscale_hi it scales the "
            "value range to an output range; equal ends turn the scale off.",
            "any float");
  PARAM_DOC("outscale_hi", "0", "What the top of the value range comes out as.", "any float");
  PARAM_DOC("inputmode", "0",
            "0: the input is the index. 1: each trigger steps to the next entry. 2: each "
            "trigger jumps to a random entry.",
            "0, 1 or 2");
  PARAM_DOC("triggermode", "0",
            "What counts as a trigger in inputmodes 1 and 2: 0 a change from 0 to anything "
            "else, 1 any change.",
            "0 or 1");
  PARAM_DOC("range", "128",
            "The table's value range for outscale: 0 to range - 1, or -range to range when "
            "signed.",
            "1 or higher");
  PARAM_DOC("signed", "0", "1 makes the value range -range to range.", "0 or 1");
}

void dTable::SetParent(pObject* newParent) {
  pObject::SetParent(newParent);
  // The scheduler a rebind goes through, built here so a message on the
  // audio thread finds it.
  EnableFileIO();
}

// Control thread: the name is a string parameter, so a change of argument
// rebuilds the object and this runs on one no audio thread has seen.
PARM_PARSE() {
  binding.Bind(bufferName);
  interp = Clamp(interp.load(), 0, 2);
  extend = Clamp(extend.load(), 0, 3);
  inputmode = Clamp(inputmode.load(), 0, 2);
  triggermode = Clamp(triggermode.load(), 0, 1);
  if (range.load() < 1) range = 1;
  isSigned = isSigned.load() != 0 ? 1 : 0;
}

// Queues one command, or counts it refused.
void dTable::Queue(int op, Flt value) {
  if (writing.test_and_set(std::memory_order_acquire)) {
    dropped.fetch_add(1, std::memory_order_relaxed);
    return;
  }
  const UInt at = head.load(std::memory_order_relaxed);
  if (at - tail.load(std::memory_order_acquire) >= QUEUE) {
    writing.clear(std::memory_order_release);
    dropped.fetch_add(1, std::memory_order_relaxed);
    return;
  }
  command& c = queue[at & QUEUE_MASK];
  c.op = op;
  c.value = value;
  head.store(at + 1, std::memory_order_release);
  writing.clear(std::memory_order_release);
}

FLOAT_IN(SetInput) {
  (void)inlet;
  (void)thread;
  Queue(OP_SET, value);
}

INT_IN(SetInputInt) {
  (void)inlet;
  (void)thread;
  Queue(OP_SET, static_cast<Flt>(value));
}

BANG_IN(Bang) {
  (void)inlet;
  (void)thread;
  Queue(OP_DRAW, 0.f);
}

// The word messages, on any thread. Read in place: nothing allocates.
MESSAGES() {
  (void)value;
  if (binding.HandleSet(*this, message)) return;
  const char* text = message.c_str();
  const std::size_t length = message.size();
  std::size_t begin = 0;
  std::size_t end = 0;
  NextToken(text, length, 0, begin, end);
  if (begin == end) return;
  std::size_t argBegin = 0;
  std::size_t argEnd = 0;
  NextToken(text, length, end, argBegin, argEnd);

  if (Is(text, begin, end, "name")) {
    binding.Request(*this, text + argBegin, argEnd - argBegin);
    return;
  }
  if (Is(text, begin, end, "next")) {
    Queue(OP_NEXT, 0.f);
    return;
  }
  if (Is(text, begin, end, "prev")) {
    Queue(OP_PREV, 0.f);
    return;
  }

  float a = 0.f;
  const bool hasA = ReadNumericToken(text + argBegin, argEnd - argBegin, a) && std::isfinite(a);
  if (Is(text, begin, end, "inmap") || Is(text, begin, end, "outscale")) {
    const bool in = Is(text, begin, end, "inmap");
    float b = 0.f;
    std::size_t bBegin = 0;
    std::size_t bEnd = 0;
    NextToken(text, length, argEnd, bBegin, bEnd);
    const bool none = Is(text, argBegin, argEnd, "none");
    const bool pair = hasA && ReadNumericToken(text + bBegin, bEnd - bBegin, b) && std::isfinite(b);
    if (!none && !pair) return;
    if (none) a = b = 0.f;
    (in ? inLo : outLo) = a;
    (in ? inHi : outHi) = b;
    return;
  }
  if (!hasA) return;
  if (Is(text, begin, end, "goto")) {
    Queue(OP_GOTO, a);
  } else if (Is(text, begin, end, "interp")) {
    interp = Clamp(ToInt(a), 0, 2);
  } else if (Is(text, begin, end, "extend")) {
    extend = Clamp(ToInt(a), 0, 3);
  } else if (Is(text, begin, end, "inputmode")) {
    inputmode = Clamp(ToInt(a), 0, 2);
  } else if (Is(text, begin, end, "triggermode")) {
    triggermode = Clamp(ToInt(a), 0, 1);
  } else if (Is(text, begin, end, "range")) {
    range = ToInt(a) < 1 ? 1 : ToInt(a);
  } else if (Is(text, begin, end, "signed")) {
    isSigned = a != 0.f ? 1 : 0;
  }
}

void dTable::Apply(const command& c, const DSP::sampleView& view) {
  switch (c.op) {
  case OP_SET:
    fn.set(view, c.value);
    break;
  case OP_GOTO:
    fn.goTo(c.value);
    break;
  case OP_NEXT:
    fn.next(view);
    break;
  case OP_PREV:
    fn.prev(view);
    break;
  case OP_DRAW:
    fn.draw(view);
    break;
  default:
    break;
  }
}

CALC() {
  binding.Take(*this); // a rebind waiting since the last block
  DSP::sampleView view = binding.Current().pin();

  fn.interp(static_cast<DSP::tableLookup::interpolation>(interp.load()))
      .extend(static_cast<DSP::tableLookup::outOfRange>(extend.load()))
      .mode(static_cast<DSP::tableLookup::inputMode>(inputmode.load()))
      .trigger(static_cast<DSP::tableLookup::triggerMode>(triggermode.load()))
      .inputMap(inLo, inHi)
      .outputScale(outLo, outHi)
      .range(range)
      .signedRange(isSigned.load() != 0);

  // Every command that arrived since the last block, in order.
  const UInt end = head.load(std::memory_order_acquire);
  UInt next = tail.load(std::memory_order_relaxed);
  while (next != end) {
    Apply(queue[next & QUEUE_MASK], view);
    next++;
  }
  tail.store(next, std::memory_order_release);

  // A connected signal wins over the number (issue #1036).
  DSP::buffer& out = inputs[0].IsConstant() ? fn(view) : fn(view, inputs[0].Signal(0.f));
  outputs[0].SendBuffer(&out, thread);
}

#undef className
