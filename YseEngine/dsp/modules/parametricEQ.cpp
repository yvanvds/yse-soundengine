/*
  ==============================================================================

    parametricEQ.cpp
    Channel-strip parametric EQ module (issue #163).

  ==============================================================================
*/

#include "parametricEQ.hpp"
#include <algorithm>
#include <cmath>

namespace {
  constexpr Flt MIN_FREQ_HZ = 20.0f;
  constexpr Flt MAX_FREQ_HZ = 20000.0f;
  constexpr Flt MAX_GAIN_DB = 24.0f;
  constexpr Flt MIN_Q = 0.1f;
  constexpr Flt MAX_Q = 18.0f;
  constexpr Flt PI = 3.14159265358979323846f;

  // Default per-band voicing (a neutral, evenly spread channel-strip layout).
  constexpr Flt DEF_FREQ[4] = {100.0f, 400.0f, 2000.0f, 8000.0f};
  constexpr Flt DEF_Q[4] = {0.7071067811865475f, 1.0f, 1.0f, 0.7071067811865475f};
} // namespace

YSE::DSP::MODULES::parametricEQ::Coeffs::Coeffs()
  : b0(1.0f), b1(0.0f), b2(0.0f), a1(0.0f), a2(0.0f) {}

YSE::DSP::MODULES::parametricEQ::BandState::BandState() : x1(0.0f), x2(0.0f), y1(0.0f), y2(0.0f) {}

YSE::DSP::MODULES::parametricEQ::parametricEQ() : dirty(true), builtRate(0), blockLength(0) {
  for (int b = 0; b < EQ_BAND_COUNT; ++b) {
    parmFreq[b].store(DEF_FREQ[b]);
    parmGain[b].store(0.0f);
    parmQ[b].store(DEF_Q[b]);
  }
}

YSE::DSP::MODULES::parametricEQ& YSE::DSP::MODULES::parametricEQ::frequency(eqBand band, Flt hz) {
  parmFreq[band].store(std::clamp(hz, MIN_FREQ_HZ, MAX_FREQ_HZ));
  dirty.store(true);
  return *this;
}

Flt YSE::DSP::MODULES::parametricEQ::frequency(eqBand band) {
  return parmFreq[band];
}

YSE::DSP::MODULES::parametricEQ& YSE::DSP::MODULES::parametricEQ::gain(eqBand band, Flt db) {
  parmGain[band].store(std::clamp(db, -MAX_GAIN_DB, MAX_GAIN_DB));
  dirty.store(true);
  return *this;
}

Flt YSE::DSP::MODULES::parametricEQ::gain(eqBand band) {
  return parmGain[band];
}

YSE::DSP::MODULES::parametricEQ& YSE::DSP::MODULES::parametricEQ::q(eqBand band, Flt value) {
  parmQ[band].store(std::clamp(value, MIN_Q, MAX_Q));
  dirty.store(true);
  return *this;
}

Flt YSE::DSP::MODULES::parametricEQ::q(eqBand band) {
  return parmQ[band];
}

// RBJ "Audio EQ Cookbook" biquad design. Coefficients are normalised by a0 so
// the difference equation is y = b0*x + b1*x1 + b2*x2 - a1*y1 - a2*y2.
void YSE::DSP::MODULES::parametricEQ::computeBand(eqBand band) {
  const Flt f = std::clamp(parmFreq[band].load(), MIN_FREQ_HZ, MAX_FREQ_HZ);
  const Flt dbGain = parmGain[band].load();
  const Flt Q = std::clamp(parmQ[band].load(), MIN_Q, MAX_Q);
  const Flt sr = static_cast<Flt>(SAMPLERATE);

  const Flt A = std::pow(10.0f, dbGain / 40.0f); // sqrt of linear gain
  const Flt w0 = 2.0f * PI * f / sr;
  const Flt cosw = std::cos(w0);
  const Flt sinw = std::sin(w0);
  const Flt alpha = sinw / (2.0f * Q);

  Flt b0, b1, b2, a0, a1, a2;

  switch (band) {
  case EQ_PEAK_1:
  case EQ_PEAK_2: {
    b0 = 1.0f + alpha * A;
    b1 = -2.0f * cosw;
    b2 = 1.0f - alpha * A;
    a0 = 1.0f + alpha / A;
    a1 = -2.0f * cosw;
    a2 = 1.0f - alpha / A;
    break;
  }
  case EQ_LOW_SHELF: {
    const Flt twoSqrtAalpha = 2.0f * std::sqrt(A) * alpha;
    b0 = A * ((A + 1.0f) - (A - 1.0f) * cosw + twoSqrtAalpha);
    b1 = 2.0f * A * ((A - 1.0f) - (A + 1.0f) * cosw);
    b2 = A * ((A + 1.0f) - (A - 1.0f) * cosw - twoSqrtAalpha);
    a0 = (A + 1.0f) + (A - 1.0f) * cosw + twoSqrtAalpha;
    a1 = -2.0f * ((A - 1.0f) + (A + 1.0f) * cosw);
    a2 = (A + 1.0f) + (A - 1.0f) * cosw - twoSqrtAalpha;
    break;
  }
  case EQ_HIGH_SHELF:
  default: {
    const Flt twoSqrtAalpha = 2.0f * std::sqrt(A) * alpha;
    b0 = A * ((A + 1.0f) + (A - 1.0f) * cosw + twoSqrtAalpha);
    b1 = -2.0f * A * ((A - 1.0f) + (A + 1.0f) * cosw);
    b2 = A * ((A + 1.0f) + (A - 1.0f) * cosw - twoSqrtAalpha);
    a0 = (A + 1.0f) - (A - 1.0f) * cosw + twoSqrtAalpha;
    a1 = 2.0f * ((A - 1.0f) - (A + 1.0f) * cosw);
    a2 = (A + 1.0f) - (A - 1.0f) * cosw - twoSqrtAalpha;
    break;
  }
  }

  Coeffs& c = coeffs[band];
  const Flt inv = 1.0f / a0;
  c.b0 = b0 * inv;
  c.b1 = b1 * inv;
  c.b2 = b2 * inv;
  c.a1 = a1 * inv;
  c.a2 = a2 * inv;
}

