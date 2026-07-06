/*
  ==============================================================================

    plateReverb.cpp
    Dattorro plate reverb module (issue #162).

  ==============================================================================
*/

#include "plateReverb.hpp"
#include <algorithm>
#include <cmath>

namespace {
  // Dattorro publishes his delay/allpass lengths (in samples) at a 29761 Hz
  // reference rate. Every length below is scaled by SAMPLERATE / REFERENCE_RATE
  // so the reverb is identical at any engine rate.
  constexpr Flt REFERENCE_RATE = 29761.0f;

  // Input diffuser lengths (samples @ reference rate).
  constexpr Flt DIFF1_LEN = 142.0f;
  constexpr Flt DIFF2_LEN = 107.0f;
  constexpr Flt DIFF3_LEN = 379.0f;
  constexpr Flt DIFF4_LEN = 277.0f;

  // Tank lengths (samples @ reference rate).
  constexpr Flt APL1_LEN = 672.0f; // left modulated allpass
  constexpr Flt DELL1_LEN = 4453.0f; // left delay 1
  constexpr Flt APL2_LEN = 1800.0f; // left allpass 2
  constexpr Flt DELL2_LEN = 3720.0f; // left delay 2
  constexpr Flt APR1_LEN = 908.0f; // right modulated allpass
  constexpr Flt DELR1_LEN = 4217.0f; // right delay 1
  constexpr Flt APR2_LEN = 2656.0f; // right allpass 2
  constexpr Flt DELR2_LEN = 3163.0f; // right delay 2

  // Diffusion coefficients (dimensionless, rate-independent).
  constexpr Flt INPUT_DIFF_1 = 0.75f; // first pair of input diffusers
  constexpr Flt INPUT_DIFF_2 = 0.625f; // second pair of input diffusers
  constexpr Flt DECAY_DIFF_1 = 0.70f; // tank modulated allpass
  constexpr Flt DECAY_DIFF_2 = 0.50f; // tank second allpass

  // Modulation of the tank's first allpass: excursion (samples @ ref rate) and
  // rate (Hz). Small and slow — this is the plate shimmer, not a vibrato.
  constexpr Flt MOD_EXCURSION = 16.0f;
  constexpr Flt MOD_RATE_HZ = 0.7f;

  // Pre-delay ceiling; the line is sized to this so process() never reallocates.
  constexpr Flt MAX_PREDELAY_MS = 100.0f;

  constexpr Flt MAX_DECAY = 0.98f;
  constexpr Flt MIN_DAMPING_HZ = 20.0f;

  // Output level trim applied to the 7-tap tank sum so a fully-wet plate sits at
  // a sensible loudness relative to the dry signal.
  constexpr Flt OUTPUT_GAIN = 0.6f;

  constexpr Flt TWO_PI = 6.283185307179586f;

  // A couple of extra samples past every buffer's addressable length so the
  // fractional read of the modulated allpass and the node taps never index one
  // past the end.
  constexpr std::size_t GUARD = 4;

  // Scale a reference-rate length to the current sample rate, at least 1 sample.
  inline std::size_t scaledLen(Flt refLen) {
    Flt s = static_cast<Flt>(YSE::SAMPLERATE) / REFERENCE_RATE;
    std::size_t n = static_cast<std::size_t>(std::lround(refLen * s));
    return (n < 1) ? 1 : n;
  }
} // namespace

// ─── DelayLine ────────────────────────────────────────────────────────────────

void YSE::DSP::MODULES::plateReverb::DelayLine::init(std::size_t len) {
  buf.assign(len, 0.0f);
  pos = 0;
}

Flt YSE::DSP::MODULES::plateReverb::DelayLine::process(Flt x) {
  Flt out = buf[pos];
  buf[pos] = x;
  if (++pos >= buf.size()) pos = 0;
  return out;
}

Flt YSE::DSP::MODULES::plateReverb::DelayLine::tap(std::size_t k) const {
  // Most recent write lives at pos-1; tap(k) reads k samples before that.
  std::size_t len = buf.size();
  if (k >= len) k = len - 1;
  std::size_t idx = (pos + len - 1 - k) % len;
  return buf[idx];
}

// ─── Allpass ──────────────────────────────────────────────────────────────────

