/*
  ==============================================================================

    feedbackDelay.cpp
    Multichannel feedback delay module (issue #160).

  ==============================================================================
*/

#include "feedbackDelay.hpp"
#include <algorithm>
#include <cmath>

namespace {
  // Maximum delay time the line can address, in milliseconds. Delay lines are
  // constructed to this size so no reallocation is needed when the user picks a
  // long time.
  constexpr Flt MAX_DELAY_MS = 2000.0f;
  constexpr Flt MIN_DELAY_MS = 1.0f;
  // Upper feedback bound keeps the recirculating loop stable.
  constexpr Flt MAX_FEEDBACK = 0.99f;
  // Time constant (seconds) for the per-sample delay-time smoother.
  constexpr Flt TIME_SMOOTH_TAU = 0.03f;
} // namespace

YSE::DSP::MODULES::feedbackDelay::delayChannel::delayChannel()
  : line(static_cast<Int>(MAX_DELAY_MS)) {}

YSE::DSP::MODULES::feedbackDelay::feedbackDelay()
  : parmTime(250.0f),
    parmFeedback(0.5f),
    parmDamping(8000.0f),
    parmCrossfeed(0.0f),
    currentTime(250.0f),
    timeSmoothCoef(0.0f),
    primed(false),
    blockLength(0) {}

YSE::DSP::MODULES::feedbackDelay& YSE::DSP::MODULES::feedbackDelay::time(Flt ms) {
  parmTime.store(std::clamp(ms, MIN_DELAY_MS, MAX_DELAY_MS));
  return *this;
}

Flt YSE::DSP::MODULES::feedbackDelay::time() {
  return parmTime;
}

YSE::DSP::MODULES::feedbackDelay& YSE::DSP::MODULES::feedbackDelay::feedback(Flt amount) {
  parmFeedback.store(std::clamp(amount, 0.0f, MAX_FEEDBACK));
  return *this;
}

Flt YSE::DSP::MODULES::feedbackDelay::feedback() {
  return parmFeedback;
}

YSE::DSP::MODULES::feedbackDelay& YSE::DSP::MODULES::feedbackDelay::damping(Flt hz) {
  if (hz < 0.0f) hz = 0.0f;
  parmDamping.store(hz);
  return *this;
}

Flt YSE::DSP::MODULES::feedbackDelay::damping() {
  return parmDamping;
}

YSE::DSP::MODULES::feedbackDelay& YSE::DSP::MODULES::feedbackDelay::crossfeed(Flt amount) {
  parmCrossfeed.store(std::clamp(amount, 0.0f, 1.0f));
  return *this;
}

Flt YSE::DSP::MODULES::feedbackDelay::crossfeed() {
  return parmCrossfeed;
}

void YSE::DSP::MODULES::feedbackDelay::create() {
  // One-pole coefficient for the per-sample delay-time smoother. Computed once
  // here (off the audio thread) from the engine sample rate.
  Flt tauSamples = TIME_SMOOTH_TAU * static_cast<Flt>(SAMPLERATE);
  if (tauSamples < 1.0f) tauSamples = 1.0f;
  timeSmoothCoef = 1.0f - std::exp(-1.0f / tauSamples);
  // Per-channel delay lines are sized on the first process() call once the
  // channel count is known, matching the other delay modules.
}

void YSE::DSP::MODULES::feedbackDelay::process(MULTICHANNELBUFFER& buffer) {
  createIfNeeded();

  if (buffer.empty()) return;

  // Grow/shrink per-channel state to match the channel count. Allocation-free
  // once the count is stable.
  channels.ensure(buffer.size());

  const std::size_t length = buffer[0].getLength();

  // (Re)size the shared scratch and per-channel buffers when the block length
  // changes. Steady state hits none of this.
  if (length != blockLength) {
    toWrite.resize(length);
    delayed.resize(length);
    timeBuffer.resize(length);
    crossScratch.resize(length);
    for (std::size_t ch = 0; ch < channels.size(); ++ch) {
      channels[ch].damped.resize(length);
      channels[ch].fbSaved.resize(length); // preserves recirculation history
    }
    blockLength = length;
  }

  const Flt feedbackAmount = parmFeedback;
  const Flt cross = parmCrossfeed;
  const Flt dampingHz = parmDamping;

  // Build the per-sample smoothed delay-time control block (ms). One shared
  // ramp keeps every channel's read position identical and click-free.
  Flt target = std::clamp(parmTime.load(), MIN_DELAY_MS, MAX_DELAY_MS);
  if (!primed) {
    currentTime = target;
    primed = true;
  }
  {
    Flt cur = currentTime;
    Flt* t = timeBuffer.getPtr();
    for (std::size_t i = 0; i < length; ++i) {
      cur += (target - cur) * timeSmoothCoef;
      t[i] = cur;
    }
    currentTime = cur;
  }

  const std::size_t n = buffer.size();

  // Pass 1: recirculate, read the delayed signal, damp it, and mix the wet tap
  // into the output. fbSaved holds the feedback computed on the previous block.
  for (std::size_t ch = 0; ch < n; ++ch) {
    toWrite = buffer[ch]; // dry input (copy)
    toWrite += channels[ch].fbSaved; // + recirculated feedback

    channels[ch].line.process(toWrite); // write & advance the line
    channels[ch].line.read(delayed, timeBuffer); // read the delayed signal

    // Damping low-pass sits in the feedback path; keep an owned copy so the
    // wet tap (delayed) stays undamped and cross-feed can read it in pass 2.
    channels[ch].damper.setFrequency(dampingHz);
    channels[ch].damped = channels[ch].damper(delayed);

    // Wet tap is the raw delayed signal; impact() sets the dry/wet balance.
    calculateImpact(buffer[ch], delayed);
  }

  // Pass 2: compute next block's feedback, mixing in the channel partner for
  // cross-feed. Runs after pass 1 so every channel's damped signal is ready.
  for (std::size_t ch = 0; ch < n; ++ch) {
    std::size_t partner = ch ^ 1u;
    if (partner >= n) partner = ch; // unpaired trailing channel: self only

    channels[ch].fbSaved = channels[ch].damped;
    channels[ch].fbSaved *= (1.0f - cross);
    crossScratch = channels[partner].damped;
    crossScratch *= cross;
    channels[ch].fbSaved += crossScratch;
    channels[ch].fbSaved *= feedbackAmount;
  }
}