void YSE::DSP::MODULES::parametricEQ::create() {
  for (int b = 0; b < EQ_BAND_COUNT; ++b)
    computeBand(static_cast<eqBand>(b));
  builtRate = SAMPLERATE;
  dirty.store(false);
}

void YSE::DSP::MODULES::parametricEQ::process(MULTICHANNELBUFFER& buffer) {
  createIfNeeded();

  if (buffer.empty()) return;

  // Recompute the shared coefficient set when a parameter moved or the sample
  // rate changed. This is the only place the transcendental design runs, it is
  // bounded (four bands), allocates nothing, and only fires on a block where
  // something actually changed.
  const UInt rate = SAMPLERATE;
  bool rateChanged = (rate != builtRate);
  if (dirty.exchange(false) || rateChanged) {
    for (int b = 0; b < EQ_BAND_COUNT; ++b)
      computeBand(static_cast<eqBand>(b));
    builtRate = rate;
  }

  // Grow/shrink per-channel biquad memory to the channel count. New channels
  // start with cleared delay memory; this is the only allocation path.
  bool resized = channels.ensure(buffer.size());
  if (resized || rateChanged) {
    // A device restart (new rate/channel count) clears filter memory so no
    // stale history leaks across the discontinuity.
    for (std::size_t ch = 0; ch < channels.size(); ++ch)
      channels[ch] = ChannelState();
  }

  const std::size_t length = buffer[0].getLength();
  if (length != blockLength) {
    wet.resize(static_cast<UInt>(length));
    blockLength = length;
  }

  // Snapshot the coefficient set into locals once per block (avoids re-reading
  // struct members in the inner loop).
  const Coeffs c0 = coeffs[EQ_LOW_SHELF];
  const Coeffs c1 = coeffs[EQ_PEAK_1];
  const Coeffs c2 = coeffs[EQ_PEAK_2];
  const Coeffs c3 = coeffs[EQ_HIGH_SHELF];
  const Coeffs* cs[EQ_BAND_COUNT] = {&c0, &c1, &c2, &c3};

  const std::size_t n = buffer.size();
  for (std::size_t ch = 0; ch < n; ++ch) {
    ChannelState& st = channels[ch];
    Flt* x = buffer[ch].getPtr();
    Flt* w = wet.getPtr();

    for (std::size_t i = 0; i < length; ++i) {
      Flt sample = x[i];
      // Cascade the four bands in series.
      for (int b = 0; b < EQ_BAND_COUNT; ++b) {
        const Coeffs& c = *cs[b];
        BandState& bs = st.bands[b];
        const Flt in = sample;
        const Flt out = c.b0 * in + c.b1 * bs.x1 + c.b2 * bs.x2 - c.a1 * bs.y1 - c.a2 * bs.y2;
        bs.x2 = bs.x1;
        bs.x1 = in;
        bs.y2 = bs.y1;
        bs.y1 = out;
        sample = out;
      }
      w[i] = sample;
    }

    // impact() sets the dry/wet balance.
    calculateImpact(buffer[ch], wet);
  }
}