void YSE::DSP::MODULES::plateReverb::Allpass::init(std::size_t len) {
  buf.assign(len, 0.0f);
  pos = 0;
}

Flt YSE::DSP::MODULES::plateReverb::Allpass::process(Flt x, Flt g) {
  // Lattice allpass: H(z) = (z^-m - g) / (1 - g z^-m), |H| = 1.
  Flt w = buf[pos];
  Flt out = -g * x + w;
  buf[pos] = x + g * out;
  if (++pos >= buf.size()) pos = 0;
  return out;
}

Flt YSE::DSP::MODULES::plateReverb::Allpass::tap(std::size_t k) const {
  std::size_t len = buf.size();
  if (k >= len) k = len - 1;
  std::size_t idx = (pos + len - 1 - k) % len;
  return buf[idx];
}

// ─── ModAllpass ───────────────────────────────────────────────────────────────

void YSE::DSP::MODULES::plateReverb::ModAllpass::init(std::size_t len, Flt base) {
  buf.assign(len, 0.0f);
  pos = 0;
  baseDelay = base;
}

Flt YSE::DSP::MODULES::plateReverb::ModAllpass::process(Flt x, Flt g, Flt delaySamps) {
  // Fractional (linearly interpolated) read of the delay-line content at
  // delaySamps back from the current write position. The buffer is sized to
  // baseDelay + excursion + guard, so delaySamps is always in bounds and the
  // i1 neighbour never indexes past the end.
  const std::size_t len = buf.size();
  const Flt lenF = static_cast<Flt>(len);
  Flt rp = static_cast<Flt>(pos) - delaySamps;
  while (rp < 0.0f)
    rp += lenF;
  while (rp >= lenF)
    rp -= lenF;
  std::size_t i0 = static_cast<std::size_t>(rp);
  if (i0 >= len) i0 = len - 1; // guard the float boundary
  const Flt frac = rp - static_cast<Flt>(i0);
  std::size_t i1 = i0 + 1;
  if (i1 >= len) i1 = 0;
  const Flt w = buf[i0] * (1.0f - frac) + buf[i1] * frac;

  Flt out = -g * x + w;
  buf[pos] = x + g * out;
  if (++pos >= len) pos = 0;
  return out;
}

// ─── plateReverb ──────────────────────────────────────────────────────────────

YSE::DSP::MODULES::plateReverb::plateReverb()
  : parmDecay(0.5f),
    parmDamping(8000.0f),
    parmPreDelay(0.0f),
    dampL(0.0f),
    dampR(0.0f),
    fbL(0.0f),
    fbR(0.0f),
    lfoPhase(0.0f),
    modExcursion(0.0f),
    modInc(0.0f),
    builtRate(0),
    blockLength(0) {
  for (std::size_t i = 0; i < 7; ++i) {
    tapL[i] = 0;
    tapR[i] = 0;
  }
}

YSE::DSP::MODULES::plateReverb& YSE::DSP::MODULES::plateReverb::decay(Flt value) {
  parmDecay.store(std::clamp(value, 0.0f, MAX_DECAY));
  return *this;
}

Flt YSE::DSP::MODULES::plateReverb::decay() {
  return parmDecay;
}

YSE::DSP::MODULES::plateReverb& YSE::DSP::MODULES::plateReverb::damping(Flt hz) {
  Flt nyquist = 0.5f * static_cast<Flt>(SAMPLERATE);
  parmDamping.store(std::clamp(hz, MIN_DAMPING_HZ, nyquist));
  return *this;
}

Flt YSE::DSP::MODULES::plateReverb::damping() {
  return parmDamping;
}

YSE::DSP::MODULES::plateReverb& YSE::DSP::MODULES::plateReverb::preDelay(Flt ms) {
  parmPreDelay.store(std::clamp(ms, 0.0f, MAX_PREDELAY_MS));
  return *this;
}

Flt YSE::DSP::MODULES::plateReverb::preDelay() {
  return parmPreDelay;
}

