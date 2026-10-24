#include "dTechno.h"

#include <cmath>
#include <cstring>
#include "../pListArgs.h"
#include "../pSelector.h"

using namespace YSE::PATCHER;

namespace {
  constexpr int MAX_STEPS = static_cast<int>(YSE::DSP::techno::MAX_STEPS);

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

  // A step number, truncated; false for one that is not finite or is below 0.
  bool ToStep(float value, UInt& step) {
    if (!std::isfinite(value) || value < 0.f) return false;
    step = value >= 4294967040.f ? 0xFFFFFF00u : static_cast<UInt>(value);
    return true;
  }
} // namespace

#define className dTechno

CONSTRUCT_DSP() {
  phase = 0.f;

  // in 0: the phase (signal, float or int), and every message
  ADD_IN_0;
  REG_SIGNAL_IN;
  REG_FLOAT_IN(SetPhase);
  REG_INT_IN(SetPhaseInt);

  // out 0: pitch; out 1: amplitude envelope; out 2: position in steps
  ADD_OUT_BUFFER;
  ADD_OUT_BUFFER;
  ADD_OUT_BUFFER;

  REG_PARM_CLEAR;
  REG_PARM_PARSE;
  ADD_PARAM(steps);

  ADD_DESCRIPTION("A step sequencer read by a 0-1 phase signal: per step a pitch, an amplitude, a "
                  "pitch slide and attack and decay shapes, out as pitch and envelope signals. "
                  "For drum patterns and acid lines at sample accuracy.");
  ADD_CATEGORY(pCategory::SEQUENCE);
  INLET_DOC(0, "phase",
            "The phase over the whole sequence, clamped to 0-1 (a ~phasor); a number while no "
            "signal is connected. Also takes 'pitch', 'amplitude', 'curve', 'attack', 'decay' "
            "and 'pos', each '<step> <value>', 'length <n>' and 'repeatpos <v> ...'.",
            "0-1");
  OUTLET_DOC(0, "pitch", "The current step's pitch in Hz, sliding from the previous one's.",
             "any float");
  OUTLET_DOC(1, "amplitude",
             "The amplitude envelope: the step's note decaying and the next one's rising.",
             "any float");
  OUTLET_DOC(2, "position", "Where the sequence is, in steps: the step plus how far through it.",
             "0 to length");
  PARAM_DOC("steps", "16",
            "How many steps the sequencer has room for; 'length' uses fewer. Changing it "
            "rebuilds the object.",
            "1-1024");
  Build();
}

// Control thread: the callbacks make ParamsNeedRebuild() true, so a live
// change rebuilds the object and the allocation never happens on a sequencer
// the audio thread is rendering. Clear runs first on every Set, parse only
// when there are arguments.
PARM_CLEAR() {
  steps = static_cast<int>(DSP::techno::DEFAULT_STEPS);
  sequencer = DSP::techno(DSP::techno::DEFAULT_STEPS);
  Build();
}

PARM_PARSE() {
  if (steps < 1) steps = 1;
  if (steps > MAX_STEPS) steps = MAX_STEPS;
  sequencer = DSP::techno(static_cast<UInt>(steps));
  Build();
}

void dTechno::Build() {
  const UInt capacity = sequencer.capacity();
  // A repeatpos for every step is capacity + 1 events; room for two.
  UInt size = 256;
  while (size < 2 * (capacity + 1))
    size <<= 1;
  ring.assign(size, event{});
  mask = size - 1;
  head.store(0, std::memory_order_relaxed);
  tail.store(0, std::memory_order_relaxed);
  repeatStage.assign(capacity, 0.f);
}

FLOAT_IN(SetPhase) {
  (void)inlet;
  (void)thread;
  phase = value;
}

INT_IN(SetPhaseInt) {
  (void)inlet;
  (void)thread;
  phase = static_cast<float>(value);
}

bool dTechno::Room(UInt count) const {
  const UInt used = tail.load(std::memory_order_relaxed) - head.load(std::memory_order_acquire);
  return static_cast<UInt>(ring.size()) - used >= count;
}

void dTechno::Push(kind what, UInt step, Flt value) {
  if (writing.test_and_set(std::memory_order_acquire)) {
    dropped.fetch_add(1, std::memory_order_relaxed);
    return;
  }
  if (Room(1)) {
    const UInt at = tail.load(std::memory_order_relaxed);
    ring[at & mask] = event{what, step, value};
    tail.store(at + 1, std::memory_order_release);
  } else {
    dropped.fetch_add(1, std::memory_order_relaxed);
  }
  writing.clear(std::memory_order_release);
}

