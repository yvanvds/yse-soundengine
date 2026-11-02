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

  // The 4x true-peak interpolator of ITU-R BS.1770-4 / -5 Annex 2: phase p
  // tap k weighs the input k samples back.
  constexpr Flt TruePeakTaps[4][12] = {
      {0.0017089843750f, 0.0109863281250f, -0.0196533203125f, 0.0332031250000f, -0.0594482421875f,
       0.1373291015625f, 0.9721679687500f, -0.1022949218750f, 0.0476074218750f, -0.0266113281250f,
       0.0148925781250f, -0.0083007812500f},
      {-0.0291748046875f, 0.0292968750000f, -0.0517578125000f, 0.0891113281250f, -0.1665039062500f,
       0.4650878906250f, 0.7797851562500f, -0.2003173828125f, 0.1015625000000f, -0.0582275390625f,
       0.0330810546875f, -0.0189208984375f},
      {-0.0189208984375f, 0.0330810546875f, -0.0582275390625f, 0.1015625000000f, -0.2003173828125f,
       0.7797851562500f, 0.4650878906250f, -0.1665039062500f, 0.0891113281250f, -0.0517578125000f,
       0.0292968750000f, -0.0291748046875f},
      {-0.0083007812500f, 0.0148925781250f, -0.0266113281250f, 0.0476074218750f, -0.1022949218750f,
       0.9721679687500f, 0.1373291015625f, -0.0594482421875f, 0.0332031250000f, -0.0196533203125f,
       0.0109863281250f, 0.0017089843750f},
  };

  inline Flt Decibels(Flt magnitude) {
    if (!(magnitude > 0.f)) return YSE::DSP::loudnessMeter::SILENCE;
    const Flt db = 20.f * std::log10(magnitude);
    return db < YSE::DSP::loudnessMeter::SILENCE ? YSE::DSP::loudnessMeter::SILENCE : db;
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
        subIndex(0),
        subEnergy(0.0),
        weights{},
        shelfState{},
        highPassState{},
        peaks{},
        truePeaks{},
        history{},
        historyPos{},
        steps{},
        stepPos(0),
        stepsSeen(0),
        subs{},
        subPos(0),
        momentaryLoudness(SILENCE),
        shortTermLoudness(SILENCE),
        maxMomentaryLoudness(SILENCE),
        maxShortTermLoudness(SILENCE),
        integratedDirty(false),
        integratedLoudness(SILENCE),
        rangeDirty(false),
        range(0.f),
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
      shelfState.fill(state{});
      highPassState.fill(state{});
      for (auto& past : history)
        past.fill(0.f);
      historyPos.fill(0);
      reset();
    }

    loudnessMeter::histogram::histogram()
      : count(static_cast<std::size_t>(BINS), 0),
        energy(static_cast<std::size_t>(BINS), 0.0),
        total(0),
        totalEnergy(0.0) {}

    int loudnessMeter::histogram::bin(Dbl loudness) {
      const Dbl at = (loudness - static_cast<Dbl>(ABSOLUTE_GATE)) * 100.0;
      if (!(at > 0.0)) return 0;
      if (at >= static_cast<Dbl>(BINS)) return BINS - 1;
      return static_cast<int>(at);
    }

    void loudnessMeter::histogram::add(Dbl loudness, Dbl e) {
      const auto b = static_cast<std::size_t>(bin(loudness));
      count[b]++;
      energy[b] += e;
      total++;
      totalEnergy += e;
    }

    void loudnessMeter::histogram::clear() {
      std::fill(count.begin(), count.end(), 0);
      std::fill(energy.begin(), energy.end(), 0.0);
      total = 0;
      totalEnergy = 0.0;
    }

    Dbl loudnessMeter::histogram::mean(int b) const {
      const auto i = static_cast<std::size_t>(b);
      return -0.691 + 10.0 * std::log10(energy[i] / static_cast<Dbl>(count[i]));
    }

    int loudnessMeter::histogram::gated(Dbl gate, bool strict, std::uint64_t& n, Dbl& e) const {
      n = 0;
      e = 0.0;
      int first = bin(gate);
      // The bin holding the gate counts as a whole, by its mean loudness.
      const auto f = static_cast<std::size_t>(first);
      if (count[f] == 0 || (strict ? !(mean(first) > gate) : !(mean(first) >= gate))) first++;
      for (int b = first; b < BINS; ++b) {
        const auto i = static_cast<std::size_t>(b);
        n += count[i];
        e += energy[i];
      }
      return first;
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
      Flt trueTop = truePeaks[channel];
      std::array<Flt, 2 * TAPS>& past = history[channel];
      UInt pos = historyPos[channel];
      Dbl sum = 0.0;
      for (UInt i = from; i < to; ++i) {
        Flt v = in[i];
        if (!std::isfinite(v)) v = 0.f;
        const Flt magnitude = std::fabs(v);
        if (magnitude > top) top = magnitude;
        if (magnitude > trueTop) trueTop = magnitude;
        // The true peak: the newest sample at past[pos], older ones after.
        pos = pos == 0 ? TAPS - 1 : pos - 1;
        past[pos] = v;
        past[pos + TAPS] = v;
        const Flt* recent = past.data() + pos;
        for (const auto& phase : TruePeakTaps) {
          Flt y = 0.f;
          for (UInt k = 0; k < TAPS; ++k)
            y += phase[k] * recent[k];
          const Flt m = std::fabs(y);
          if (m > trueTop) trueTop = m;
        }
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
      truePeaks[channel] = trueTop;
      historyPos[channel] = pos;
      return sum;
    }

    // The running sub-step ends this many samples into its step: the
    // sub-steps tile the step, so every MOMENTARY_SUBS of them in a row
    // hold exactly MOMENTARY_STEPS steps.
    UInt loudnessMeter::subEnd() const {
      const std::uint64_t at = static_cast<std::uint64_t>(stepLength) * (subIndex + 1);
      return static_cast<UInt>((at + SUB_STEPS / 2) / SUB_STEPS);
    }

    // One 5 ms sub-step is complete: slide the momentary window and, after
    // the last sub-step of a step, end the step.
    void loudnessMeter::endSub() {
      subs[subPos] = subEnergy;
      subPos = (subPos + 1) % MOMENTARY_SUBS;
      stepEnergy += subEnergy;
      subEnergy = 0.0;
      Dbl recent = 0.0;
      for (const Dbl e : subs)
        recent += e;
      momentaryLoudness = Loudness(recent / (MOMENTARY_STEPS * static_cast<Dbl>(stepLength)));
      if (momentaryLoudness > maxMomentaryLoudness) maxMomentaryLoudness = momentaryLoudness;
      if (++subIndex >= SUB_STEPS) endStep();
    }

    // One 100 ms step is complete: update the short-term window and, once
    // 400 ms have been measured, gate the block that ends here.
    void loudnessMeter::endStep() {
      steps[stepPos] = stepEnergy;
      stepPos = (stepPos + 1) % WINDOW_STEPS;
      if (stepsSeen < WINDOW_STEPS) stepsSeen++;
      stepEnergy = 0.0;
      stepFill = 0;
      subIndex = 0;

      Dbl recent = 0.0;
      Dbl all = 0.0;
      for (UInt k = 0; k < WINDOW_STEPS; ++k) {
        const Dbl e = steps[(stepPos + WINDOW_STEPS - 1 - k) % WINDOW_STEPS];
        if (k < MOMENTARY_STEPS) recent += e;
        all += e;
      }
      const Dbl length = static_cast<Dbl>(stepLength);
      const Dbl block = recent / (MOMENTARY_STEPS * length);
      const Dbl window = all / (WINDOW_STEPS * length);
      shortTermLoudness = Loudness(window);
      if (shortTermLoudness > maxShortTermLoudness) maxShortTermLoudness = shortTermLoudness;

      // Loudness range: every short-term value once 3 s have been
      // measured, at or above the absolute gate (Tech 3342).
      if (stepsSeen >= WINDOW_STEPS && window > 0.0) {
        const Dbl l = -0.691 + 10.0 * std::log10(window);
        if (l >= static_cast<Dbl>(ABSOLUTE_GATE)) {
          shortTerms.add(l, window);
          rangeDirty = true;
        }
      }

      // Integrated: every 400 ms block above the absolute gate (BS.1770).
      if (stepsSeen < MOMENTARY_STEPS || !(block > 0.0)) return;
      const Dbl l = -0.691 + 10.0 * std::log10(block);
      if (!(l > static_cast<Dbl>(ABSOLUTE_GATE))) return;
      blocks.add(l, block);
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
        // Sub-steps of no samples, at very low rates, end at once.
        while (stepFill >= subEnd())
          endSub();
        UInt end = at + (subEnd() - stepFill);
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
        subEnergy += energy;
        stepFill += end - at;
        at = end;
        if (stepFill >= subEnd()) endSub();
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

    Flt loudnessMeter::maxMomentary() const {
      return maxMomentaryLoudness;
    }

    Flt loudnessMeter::maxShortTerm() const {
      return maxShortTermLoudness;
    }

    Flt loudnessMeter::integrated() {
      if (!integratedDirty) return integratedLoudness;
      integratedDirty = false;
      if (blocks.total == 0) {
        integratedLoudness = SILENCE;
        return integratedLoudness;
      }
      // The relative gate, from the mean of every block past the absolute
      // gate.
      const Dbl absolute = blocks.totalEnergy / static_cast<Dbl>(blocks.total);
      const Dbl gate = -0.691 + 10.0 * std::log10(absolute) + static_cast<Dbl>(RELATIVE_GATE);
      std::uint64_t n = 0;
      Dbl e = 0.0;
      blocks.gated(gate, true, n, e);
      integratedLoudness = n == 0 ? SILENCE : Loudness(e / static_cast<Dbl>(n));
      return integratedLoudness;
    }

    Flt loudnessMeter::loudnessRange() {
      if (!rangeDirty) return range;
      rangeDirty = false;
      range = 0.f;
      if (shortTerms.total == 0) return range;
      // Tech 3342: 20 LU below the mean of the absolutely gated values.
      const Dbl absolute = shortTerms.totalEnergy / static_cast<Dbl>(shortTerms.total);
      const Dbl gate = -0.691 + 10.0 * std::log10(absolute) - 20.0;
      std::uint64_t n = 0;
      Dbl e = 0.0;
      const int first = shortTerms.gated(gate, false, n, e);
      if (n == 0) return range;
      // The values at ranks round((n - 1) p) in sorted order.
      const Dbl last = static_cast<Dbl>(n - 1);
      const auto lowRank = static_cast<std::uint64_t>(std::llround(last * 0.10));
      const auto highRank = static_cast<std::uint64_t>(std::llround(last * 0.95));
      Dbl low = 0.0;
      Dbl high = 0.0;
      bool haveLow = false;
      std::uint64_t seen = 0;
      for (int b = first; b < BINS; ++b) {
        const std::uint64_t c = shortTerms.count[static_cast<std::size_t>(b)];
        if (c == 0) continue;
        seen += c;
        if (!haveLow && seen > lowRank) {
          low = shortTerms.mean(b);
          haveLow = true;
        }
        if (seen > highRank) {
          high = shortTerms.mean(b);
          break;
        }
      }
      range = static_cast<Flt>(high - low);
      return range;
    }

    Flt loudnessMeter::peak(UInt channel) const {
      Flt top = 0.f;
      if (channel >= 1 && channel <= channelCount) {
        top = peaks[channel - 1];
      } else {
        for (UInt c = 0; c < channelCount; ++c)
          if (peaks[c] > top) top = peaks[c];
      }
      return Decibels(top);
    }

    Flt loudnessMeter::truePeak(UInt channel) const {
      Flt top = 0.f;
      if (channel >= 1 && channel <= channelCount) {
        top = truePeaks[channel - 1];
      } else {
        for (UInt c = 0; c < channelCount; ++c)
          if (truePeaks[c] > top) top = truePeaks[c];
      }
      return Decibels(top);
    }

    std::uint64_t loudnessMeter::gatedBlocks() const {
      return blocks.total;
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
      subIndex = 0;
      subEnergy = 0.0;
      subs.fill(0.0);
      subPos = 0;
      // The filters and the interpolator keep their memory: the signal goes
      // on, and clearing it would read a step that is not there.
      peaks.fill(0.f);
      truePeaks.fill(0.f);
      steps.fill(0.0);
      stepPos = 0;
      stepsSeen = 0;
      momentaryLoudness = SILENCE;
      shortTermLoudness = SILENCE;
      maxMomentaryLoudness = SILENCE;
      maxShortTermLoudness = SILENCE;
      blocks.clear();
      shortTerms.clear();
      integratedDirty = false;
      integratedLoudness = SILENCE;
      rangeDirty = false;
      range = 0.f;
      elapsed = 0.0;
      return *this;
    }

  } // namespace DSP
} // namespace YSE