void YSE::DSP::MODULES::plateReverb::build() {
  // Input diffusers.
  diff1.init(scaledLen(DIFF1_LEN) + GUARD);
  diff2.init(scaledLen(DIFF2_LEN) + GUARD);
  diff3.init(scaledLen(DIFF3_LEN) + GUARD);
  diff4.init(scaledLen(DIFF4_LEN) + GUARD);

  // Pre-delay line, sized to the maximum pre-delay.
  std::size_t preLen =
      static_cast<std::size_t>(MAX_PREDELAY_MS * 0.001f * static_cast<Flt>(SAMPLERATE)) + GUARD + 1;
  preLine.init(preLen);

  // Tank. The modulated allpasses get room for their excursion.
  modExcursion = MOD_EXCURSION * static_cast<Flt>(SAMPLERATE) / REFERENCE_RATE;
  std::size_t excSamps = static_cast<std::size_t>(std::lround(modExcursion));

  apL1.init(scaledLen(APL1_LEN) + excSamps + GUARD, static_cast<Flt>(scaledLen(APL1_LEN)));
  delL1.init(scaledLen(DELL1_LEN) + GUARD);
  apL2.init(scaledLen(APL2_LEN) + GUARD);
  delL2.init(scaledLen(DELL2_LEN) + GUARD);

  apR1.init(scaledLen(APR1_LEN) + excSamps + GUARD, static_cast<Flt>(scaledLen(APR1_LEN)));
  delR1.init(scaledLen(DELR1_LEN) + GUARD);
  apR2.init(scaledLen(APR2_LEN) + GUARD);
  delR2.init(scaledLen(DELR2_LEN) + GUARD);

  dampL = 0.0f;
  dampR = 0.0f;
  fbL = 0.0f;
  fbR = 0.0f;
  lfoPhase = 0.0f;

  modInc = TWO_PI * MOD_RATE_HZ / static_cast<Flt>(SAMPLERATE);

  // Output taps (reference-rate offsets, scaled to this rate). Each index sits
  // well inside its source line, so scaling keeps it in bounds; tap() clamps
  // defensively regardless.
  tapL[0] = scaledLen(266.0f); // delR1
  tapL[1] = scaledLen(2974.0f); // delR1
  tapL[2] = scaledLen(1913.0f); // apR2
  tapL[3] = scaledLen(1996.0f); // delR2
  tapL[4] = scaledLen(1990.0f); // delL1
  tapL[5] = scaledLen(187.0f); // apL2
  tapL[6] = scaledLen(1066.0f); // delL2

  tapR[0] = scaledLen(353.0f); // delL1
  tapR[1] = scaledLen(3627.0f); // delL1
  tapR[2] = scaledLen(1228.0f); // apL2
  tapR[3] = scaledLen(2673.0f); // delL2
  tapR[4] = scaledLen(2111.0f); // delR1
  tapR[5] = scaledLen(335.0f); // apR2
  tapR[6] = scaledLen(121.0f); // delR2

  builtRate = SAMPLERATE;
}

void YSE::DSP::MODULES::plateReverb::create() {
  // Size every buffer from the engine sample rate (off the audio thread).
  build();
}