// `repeatpos v ...`: one REPEAT_VALUE per number (up to the capacity) and a
// REPEAT, all or nothing. Numbers are read in place, twice: once to count,
// once to write, so the request is refused whole rather than half applied.
void dTechno::PushRepeat(const char* text, std::size_t from, std::size_t length) {
  const UInt capacity = static_cast<UInt>(repeatStage.size());
  UInt numbers = 0;
  std::size_t begin = 0;
  std::size_t end = from;
  for (;;) {
    NextToken(text, length, end, begin, end);
    if (begin == end) break;
    float number = 0.f;
    if (ReadNumericToken(text + begin, end - begin, number)) numbers++;
  }
  // A segment longer than the capacity can never divide the length.
  if (numbers >= capacity) {
    dropped.fetch_add(1, std::memory_order_relaxed);
    return;
  }

  if (writing.test_and_set(std::memory_order_acquire)) {
    dropped.fetch_add(1, std::memory_order_relaxed);
    return;
  }
  if (!Room(numbers + 1)) {
    dropped.fetch_add(1, std::memory_order_relaxed);
    writing.clear(std::memory_order_release);
    return;
  }
  UInt at = tail.load(std::memory_order_relaxed);
  UInt written = 0;
  end = from;
  while (written < numbers) {
    NextToken(text, length, end, begin, end);
    if (begin == end) break;
    float number = 0.f;
    if (!ReadNumericToken(text + begin, end - begin, number)) continue;
    ring[at & mask] = event{kind::REPEAT_VALUE, written, number};
    at++;
    written++;
  }
  ring[at & mask] = event{kind::REPEAT, written, 0.f};
  tail.store(at + 1, std::memory_order_release);
  writing.clear(std::memory_order_release);
}

MESSAGES() {
  (void)value;
  const char* text = message.c_str();
  const std::size_t length = message.size();
  std::size_t begin = 0;
  std::size_t end = 0;
  NextToken(text, length, 0, begin, end);
  if (begin == end) return;

  if (Is(text, begin, end, "repeatpos")) {
    PushRepeat(text, end, length);
    return;
  }

  std::size_t arg = 0;
  std::size_t argEnd = 0;
  NextToken(text, length, end, arg, argEnd);
  float first = 0.f;
  if (!ReadNumericToken(text + arg, argEnd - arg, first)) return;

  if (Is(text, begin, end, "length")) {
    UInt count = 0;
    if (ToStep(first, count)) Push(kind::LENGTH, count, 0.f);
    return;
  }

  kind what = kind::PITCH;
  if (Is(text, begin, end, "pitch")) {
    what = kind::PITCH;
  } else if (Is(text, begin, end, "amplitude")) {
    what = kind::AMPLITUDE;
  } else if (Is(text, begin, end, "curve")) {
    what = kind::CURVE;
  } else if (Is(text, begin, end, "attack")) {
    what = kind::ATTACK;
  } else if (Is(text, begin, end, "decay")) {
    what = kind::DECAY;
  } else if (Is(text, begin, end, "pos")) {
    what = kind::POSITION;
  } else {
    return;
  }
  UInt step = 0;
  if (!ToStep(first, step) || step >= static_cast<UInt>(repeatStage.size())) return;
  std::size_t valueBegin = 0;
  std::size_t valueEnd = 0;
  NextToken(text, length, argEnd, valueBegin, valueEnd);
  float number = 0.f;
  if (!ReadNumericToken(text + valueBegin, valueEnd - valueBegin, number)) return;
  Push(what, step, number);
}

void dTechno::Apply() {
  UInt at = head.load(std::memory_order_relaxed);
  const UInt last = tail.load(std::memory_order_acquire);
  for (; at != last; ++at) {
    const event& e = ring[at & mask];
    switch (e.what) {
    case kind::PITCH:
      sequencer.pitch(e.step, e.value);
      break;
    case kind::AMPLITUDE:
      sequencer.amplitude(e.step, e.value);
      break;
    case kind::CURVE:
      sequencer.curve(e.step, e.value);
      break;
    case kind::ATTACK:
      sequencer.attack(e.step, e.value);
      break;
    case kind::DECAY:
      sequencer.decay(e.step, e.value);
      break;
    case kind::POSITION:
      sequencer.position(e.step, e.value);
      break;
    case kind::LENGTH:
      sequencer.length(e.step);
      break;
    case kind::REPEAT_VALUE:
      if (e.step < repeatStage.size()) repeatStage[e.step] = e.value;
      break;
    case kind::REPEAT:
      if (e.step < repeatStage.size()) sequencer.repeatPositions(repeatStage.data(), e.step);
      break;
    }
  }
  head.store(last, std::memory_order_release);
}

CALC() {
  Apply();
  // An unconnected phase inlet reads its last number (#1036).
  DSP::buffer& out = sequencer(inputs[0].Signal(phase));
  outputs[0].SendBuffer(&out, thread);
  outputs[1].SendBuffer(&sequencer.envelope(), thread);
  outputs[2].SendBuffer(&sequencer.stepPosition(), thread);
}

#undef className
