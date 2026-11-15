#include "dLevelmeter.h"
#include "meterCommon.h"

using namespace YSE::PATCHER;
using namespace YSE::PATCHER::meterCommon;
using YSE::DSP::levelMeter;

namespace {
  constexpr unsigned int CELLS = 3; // dB, hold dB, clip
  constexpr int LAST_MODE = static_cast<int>(levelMeter::EBU);
} // namespace

#define className dLevelmeter

CONSTRUCT_DSP() {
  REG_PARM_CLEAR;
  REG_PARM_PARSE;
  mode = 0;
  interval = 20.f;
  hold = 0.f;
  fn.detector(levelMeter::RMS);

  // in 0: the signal; a bang sends the reading, a number is shown and sent
  // out; 'mode', 'attack', 'release', 'offset', 'interval', 'hold', 'clear'
  ADD_IN_0;
  REG_SIGNAL_IN;
  REG_BANG_IN(Report);
  REG_FLOAT_IN(SetReading);
  REG_INT_IN(SetReadingInt);

  // out 0: the reading in dB every interval, sent after the block (#1037)
  ADD_OUT_FLOAT;

  ADD_PARAM(mode);
  ADD_PARAM(interval);
  ADD_PARAM(hold);

  ADD_DESCRIPTION("An RMS level meter: measures the RMS level of a signal in dB every update "
                  "interval with the ballistics of a VU meter or a peak programme meter, sends "
                  "it out, and keeps the reading, a held peak and a clip indicator for the host "
                  "to draw.");
  ADD_CATEGORY(pCategory::ANALYSIS);
  INLET_DOC(0, "signal",
            "The signal to measure; unconnected it reads as silence. A bang sends the latest "
            "reading; a number is shown as that reading in dB and sent out. Also takes 'mode "
            "<0-5>', 'attack <ms>', 'release <ms>', 'offset <dB>', 'interval <ms>', 'hold <ms>' "
            "and 'clear', which turns the clip indicator off and drops the held peak.",
            "any float");
  OUTLET_DOC(0, "dB",
             "The RMS level in dB after the ballistics, against the offset, sent every update "
             "interval. -200 for silence.",
             "-200 or more dB");
  PARAM_DOC("mode", "0",
            "The ballistics: attack, release and the dBFS that reads 0. 0 fast (none), 1 VU "
            "(330 ms, 330 ms, -20 dBFS), 2 DIN (10 ms, 1500 ms, -14 dBFS), 3 BBC (10 ms, 3000 "
            "ms, -14 dBFS), 4 Nordic (5 ms, 2000 ms, -14 dBFS), 5 EBU (0 ms, 1500 ms, -14 dBFS) "
            "('~levelmeter 1').",
            "0-5");
  PARAM_DOC("interval", "20",
            "The update interval in milliseconds: the window the RMS is taken over, and how "
            "often the reading is sent ('~levelmeter 0 50'). 10 to 2000.",
            "10-2000 ms");
  PARAM_DOC("hold", "0",
            "How long the held peak stays up, in milliseconds, before it drops to the reading "
            "('~levelmeter 1 20 2000'). 0 makes the held peak the reading.",
            "0 or more ms");
}

void dLevelmeter::ApplyMode(int value) {
  const int clamped = value < 0 ? 0 : (value > LAST_MODE ? LAST_MODE : value);
  const levelMeter::ballisticsSettings s =
      levelMeter::settings(static_cast<levelMeter::BALLISTICS>(clamped));
  attack = s.attack;
  release = s.release;
  offset = s.offset;
}

PARM_CLEAR() {
  mode = 0;
  interval = 20.f;
  hold = 0.f;
  ApplyMode(0);
}

PARM_PARSE() {
  ApplyMode(mode.load());
}

void dLevelmeter::SetReadingFrom(float dB, YSE::THREAD thread) {
  pending = dB;
  setRequested = true;
  outputs[0].SendFloat(dB, thread);
}

BANG_IN(Report) {
  (void)inlet;
  (void)thread;
  report = true;
}

FLOAT_IN(SetReading) {
  (void)inlet;
  SetReadingFrom(value, thread);
}

INT_IN(SetReadingInt) {
  (void)inlet;
  SetReadingFrom(static_cast<float>(value), thread);
}

MESSAGES() {
  (void)value;
  // 'clear', and the settings that take a number; a missing or malformed
  // number is ignored.
  std::size_t next = 0;
  const std::string_view word = Token(message, 0, next);
  if (word == "clear") {
    clearRequested = true;
    return;
  }
  float number = 0.f;
  if (!Number(message, next, number)) return;
  if (word == "mode") {
    // Max converts a float to an int; clamped first so the cast is defined.
    const auto most = static_cast<float>(LAST_MODE);
    ApplyMode(static_cast<int>(number < 0.f ? 0.f : (number > most ? most : number)));
  } else if (word == "attack") {
    attack = number;
  } else if (word == "release") {
    release = number;
  } else if (word == "offset") {
    offset = number;
  } else if (word == "interval") {
    interval = number;
  } else if (word == "hold") {
    hold = number;
  }
}

CALC() {
  (void)thread;
  fn.interval(Interval(interval.load()))
      .hold(hold.load())
      .attack(attack.load())
      .release(release.load())
      .offset(offset.load());
  if (clearRequested.exchange(false)) fn.clear();
  if (setRequested.exchange(false)) fn.setDb(pending.load());
  // Never Send* from CALC(): the patcher sends these after the traversal.
  // A bang sends the reading up to the end of the last block.
  if (report.exchange(false)) outputs[0].ArmFloat(fn.levelDb());
  // Unconnected, the inlet reads as silence (#1036).
  if (fn(inputs[0].Signal(0.f)) > 0) outputs[0].ArmFloat(fn.levelDb());
  shownLevel.store(fn.levelDb(), std::memory_order_relaxed);
  shownHold.store(fn.peakHoldDb(), std::memory_order_relaxed);
  shownClip.store(fn.clipped(), std::memory_order_relaxed);
}

// Host thread: the three cells, read from the atomics CALC() writes.
std::string dLevelmeter::GetGuiValue() {
  return GetGuiValueAt(0) + ' ' + GetGuiValueAt(1) + ' ' + GetGuiValueAt(2);
}

unsigned int dLevelmeter::GetGuiValueCount() const {
  return CELLS;
}

std::string dLevelmeter::GetGuiValueAt(unsigned int index) {
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