void YSE::DSP::MODULES::plateReverb::process(MULTICHANNELBUFFER& buffer) {
  createIfNeeded();

  if (buffer.empty()) return;

  // Rebuild only if the sample rate changed since the last sizing (device
  // restart at a new rate). Steady state never enters here.
  if (builtRate != SAMPLERATE) build();

  const std::size_t length = buffer[0].getLength();
  if (length != blockLength) {
    wetL.resize(length);
    wetR.resize(length);
    blockLength = length;
  }

  const std::size_t n = buffer.size();

  // Damping one-pole coefficient from the cut-off (Hz) and sample rate.
  // c in [0,1]: 1 = open (no damping), smaller = darker.
  Flt fc = std::clamp(parmDamping.load(), MIN_DAMPING_HZ, 0.5f * static_cast<Flt>(SAMPLERATE));
  Flt dampCoef = 1.0f - std::exp(-TWO_PI * fc / static_cast<Flt>(SAMPLERATE));

  const Flt dec = std::clamp(parmDecay.load(), 0.0f, MAX_DECAY);

  // Pre-delay offset in samples (clamped inside the line).
  std::size_t preSamps =
      static_cast<std::size_t>(std::clamp(parmPreDelay.load(), 0.0f, MAX_PREDELAY_MS) * 0.001f *
                               static_cast<Flt>(SAMPLERATE));
  if (preSamps >= preLine.length()) preSamps = preLine.length() - 1;

  Flt* wl = wetL.getPtr();
  Flt* wr = wetR.getPtr();

  Flt phase = lfoPhase;

  for (std::size_t i = 0; i < length; ++i) {
    // Downmix every input channel to the mono tank input.
    Flt mono = 0.0f;
    for (std::size_t ch = 0; ch < n; ++ch)
      mono += buffer[ch].getPtr()[i];
    mono /= static_cast<Flt>(n);

    // Pre-delay: write the fresh input, then read `preSamps` samples back. With
    // preSamps == 0 the read lands on the sample just written (a true
    // passthrough); a larger offset walks back into the line's history.
    const std::size_t preLen = preLine.buf.size();
    preLine.buf[preLine.pos] = mono;
    std::size_t preRead = (preLine.pos + preLen - preSamps) % preLen;
    Flt pd = preLine.buf[preRead];
    if (++preLine.pos >= preLen) preLine.pos = 0;

    // Input diffusion (four series allpasses).
    Flt in = pd;
    in = diff1.process(in, INPUT_DIFF_1);
    in = diff2.process(in, INPUT_DIFF_1);
    in = diff3.process(in, INPUT_DIFF_2);
    in = diff4.process(in, INPUT_DIFF_2);

    // Tank modulation: the first allpass of each half reads a modulated delay.
    Flt lfo = std::sin(phase);
    Flt modL = static_cast<Flt>(apL1.baseDelay) - modExcursion * (0.5f + 0.5f * lfo);
    Flt modR = static_cast<Flt>(apR1.baseDelay) - modExcursion * (0.5f - 0.5f * lfo);
    phase += modInc;
    if (phase >= TWO_PI) phase -= TWO_PI;

    // Cross-coupled figure-eight tank. Each half uses the *other* half's
    // previous-sample output for the cross term (the long delay lines carry the
    // real loop memory, so the one-sample cross latency is inaudible and keeps
    // the update symmetric).
    Flt leftIn = in + dec * fbR;
    Flt rightIn = in + dec * fbL;

    // Left half.
    Flt a = apL1.process(leftIn, DECAY_DIFF_1, modL);
    Flt d1 = delL1.process(a);
    dampL += (d1 - dampL) * dampCoef;
    Flt node = dec * dampL;
    Flt a2 = apL2.process(node, DECAY_DIFF_2);
    Flt d2 = delL2.process(a2);
    Flt leftTankOut = dec * d2;

    // Right half.
    Flt b = apR1.process(rightIn, DECAY_DIFF_1, modR);
    Flt e1 = delR1.process(b);
    dampR += (e1 - dampR) * dampCoef;
    Flt nodeR = dec * dampR;
    Flt b2 = apR2.process(nodeR, DECAY_DIFF_2);
    Flt e2 = delR2.process(b2);
    Flt rightTankOut = dec * e2;

    fbL = leftTankOut;
    fbR = rightTankOut;

    // Seven-tap stereo output read from the tank nodes.
    Flt outL = delR1.tap(tapL[0]) + delR1.tap(tapL[1]) - apR2.tap(tapL[2]) + delR2.tap(tapL[3]) -
               delL1.tap(tapL[4]) - apL2.tap(tapL[5]) - delL2.tap(tapL[6]);
    Flt outR = delL1.tap(tapR[0]) + delL1.tap(tapR[1]) - apL2.tap(tapR[2]) + delL2.tap(tapR[3]) -
               delR1.tap(tapR[4]) - apR2.tap(tapR[5]) - delR2.tap(tapR[6]);

    wl[i] = outL * OUTPUT_GAIN;
    wr[i] = outR * OUTPUT_GAIN;
  }

  lfoPhase = phase;

  // Distribute the stereo wet signal across the output channels and apply the
  // wet/dry balance per channel.
  if (n == 1) {
    // Mono: average the two tank outputs.
    for (std::size_t i = 0; i < length; ++i)
      wl[i] = 0.5f * (wl[i] + wr[i]);
    calculateImpact(buffer[0], wetL);
  } else {
    for (std::size_t ch = 0; ch < n; ++ch) {
      DSP::buffer& wet = (ch & 1u) ? wetR : wetL;
      calculateImpact(buffer[ch], wet);
    }
  }
}
