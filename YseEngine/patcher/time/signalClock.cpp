#include "signalClock.h"

using namespace YSE::PATCHER;

void signalClock::Bind(clockBridge* clocks, const char* name, std::size_t length) {
  if (clocks == nullptr || name == nullptr || length == 0) {
    handle.store(0, std::memory_order_release);
    return;
  }
  // A refusal (a full bridge, a name too long for a slot) leaves 0: the
  // object then has no clock and its ramp holds, as for a clock that does not
  // exist.
  handle.store(clocks->Bind(name, length), std::memory_order_release);
}

const YSE::DSP::beatSpan& signalClock::Read(const clockBridge* clocks, UInt length) {
  const clockBridge::Handle bound = handle.load(std::memory_order_acquire);
  double beat = 0.0;
  float bpm = 0.f;
  if (clocks == nullptr || bound == 0 || !clocks->Beat(bound, beat) || !clocks->Tempo(bound, bpm)) {
    span.lose();
    return span;
  }
  span.read(beat, bpm, length);
  return span;
}
