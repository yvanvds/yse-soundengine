#include "loudnessMeter.hpp"

#include <algorithm>
#include <cmath>

namespace {
  inline UInt Clamp(UInt length) {
    return length > YSE::STANDARD_BUFFERSIZE ? YSE::STANDARD_BUFFERSIZE : length;
  }

  // BS.1770 loudness of a mean (weighted) energy; SILENCE for none.
  inline Flt Loudness(Dbl energy) {
    if (!(energy > 0.0)) return YSE::DSP::loudnessMeter::SILENCE;
    const Dbl l = -0.691 + 10.0 * std::log10(energy);
    return l < static_cast<Dbl>(YSE::DSP::loudnessMeter::SILENCE) ? YSE::DSP::loudnessMeter::SILENCE
                                                                  : static_cast<Flt>(l);
  }

  // The analog prototype of the K-weighting stages, fitted to BS.1770's
  // 48 kHz coefficients (after libebur128's derivation): a shelf of G dB
  // at f0 with quality Q, and a second-order high-pass.
  constexpr Dbl ShelfFrequency = 1681.974450955533;
  constexpr Dbl ShelfGain = 3.999843853973347;
  constexpr Dbl ShelfQ = 0.7071752369554196;
  constexpr Dbl ShelfBandExponent = 0.4996667741545416;
  constexpr Dbl HighPassFrequency = 38.13547087602444;
  constexpr Dbl HighPassQ = 0.5003270373238773;
  constexpr Dbl Pi = 3.14159265358979323846;

  // Below this a filter state is flushed to 0, so silence never runs into
  // denormals.
  constexpr Dbl Tiny = 1e-30;
  inline Dbl Flush(Dbl v) {
    return std::fabs(v) < Tiny ? 0.0 : v;
  }

  // What a missing or short channel reads past its end.
  const Flt Zeros[YSE::STANDARD_BUFFERSIZE] = {};
} // namespace

namespace YSE {
  namespace DSP {

    loudnessMeter::section loudnessMeter::Shelf(Dbl sampleRate) {
      section s;
      if (!(sampleRate > 0.0)) return s;
      const Dbl k = std::tan(Pi * ShelfFrequency / sampleRate);
      const Dbl vh = std::pow(10.0, ShelfGain / 20.0);
      const Dbl vb = std::pow(vh, ShelfBandExponent);
      const Dbl a0 = 1.0 + k / ShelfQ + k * k;
      s.b0 = (vh + vb * k / ShelfQ + k * k) / a0;
      s.b1 = 2.0 * (k * k - vh) / a0;
      s.b2 = (vh - vb * k / ShelfQ + k * k) / a0;
      s.a1 = 2.0 * (k * k - 1.0) / a0;
      s.a2 = (1.0 - k / ShelfQ + k * k) / a0;
      return s;
    }

    loudnessMeter::section loudnessMeter::HighPass(Dbl sampleRate) {
      section s;
      if (!(sampleRate > 0.0)) return s;
      const Dbl k = std::tan(Pi * HighPassFrequency / sampleRate);
      const Dbl a0 = 1.0 + k / HighPassQ + k * k;
      // The numerator is BS.1770's 1, -2, 1, not normalised.
      s.b0 = 1.0;
      s.b1 = -2.0;
      s.b2 = 1.0;
      s.a1 = 2.0 * (k * k - 1.0) / a0;
      s.a2 = (1.0 - k / HighPassQ + k * k) / a0;
      return s;
    }

    loudnessMeter::loudnessMeter(UInt channels)
      : channelCount(channels < 1 ? 1 : (channels > MAX_CHANNELS ? MAX_CHANNELS : channels)),
        rate(0),
        stepLength(1),
        stepFill(0),
        stepEnergy(0.0),
        weights{},
        shelfState{},
        highPassState{},
        peaks{},
        steps{},
        stepPos(0),
        stepsSeen(0),
        momentaryLoudness(SILENCE),
        shortTermLoudness(SILENCE),
        binCount(static_cast<std::size_t>(BINS), 0),
        binEnergy(static_cast<std::size_t>(BINS), 0.0),
        gatedCount(0),
        gatedEnergy(0.0),
        integratedDirty(false),
        integratedLoudness(SILENCE),
        intervalMs(0.f),
        elapsed(0.0) {
      weights.fill(1.0);
      configure();
    }

    // Follows the live sample rate: new coefficients and step length, and a
    // fresh start, since the old measurements are of another filter.
    void loudnessMeter::configure() {
      rate = SAMPLERATE;
      const Dbl sr = static_cast<Dbl>(rate);
      shelf = Shelf(sr);
      highPass = HighPass(sr);
      const Dbl step = std::round(sr / 10.0);
      stepLength = step < 1.0 ? 1u : static_cast<UInt>(step);
      reset();
    }

