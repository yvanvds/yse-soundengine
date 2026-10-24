#include "dRate.h"
#include "../pListArgs.h"
#include "../pSelector.h"

using namespace YSE::PATCHER;

namespace {
  // The next token of `message` from `cursor` on, walked in place so nothing
  // allocates on whichever thread delivers the message. False at the end.
  bool NextToken(const std::string& message, std::size_t& cursor, std::size_t& begin,
                 std::size_t& length) {
    const char* text = message.c_str();
    const std::size_t size = message.size();
    while (cursor < size && IsSelectorSeparator(text[cursor]))
      cursor++;
    begin = cursor;
    while (cursor < size && !IsSelectorSeparator(text[cursor]))
      cursor++;
    length = cursor - begin;
    return length > 0;
  }

  bool Is(const std::string& message, std::size_t begin, std::size_t length, const char* name,
          std::size_t nameLength) {
    return length == nameLength && message.compare(begin, nameLength, name) == 0;
  }

  // `sync` takes Max's numbers or names; -1 when it is neither.
  int SyncValue(const std::string& message, std::size_t begin, std::size_t length) {
    float number = 0.f;
    if (ReadNumericToken(message.c_str() + begin, length, number)) {
      const int value = static_cast<int>(number);
      return value >= 0 && value <= 2 ? value : -1;
    }
    if (Is(message, begin, length, "cycle", 5)) return 0;
    if (Is(message, begin, length, "lock", 4)) return 1;
    if (Is(message, begin, length, "off", 3)) return 2;
    return -1;
  }
} // namespace

#define className dRate

CONSTRUCT_DSP() {
  multiplier = 1.f;
  jumpValue = 0.f;
  jumpAt = 0.f;

  // in 0: the ramp to follow (signal); float / int jump the output; messages
  ADD_IN_0;
  REG_SIGNAL_IN;
  REG_FLOAT_IN(SetPhase);
  REG_INT_IN(SetPhaseInt);

  // in 1: the multiplier (signal, or a number while unconnected)
  ADD_IN_1;
  REG_SIGNAL_IN;
  REG_FLOAT_IN(SetMultiplier);
  REG_INT_IN(SetMultiplierInt);

  ADD_OUT_BUFFER;

  ADD_PARAM(multiplier);
  ADD_PARAM(sync);
  ADD_PARAM(oneshot);

  ADD_DESCRIPTION("Time-scales a 0-1 ramp such as a ~phasor: a multiplier below 1 makes several "
                  "ramps per input cycle, above 1 one ramp over several, kept in phase with the "
                  "input. Derives faster and slower clocks from one master ramp.");
  ADD_CATEGORY(pCategory::OSC);
  INLET_DOC(0, "phase",
            "The ramp to follow, a signal from 0 to 1 (unconnected reads 0). A number jumps the "
            "output to it. 'goto <value> [<time>]' jumps now, or when the input reaches time; "
            "'reset' restarts in phase on the next input wrap; 'oneshot <0|1>'; 'sync <0|1|2>' "
            "or 'sync <cycle|lock|off>'.",
            "0-1");
  INLET_DOC(1, "multiplier",
            "Cycle-length multiplier: below 1 faster, above 1 slower, negative backwards; 0 is "
            "ignored. A signal, or a number used while no signal is connected.",
            "any non-zero float");
  OUTLET_DOC(0, "out", "The time-scaled ramp.", "0-1");
  PARAM_DOC("multiplier", "1", "Cycle-length multiplier used while no signal is connected.",
            "any non-zero float");
  PARAM_DOC("sync", "2",
            "0 (cycle): back in phase at every input wrap; 1 (lock): back in phase when the "
            "multiplier changes; 2 (off): never, a new multiplier only changes the speed.",
            "0-2");
  PARAM_DOC("oneshot", "0", "1: stop after one output ramp and hold until a goto or reset.", "0-1");
}

void dRate::PostJump(float value, float at, int kind) {
  jumpValue = value;
  jumpAt = at;
  jumpPending = kind;
}

FLOAT_IN(SetPhase) {
  (void)inlet;
  (void)thread;
  PostJump(value, 0.f, 1);
}

INT_IN(SetPhaseInt) {
  (void)inlet;
  (void)thread;
  PostJump(static_cast<float>(value), 0.f, 1);
}

FLOAT_IN(SetMultiplier) {
  (void)inlet;
  (void)thread;
  multiplier = value;
}

INT_IN(SetMultiplierInt) {
  (void)inlet;
  (void)thread;
  multiplier = static_cast<float>(value);
}

MESSAGES() {
  (void)value;
  std::size_t cursor = 0;
  std::size_t begin = 0;
  std::size_t length = 0;
  if (!NextToken(message, cursor, begin, length)) return;

  if (Is(message, begin, length, "reset", 5)) {
    resetPending = true;
    return;
  }

  const bool isGoto = Is(message, begin, length, "goto", 4);
  const bool isOneshot = Is(message, begin, length, "oneshot", 7);
  const bool isSync = Is(message, begin, length, "sync", 4);
  if (!isGoto && !isOneshot && !isSync) return;

  std::size_t arg = 0;
  std::size_t argLength = 0;
  if (!NextToken(message, cursor, arg, argLength)) return;

  if (isSync) {
    const int mode = SyncValue(message, arg, argLength);
    if (mode >= 0) sync = mode;
    return;
  }

  float number = 0.f;
  if (!ReadNumericToken(message.c_str() + arg, argLength, number)) return;
  if (isOneshot) {
    oneshot = number != 0.f ? 1 : 0;
    return;
  }

  // goto <value> [<time>]
  float at = 0.f;
  std::size_t time = 0;
  std::size_t timeLength = 0;
  if (NextToken(message, cursor, time, timeLength)) {
    if (!ReadNumericToken(message.c_str() + time, timeLength, at)) return;
    PostJump(number, at, 2);
  } else {
    PostJump(number, 0.f, 1);
  }
}

CALC() {
  const int mode = sync;
  fn.multiplier(multiplier)
      .sync(mode == 0
                ? DSP::phasorRate::syncMode::cycle
                : (mode == 1 ? DSP::phasorRate::syncMode::lock : DSP::phasorRate::syncMode::off))
      .oneshot(oneshot != 0);
  if (resetPending.exchange(false)) fn.reset();
  const int jump = jumpPending.exchange(0);
  if (jump == 1) fn.jump(jumpValue);
  if (jump == 2) fn.jump(jumpValue, jumpAt);

  // Unconnected inlets read as their numbers; a connected signal wins (#1036).
  DSP::buffer& in = inputs[0].Signal(0.f);
  DSP::buffer& out = inputs[1].IsConstant() ? fn(in) : fn(in, inputs[1].Signal(multiplier));
  outputs[0].SendBuffer(&out, thread);
}

#undef className
