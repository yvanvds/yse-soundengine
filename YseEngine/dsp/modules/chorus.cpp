/*
  ==============================================================================

    chorus.cpp
    Multichannel chorus/flanger module (issue #161).

  ==============================================================================
*/

#include "chorus.hpp"
#include <algorithm>
#include <cmath>

namespace {
  // Longest delay the line can address, in milliseconds. Lines are sized to
  // this so the sweep never needs to reallocate.
  constexpr Flt MAX_DELAY_MS = 50.0f;
  // A few extra samples past the max delay so the interpolation neighbour and
  // wrap arithmetic always stay in bounds.
  constexpr std::size_t LINE_GUARD = 4;

  // Mode presets (Dattorro / Juno-style ranges): chorus is a longer, gentle
  // sweep; flanger is a short delay that sweeps toward the very short end where
  // the comb notches are audible.
  constexpr Flt CHORUS_BASE_MS = 15.0f;
  constexpr Flt CHORUS_SWEEP_MS = 12.0f;
  constexpr Flt FLANGER_BASE_MS = 1.0f;
  constexpr Flt FLANGER_SWEEP_MS = 7.0f;

  constexpr Flt MAX_FEEDBACK = 0.95f;
  constexpr Flt MIN_RATE_HZ = 0.01f;
  constexpr Flt MAX_RATE_HZ = 20.0f;

  // Time constant (seconds) for the per-sample delay-time smoother — absorbs
  // abrupt depth/mode/spread parameter steps so they do not click.
  constexpr Flt DELAY_SMOOTH_TAU = 0.005f;

  constexpr Flt TWO_PI = 6.283185307179586f;
} // namespace

YSE::DSP::MODULES::chorus::voice::voice() : writePos(0), smoothedDelay(0.0f), primed(false) {}

YSE::DSP::MODULES::chorus::chorus()
  : parmRate(0.8f),
    parmDepth(0.5f),
    parmFeedback(0.0f),
    parmSpread(0.0f),
    parmMode(MODE_CHORUS),
    lfoCursor(0.0f),
    delaySmoothCoef(0.0f),
    lineSize(0),
    blockLength(0) {}

YSE::DSP::MODULES::chorus& YSE::DSP::MODULES::chorus::mode(chorusMode value) {
  parmMode.store(static_cast<Int>(value));
  return *this;
}

YSE::DSP::MODULES::chorusMode YSE::DSP::MODULES::chorus::mode() {
  return static_cast<chorusMode>(parmMode.load());
}

YSE::DSP::MODULES::chorus& YSE::DSP::MODULES::chorus::rate(Flt hz) {
  parmRate.store(std::clamp(hz, MIN_RATE_HZ, MAX_RATE_HZ));
  return *this;
}

Flt YSE::DSP::MODULES::chorus::rate() {
  return parmRate;
}

YSE::DSP::MODULES::chorus& YSE::DSP::MODULES::chorus::depth(Flt value) {
  parmDepth.store(std::clamp(value, 0.0f, 1.0f));
  return *this;
}

Flt YSE::DSP::MODULES::chorus::depth() {
  return parmDepth;
}

YSE::DSP::MODULES::chorus& YSE::DSP::MODULES::chorus::feedback(Flt value) {
  parmFeedback.store(std::clamp(value, -MAX_FEEDBACK, MAX_FEEDBACK));
  return *this;
}

Flt YSE::DSP::MODULES::chorus::feedback() {
  return parmFeedback;
}

YSE::DSP::MODULES::chorus& YSE::DSP::MODULES::chorus::spread(Flt value) {
  parmSpread.store(std::clamp(value, 0.0f, 1.0f));
  return *this;
}

Flt YSE::DSP::MODULES::chorus::spread() {
  return parmSpread;
}

Flt YSE::DSP::MODULES::chorus::baseDelayMs() const {
  return (parmMode.load() == MODE_FLANGER) ? FLANGER_BASE_MS : CHORUS_BASE_MS;
}

Flt YSE::DSP::MODULES::chorus::sweepDelayMs() const {
  return (parmMode.load() == MODE_FLANGER) ? FLANGER_SWEEP_MS : CHORUS_SWEEP_MS;
}

void YSE::DSP::MODULES::chorus::create() {
  // One-pole coefficient for the per-sample delay smoother, from the engine
  // sample rate (computed off the audio thread).
  Flt tauSamples = DELAY_SMOOTH_TAU * static_cast<Flt>(SAMPLERATE);
  if (tauSamples < 1.0f) tauSamples = 1.0f;
  delaySmoothCoef = 1.0f - std::exp(-1.0f / tauSamples);

  // Line length that covers the longest addressable delay at the current rate.
  lineSize =
      static_cast<std::size_t>(MAX_DELAY_MS * 0.001f * static_cast<Flt>(SAMPLERATE)) + LINE_GUARD;
}