    UInt loudnessMeter::channels() const {
      return channelCount;
    }

    loudnessMeter& loudnessMeter::weight(UInt channel, Flt value) {
      if (channel < MAX_CHANNELS && std::isfinite(value) && value >= 0.f)
        weights[channel] = static_cast<Dbl>(value);
      return *this;
    }

    Flt loudnessMeter::weight(UInt channel) const {
      return channel < MAX_CHANNELS ? static_cast<Flt>(weights[channel]) : 0.f;
    }

    // K-weights samples from .. to of one channel and returns the sum of
    // their squares; tracks the peak.
    Dbl loudnessMeter::filter(UInt channel, const Flt* in, UInt from, UInt to) {
      state& s1 = shelfState[channel];
      state& s2 = highPassState[channel];
      const section a = shelf;
      const section b = highPass;
      Flt top = peaks[channel];
      Dbl sum = 0.0;
      for (UInt i = from; i < to; ++i) {
        Flt v = in[i];
        if (!std::isfinite(v)) v = 0.f;
        const Flt magnitude = std::fabs(v);
        if (magnitude > top) top = magnitude;
        const Dbl x = static_cast<Dbl>(v);
        const Dbl y = a.b0 * x + a.b1 * s1.x1 + a.b2 * s1.x2 - a.a1 * s1.y1 - a.a2 * s1.y2;
        s1.x2 = s1.x1;
        s1.x1 = x;
        s1.y2 = s1.y1;
        s1.y1 = y;
        const Dbl z = b.b0 * y + b.b1 * s2.x1 + b.b2 * s2.x2 - b.a1 * s2.y1 - b.a2 * s2.y2;
        s2.x2 = s2.x1;
        s2.x1 = y;
        s2.y2 = s2.y1;
        s2.y1 = z;
        sum += z * z;
      }
      s1.y1 = Flush(s1.y1);
      s1.y2 = Flush(s1.y2);
      s2.y1 = Flush(s2.y1);
      s2.y2 = Flush(s2.y2);
      peaks[channel] = top;
      return sum;
    }

    // One 100 ms step is complete: update the windows and, once 400 ms
    // have been measured, gate the block that ends here.
    void loudnessMeter::endStep() {
      steps[stepPos] = stepEnergy;
      stepPos = (stepPos + 1) % WINDOW_STEPS;
      if (stepsSeen < WINDOW_STEPS) stepsSeen++;
      stepEnergy = 0.0;
      stepFill = 0;

      Dbl recent = 0.0;
      Dbl all = 0.0;
      for (UInt k = 0; k < WINDOW_STEPS; ++k) {
        const Dbl e = steps[(stepPos + WINDOW_STEPS - 1 - k) % WINDOW_STEPS];
        if (k < MOMENTARY_STEPS) recent += e;
        all += e;
      }
      const Dbl length = static_cast<Dbl>(stepLength);
      const Dbl block = recent / (MOMENTARY_STEPS * length);
      momentaryLoudness = Loudness(block);
      shortTermLoudness = Loudness(all / (WINDOW_STEPS * length));

      if (stepsSeen < MOMENTARY_STEPS || !(block > 0.0)) return;
      const Dbl l = -0.691 + 10.0 * std::log10(block);
      if (!(l > static_cast<Dbl>(ABSOLUTE_GATE))) return;
      int bin = static_cast<int>((l - static_cast<Dbl>(ABSOLUTE_GATE)) * 100.0);
      if (bin >= BINS) bin = BINS - 1;
      if (bin < 0) bin = 0;
      binCount[static_cast<std::size_t>(bin)]++;
      binEnergy[static_cast<std::size_t>(bin)] += block;
      gatedCount++;
      gatedEnergy += block;
      integratedDirty = true;
    }

    UInt loudnessMeter::operator()(buffer& in) {
      buffer* one = &in;
      return (*this)(&one, 1);
    }

