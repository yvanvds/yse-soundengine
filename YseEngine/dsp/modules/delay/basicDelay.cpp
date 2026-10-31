/*
  ==============================================================================

    basicDelay.cpp
    Created: 30 Sep 2015 4:40:17pm
    Author:  yvan

  ==============================================================================
*/

#include "basicDelay.hpp"
#include <algorithm>

namespace {
  // Line length past MAX_DELAY_MS, in ms: see delayChannel's constructor.
  constexpr Int LINE_HEADROOM_MS = 100;
} // namespace

YSE::DSP::MODULES::basicDelay::basicDelay()
  : time0(0.f), time1(0.f), time2(0.f), gain0(0.f), gain1(0.f), gain2(0.f) {}

YSE::DSP::MODULES::basicDelay& YSE::DSP::MODULES::basicDelay::set(basicDelay::DELAY_NR nr, Flt time,
                                                                  Flt gain) {
  // The line only holds MAX_DELAY_MS (plus read headroom); a negative time
  // would wrap in process()'s cast to UInt.
  time = std::clamp(time, 0.f, MAX_DELAY_MS);
  switch (nr) {
  case FIRST:
    time0.store(time);
    gain0.store(gain);
    break;
  case SECOND:
    time1.store(time);
    gain1.store(gain);
    break;
  case THIRD:
    time2.store(time);
    gain2.store(gain);
    break;
  case DELAY_NR_COUNT_:
    break; // sentinel, not a tap
  }

  return *this;
}

Flt YSE::DSP::MODULES::basicDelay::time(basicDelay::DELAY_NR nr) {
  switch (nr) {
  case FIRST:
    return time0;
  case SECOND:
    return time1;
  case THIRD:
    return time2;
  case DELAY_NR_COUNT_:
    break; // sentinel, not a tap
  }
  return time0; // stop complaining about control paths
}

Flt YSE::DSP::MODULES::basicDelay::gain(basicDelay::DELAY_NR nr) {
  switch (nr) {
  case FIRST:
    return gain0;
  case SECOND:
    return gain1;
  case THIRD:
    return gain2;
  case DELAY_NR_COUNT_:
    break; // sentinel, not a tap
  }
  return gain0; // stop complaining about control paths
}

// The line is a fixed length in ms, allocated for DSP::delay's
// HIGHEST_SAMPLERATE, so DSP::delay::process() re-derives its length in
// samples from the live rate without allocating (issues #637, #1212). It used
// to be SAMPLERATE ms long — 48 s at 48 kHz, its memory growing with the rate
// squared — which basicDelay had to grow on the audio thread at a rate change.
//
// DSP::delay::read() can reach back the line length minus one block (the
// offset counts from the start of the last block written), so the line gets
// headroom past MAX_DELAY_MS for the block: 100 ms covers the engine's
// STANDARD_BUFFERSIZE down to 1.28 kHz.
YSE::DSP::MODULES::basicDelay::delayChannel::delayChannel()
  : line(static_cast<Int>(MAX_DELAY_MS) + LINE_HEADROOM_MS) {}

void YSE::DSP::MODULES::basicDelay::create() {
  // Per-channel delay lines are sized lazily in process() once the channel
  // count is known; nothing to allocate up front.
}

void YSE::DSP::MODULES::basicDelay::ensurePreFilter(std::size_t /*count*/) {}
void YSE::DSP::MODULES::basicDelay::applyPreFilter(DSP::buffer& /*buffer*/, std::size_t /*ch*/) {}

void YSE::DSP::MODULES::basicDelay::process(MULTICHANNELBUFFER& buffer) {
  createIfNeeded();

  if (buffer.empty()) return;

  // Grow/shrink per-channel delay lines (and any subclass pre-filter) to match
  // the channel count. Allocation-free once the count is stable.
  channels.ensure(buffer.size());
  ensurePreFilter(buffer.size());

  for (std::size_t ch = 0; ch < buffer.size(); ++ch) {
    // Within the scratch capacity (STANDARD_BUFFERSIZE from construction, or
    // the longest block seen) this resize allocates nothing.
    if (buffer[ch].getLength() != result.getLength()) {
      result.resize(buffer[ch].getLength());
      reader.resize(buffer[ch].getLength());
    }

    result = buffer[ch];
    applyPreFilter(buffer[ch], ch);

    channels[ch].line.process(buffer[ch]);
    Int delayCount = 1; // the original signal

    // add delays to signal
    if (gain0 > 0) {
      channels[ch].line.read(reader, (UInt)time0);
      reader *= gain0;
      result += reader;
      delayCount++;
    }

    if (gain1 > 0) {
      channels[ch].line.read(reader, (UInt)time1);
      reader *= gain1;
      result += reader;
      delayCount++;
    }

    if (gain2 > 0) {
      channels[ch].line.read(reader, (UInt)time2);
      reader *= gain2;
      result += reader;
      delayCount++;
    }

    // adjust total gain
    result *= (1.f / delayCount);
    calculateImpact(buffer[ch], result);
  }
}