void YSE::DSP::MODULES::chorus::process(MULTICHANNELBUFFER& buffer) {
  createIfNeeded();

  if (buffer.empty()) return;

  // (Re)derive the line length from the current sample rate. It only changes on
  // create() or a device restart at a new rate, never in steady state.
  std::size_t wantLine =
      static_cast<std::size_t>(MAX_DELAY_MS * 0.001f * static_cast<Flt>(SAMPLERATE)) + LINE_GUARD;
  bool lineChanged = (wantLine != lineSize);
  lineSize = wantLine;

  // Grow/shrink per-channel voices to the channel count. New voices get a
  // zeroed line of the current length; this is the only allocation path.
  const std::size_t needLine = lineSize;
  voices.ensure(buffer.size(), [needLine](voice& v) {
    v.line.assign(needLine, 0.0f);
    v.writePos = 0;
    v.primed = false;
  });

  // Sample-rate change: re-size every existing line (device-restart resize
  // path, where allocation is permitted). Steady state never enters here.
  if (lineChanged) {
    for (std::size_t ch = 0; ch < voices.size(); ++ch) {
      voices[ch].line.assign(lineSize, 0.0f);
      voices[ch].writePos = 0;
      voices[ch].primed = false;
    }
  }

  const std::size_t length = buffer[0].getLength();

  // (Re)size shared scratch when the block length changes.
  if (length != blockLength) {
    phaseBuf.resize(length);
    wet.resize(length);
    blockLength = length;
  }

  // Build the shared LFO phase for this block (radians). One sweep drives the
  // whole buffer; per-channel spread is applied as a phase offset below.
  const Flt phaseInc = TWO_PI * static_cast<Flt>(parmRate) / static_cast<Flt>(SAMPLERATE);
  {
    Flt cur = lfoCursor;
    Flt* ph = phaseBuf.getPtr();
    for (std::size_t i = 0; i < length; ++i) {
      ph[i] = cur;
      cur += phaseInc;
    }
    cur = std::fmod(cur, TWO_PI);
    if (cur < 0.0f) cur += TWO_PI;
    lfoCursor = cur;
  }

  const Flt base = baseDelayMs();
  const Flt sweep = sweepDelayMs() * static_cast<Flt>(parmDepth);
  const Flt fb = parmFeedback;
  const Flt spreadAmount = parmSpread;
  const Flt srMs = 0.001f * static_cast<Flt>(SAMPLERATE);
  const Flt maxDelaySamps = static_cast<Flt>(lineSize) - 2.0f;
  const std::size_t n = buffer.size();

  const Flt* ph = phaseBuf.getPtr();

  for (std::size_t ch = 0; ch < n; ++ch) {
    voice& v = voices[ch];
    // Even phase spread across the channels for stereo width.
    const Flt offset = spreadAmount * TWO_PI * static_cast<Flt>(ch) / static_cast<Flt>(n);

    Flt* x = buffer[ch].getPtr();
    Flt* w = wet.getPtr();

    // Seed the smoother on first use so the sweep starts from its true value
    // rather than ramping up from zero (which would read a fixed short delay).
    if (!v.primed) {
      v.smoothedDelay = base + sweep * 0.5f * (1.0f + std::sin(ph[0] + offset));
      v.primed = true;
    }
    Flt sd = v.smoothedDelay;
    std::size_t wp = v.writePos;

    for (std::size_t i = 0; i < length; ++i) {
      const Flt lfo = 0.5f * (1.0f + std::sin(ph[i] + offset)); // [0, 1]
      const Flt target = base + sweep * lfo;
      sd += (target - sd) * delaySmoothCoef;

      Flt delaySamps = sd * srMs;
      if (delaySamps < 1.0f) delaySamps = 1.0f;
      if (delaySamps > maxDelaySamps) delaySamps = maxDelaySamps;

      // Fractional (linearly interpolated) read from the circular line.
      Flt rp = static_cast<Flt>(wp) - delaySamps;
      while (rp < 0.0f)
        rp += static_cast<Flt>(lineSize);
      std::size_t i0 = static_cast<std::size_t>(rp);
      const Flt frac = rp - static_cast<Flt>(i0);
      std::size_t i1 = i0 + 1;
      if (i1 >= lineSize) i1 = 0;
      const Flt delayed = v.line[i0] * (1.0f - frac) + v.line[i1] * frac;

      // Write dry input plus the recirculated (feedback) tap, then advance.
      v.line[wp] = x[i] + fb * delayed;
      if (++wp >= lineSize) wp = 0;

      w[i] = delayed; // wet tap
    }

    v.smoothedDelay = sd;
    v.writePos = wp;

    // impact() sets the dry/wet balance.
    calculateImpact(buffer[ch], wet);
  }
}