    UInt loudnessMeter::operator()(buffer* const* in, UInt count) {
      if (rate != SAMPLERATE) configure();
      UInt length = 0;
      if (count > 0 && in != nullptr && in[0] != nullptr) length = Clamp(in[0]->getLength());
      const UInt used = count < channelCount ? count : channelCount;

      UInt at = 0;
      while (at < length) {
        UInt end = at + (stepLength - stepFill);
        if (end > length) end = length;
        Dbl energy = 0.0;
        for (UInt c = 0; c < channelCount; ++c) {
          buffer* b = c < used ? in[c] : nullptr;
          const UInt have = b != nullptr ? Clamp(b->getLength()) : 0;
          const Flt* samples = b != nullptr ? b->getPtr() : Zeros;
          if (have >= end) {
            energy += weights[c] * filter(c, samples, at, end);
          } else {
            // Shorter or missing: silence past its end.
            Dbl sum = have > at ? filter(c, samples, at, have) : 0.0;
            const UInt from = have > at ? have : at;
            sum += filter(c, Zeros, from, end);
            energy += weights[c] * sum;
          }
        }
        stepEnergy += energy;
        stepFill += end - at;
        at = end;
        if (stepFill >= stepLength) endStep();
      }

      if (!(intervalMs > 0.f) || rate == 0) return 0;
      Dbl period = static_cast<Dbl>(intervalMs) * static_cast<Dbl>(rate) / 1000.0;
      if (period < 1.0) period = 1.0;
      elapsed += static_cast<Dbl>(length);
      if (elapsed < period) return 0;
      const Dbl reports = std::floor(elapsed / period);
      elapsed -= reports * period;
      return static_cast<UInt>(reports);
    }

    Flt loudnessMeter::momentary() const {
      return momentaryLoudness;
    }

    Flt loudnessMeter::shortTerm() const {
      return shortTermLoudness;
    }

    Flt loudnessMeter::integrated() {
      if (!integratedDirty) return integratedLoudness;
      integratedDirty = false;
      if (gatedCount == 0) {
        integratedLoudness = SILENCE;
        return integratedLoudness;
      }
      // The relative gate, from the mean of every block past the absolute
      // gate.
      const Dbl absolute = gatedEnergy / static_cast<Dbl>(gatedCount);
      const Dbl gate = -0.691 + 10.0 * std::log10(absolute) + static_cast<Dbl>(RELATIVE_GATE);
      const Dbl edge = (gate - static_cast<Dbl>(ABSOLUTE_GATE)) * 100.0;
      int first = edge < 0.0 ? 0 : static_cast<int>(edge);
      std::uint64_t n = 0;
      Dbl e = 0.0;
      if (first < BINS) {
        // The bin holding the gate counts by its mean loudness.
        const auto f = static_cast<std::size_t>(first);
        if (binCount[f] > 0) {
          const Dbl mean = binEnergy[f] / static_cast<Dbl>(binCount[f]);
          if (-0.691 + 10.0 * std::log10(mean) > gate) {
            n += binCount[f];
            e += binEnergy[f];
          }
        }
        for (int b = first + 1; b < BINS; ++b) {
          const auto i = static_cast<std::size_t>(b);
          n += binCount[i];
          e += binEnergy[i];
        }
      }
      integratedLoudness = n == 0 ? SILENCE : Loudness(e / static_cast<Dbl>(n));
      return integratedLoudness;
    }

    Flt loudnessMeter::peak(UInt channel) const {
      Flt top = 0.f;
      if (channel >= 1 && channel <= channelCount) {
        top = peaks[channel - 1];
      } else {
        for (UInt c = 0; c < channelCount; ++c)
          if (peaks[c] > top) top = peaks[c];
      }
      if (!(top > 0.f)) return SILENCE;
      const Flt db = 20.f * std::log10(top);
      return db < SILENCE ? SILENCE : db;
    }

    std::uint64_t loudnessMeter::gatedBlocks() const {
      return gatedCount;
    }

    loudnessMeter& loudnessMeter::interval(Flt ms) {
      const Flt clean = (std::isfinite(ms) && ms > 0.f) ? ms : 0.f;
      if (clean != intervalMs) {
        intervalMs = clean;
        elapsed = 0.0;
      }
      return *this;
    }

    Flt loudnessMeter::interval() const {
      return intervalMs;
    }

    loudnessMeter& loudnessMeter::reset() {
      stepFill = 0;
      stepEnergy = 0.0;
      shelfState.fill(state{});
      highPassState.fill(state{});
      peaks.fill(0.f);
      steps.fill(0.0);
      stepPos = 0;
      stepsSeen = 0;
      momentaryLoudness = SILENCE;
      shortTermLoudness = SILENCE;
      std::fill(binCount.begin(), binCount.end(), 0);
      std::fill(binEnergy.begin(), binEnergy.end(), 0.0);
      gatedCount = 0;
      gatedEnergy = 0.0;
      integratedDirty = false;
      integratedLoudness = SILENCE;
      elapsed = 0.0;
      return *this;
    }

  } // namespace DSP
} // namespace YSE
