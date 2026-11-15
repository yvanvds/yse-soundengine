#include "dMeter.h"
#include "meterCommon.h"

using namespace YSE::PATCHER;
using namespace YSE::PATCHER::meterCommon;

namespace {
  constexpr unsigned int CELLS = 3; // level, hold, clip
} // namespace

#define className dMeter

CONSTRUCT_DSP() {
  interval = 50.f;
  hold = 0.f;

  // in 0: the signal; a number is shown and sent out; 'interval <ms>',
  // 'hold <ms>', 'clear'
  ADD_IN_0;
  REG_SIGNAL_IN;
  REG_FLOAT_IN(SetLevel);
  REG_INT_IN(SetLevelInt);

  // out 0: the peak of every interval, sent after the block (issue #1037)
  ADD_OUT_FLOAT;

  ADD_PARAM(interval);
  ADD_PARAM(hold);

  ADD_DESCRIPTION("A peak meter: measures the peak amplitude of a signal every update interval "
                  "and sends it out, and keeps the level, a held peak and a clip indicator for "
                  "the host to draw as LEDs.");
  ADD_CATEGORY(pCategory::ANALYSIS);
  INLET_DOC(0, "signal",
            "The signal to measure; unconnected it reads as silence. A number is shown as if a "
            "signal with that peak had arrived, and is sent out. Also takes 'interval <ms>', "
            "'hold <ms>' and 'clear', which turns the clip indicator off and drops the held "
            "peak.",
            "any float");
  OUTLET_DOC(0, "peak",
             "The peak amplitude of the last update interval, the largest absolute sample "
             "value in it, sent every interval.",
             "0 or more");
  PARAM_DOC("interval", "50",
            "The update interval in milliseconds: how often the level is measured and sent "
            "('~meter 100'). 10 to 2000.",
            "10-2000 ms");
  PARAM_DOC("hold", "0",
            "How long the held peak stays up, in milliseconds, before it drops to the level "
            "('~meter 50 1500'). 0 makes the held peak the level.",
            "0 or more ms");
}

FLOAT_IN(SetLevel) {
  (void)inlet;
  pending = value;
  setRequested = true;
  outputs[0].SendFloat(value, thread);
}

INT_IN(SetLevelInt) {
  (void)inlet;
  const auto number = static_cast<float>(value);
  pending = number;
  setRequested = true;
  outputs[0].SendFloat(number, thread);
}

MESSAGES() {
  (void)value;
  // 'interval <ms>', 'hold <ms>' and 'clear'; a missing or malformed number
  // is ignored.
  std::size_t next = 0;
  const std::string_view word = Token(message, 0, next);
  if (word == "clear") {
    clearRequested = true;
    return;
  }
  float number = 0.f;
  if (word == "interval") {
    if (Number(message, next, number)) interval = number;
  } else if (word == "hold") {
    if (Number(message, next, number)) hold = number;
  }
}

CALC() {
  (void)thread;
  fn.interval(Interval(interval.load())).hold(hold.load());
  if (clearRequested.exchange(false)) fn.clear();
  if (setRequested.exchange(false)) fn.set(pending.load());
  // Unconnected, the inlet reads as silence (#1036). Never Send* from
  // CALC(): the patcher sends the peak after the traversal.
  if (fn(inputs[0].Signal(0.f)) > 0) outputs[0].ArmFloat(fn.level());
  shownLevel.store(fn.level(), std::memory_order_relaxed);
  shownHold.store(fn.peakHold(), std::memory_order_relaxed);
  shownClip.store(fn.clipped(), std::memory_order_relaxed);
}

// Host thread: the three cells, read from the atomics CALC() writes.
std::string dMeter::GetGuiValue() {
  return GetGuiValueAt(0) + ' ' + GetGuiValueAt(1) + ' ' + GetGuiValueAt(2);
}

unsigned int dMeter::GetGuiValueCount() const {
  return CELLS;
}

std::string dMeter::GetGuiValueAt(unsigned int index) {
  switch (index) {
  case 0:
    return Cell(shownLevel.load(std::memory_order_relaxed));
  case 1:
    return Cell(shownHold.load(std::memory_order_relaxed));
  case 2:
    return shownClip.load(std::memory_order_relaxed) ? "1" : "0";
  default:
    return std::string();
  }
}

#undef className
