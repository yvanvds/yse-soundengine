/*
  ==============================================================================

    pitchTracker.cpp
    Fundamental frequency, peak amplitude and note onsets of a monophonic
    signal, after the YIN estimator (issue #1130).

  ==============================================================================
*/

#include "pitchTracker.hpp"
#include "../../headers/constants.hpp"

#include <cmath>
#include <utility>

using namespace YSE::DSP;

namespace {

  constexpr Dbl Pi = 3.14159265358979323846;
  constexpr UInt MASK = pitchTracker::MAX_SIZE - 1;
  constexpr UInt NEVER = 0x7fffffffu; // samples since an onset that never was

  inline UInt Clamp(UInt value, UInt lowest, UInt highest) {
    return value < lowest ? lowest : (value > highest ? highest : value);
  }

  // The largest power of two at or below `value` (which is at least 1).
  inline UInt FloorPowerOfTwo(UInt value) {
    UInt result = 1;
    while (result <= value / 2)
      result *= 2;
    return result;
  }

  // The distance between two pitches in semitones.
  inline Flt Semitones(Flt a, Flt b) {
    return static_cast<Flt>(std::fabs(12.0 * std::log2(static_cast<Dbl>(a) / static_cast<Dbl>(b))));
  }

  // At or above 0; false when not finite, so the caller keeps its value.
  inline bool NonNegative(Flt value, Flt& out) {
    if (!std::isfinite(value)) return false;
    out = value < 0.f ? 0.f : value;
    return true;
  }

} // namespace

pitchTracker::pitchTracker()
  : frameSize(DEFAULT_SIZE),
    hop(DEFAULT_PERIOD),
    lowest(DEFAULT_MIN_FREQUENCY),
    highest(DEFAULT_MAX_FREQUENCY),
    gate(DEFAULT_THRESHOLD),
    yinThreshold(DEFAULT_TOLERANCE),
    onsetAmp(DEFAULT_ONSET_AMPLITUDE),
    onsetSemitones(DEFAULT_ONSET_PITCH),
    onsetGap(DEFAULT_ONSET_PERIOD),
    history(MAX_SIZE, 0.f),
    writePos(0),
    untilNext(DEFAULT_PERIOD),
    sinceOnset(NEVER),
    sinceNote(NEVER),
    re(MAX_SIZE, 0.0),
    im(MAX_SIZE, 0.0),
    energy(MAX_SIZE + 1, 0.0),
    difference(MAX_SIZE / 2 + 1, 0.0),
    normalised(MAX_SIZE / 2 + 1, 1.0),
    cosTable(MAX_SIZE / 2),
    sinTable(MAX_SIZE / 2),
    reportCount(0),
    pitchCount(0),
    onsetCount(0),
    pitch(0.f),
    pitchClarity(0.f),
    peakAmplitude(0.f),
    isVoiced(false),
    onsetPitchValue(0.f),
    onsetPeakValue(0.f),
    previousPeak(0.f),
    notePitch(0.f),
    candidatePitch(0.f) {
  for (UInt k = 0; k < MAX_SIZE / 2; ++k) {
    const Dbl angle = 2.0 * Pi * static_cast<Dbl>(k) / static_cast<Dbl>(MAX_SIZE);
    cosTable[k] = std::cos(angle);
    sinTable[k] = std::sin(angle);
  }
}

pitchTracker& pitchTracker::size(UInt samples) {
  frameSize = FloorPowerOfTwo(Clamp(samples, MIN_SIZE, MAX_SIZE));
  return *this;
}

UInt pitchTracker::size() const {
  return frameSize;
}

pitchTracker& pitchTracker::period(UInt samples) {
  hop = Clamp(samples, MIN_PERIOD, MAX_PERIOD);
  if (untilNext > hop) untilNext = hop;
  return *this;
}

UInt pitchTracker::period() const {
  return hop;
}

pitchTracker& pitchTracker::minFrequency(Flt hz) {
  if (std::isfinite(hz) && hz > 0.f) lowest = hz;
  return *this;
}

Flt pitchTracker::minFrequency() const {
  return lowest;
}

pitchTracker& pitchTracker::maxFrequency(Flt hz) {
  if (std::isfinite(hz) && hz > 0.f) highest = hz;
  return *this;
}

Flt pitchTracker::maxFrequency() const {
  return highest;
}

pitchTracker& pitchTracker::threshold(Flt amplitude) {
  NonNegative(amplitude, gate);
  return *this;
}

Flt pitchTracker::threshold() const {
  return gate;
}

pitchTracker& pitchTracker::tolerance(Flt value) {
  if (NonNegative(value, yinThreshold) && yinThreshold > 1.f) yinThreshold = 1.f;
  return *this;
}

Flt pitchTracker::tolerance() const {
  return yinThreshold;
}

pitchTracker& pitchTracker::onsetAmplitude(Flt amplitude) {
  NonNegative(amplitude, onsetAmp);
  return *this;
}

Flt pitchTracker::onsetAmplitude() const {
  return onsetAmp;
}

pitchTracker& pitchTracker::onsetPitch(Flt semitones) {
  NonNegative(semitones, onsetSemitones);
  return *this;
}

Flt pitchTracker::onsetPitch() const {
  return onsetSemitones;
}

pitchTracker& pitchTracker::onsetPeriod(UInt samples) {
  onsetGap = samples;
  return *this;
}

UInt pitchTracker::onsetPeriod() const {
  return onsetGap;
}

UInt pitchTracker::operator()(buffer& in) {
  return (*this)(in.getPtr(), in.getLength());
}

UInt pitchTracker::operator()(const Flt* in, UInt length) {
  reportCount = 0;
  pitchCount = 0;
  onsetCount = 0;
  UInt analyses = 0;
  UInt pos = 0;
  while (pos < length) {
    const UInt run = length - pos < untilNext ? length - pos : untilNext;
    for (UInt i = 0; i < run; ++i) {
      const Flt v = in != nullptr ? in[pos + i] : 0.f;
      history[writePos] = std::isfinite(v) ? v : 0.f;
      writePos = (writePos + 1) & MASK;
    }
    pos += run;
    untilNext -= run;
    sinceOnset = sinceOnset > NEVER - run ? NEVER : sinceOnset + run;
    sinceNote = sinceNote > NEVER - run ? NEVER : sinceNote + run;
    if (untilNext == 0) {
      analyse();
      untilNext = hop;
      analyses++;
    }
  }
  return analyses;
}

UInt pitchTracker::reports() const {
  return reportCount;
}

UInt pitchTracker::pitches() const {
  return pitchCount;
}

UInt pitchTracker::onsets() const {
  return onsetCount;
}

Flt pitchTracker::frequency() const {
  return pitch;
}

Flt pitchTracker::clarity() const {
  return pitchClarity;
}

Flt pitchTracker::amplitude() const {
  return peakAmplitude;
}

bool pitchTracker::voiced() const {
  return isVoiced;
}

Flt pitchTracker::onsetFrequency() const {
  return onsetPitchValue;
}

Flt pitchTracker::onsetPeak() const {
  return onsetPeakValue;
}

UInt pitchTracker::latency() const {
  return frameSize / 2;
}

pitchTracker& pitchTracker::reset() {
  for (Flt& v : history)
    v = 0.f;
  writePos = 0;
  untilNext = hop;
  sinceOnset = NEVER;
  sinceNote = NEVER;
  reportCount = 0;
  pitchCount = 0;
  onsetCount = 0;
  pitch = 0.f;
  pitchClarity = 0.f;
  peakAmplitude = 0.f;
  isVoiced = false;
  onsetPitchValue = 0.f;
  onsetPeakValue = 0.f;
  previousPeak = 0.f;
  notePitch = 0.f;
  candidatePitch = 0.f;
  return *this;
}

void pitchTracker::analyse() {
  const UInt n = frameSize;
  const UInt start = (writePos - n) & MASK;
  Flt peak = 0.f;
  energy[0] = 0.0;
  for (UInt j = 0; j < n; ++j) {
    const Flt x = history[(start + j) & MASK];
    const Flt magnitude = std::fabs(x);
    if (magnitude > peak) peak = magnitude;
    re[j] = static_cast<Dbl>(x);
    energy[j + 1] = energy[j] + re[j] * re[j];
  }

  const Flt before = previousPeak;
  previousPeak = peak;
  if (!(peak > 0.f) || peak < gate) {
    isVoiced = false;
    notePitch = 0.f;
    candidatePitch = 0.f;
    return;
  }
  reportCount++;
  peakAmplitude = peak;

  Dbl lag = 0.0;
  Dbl depth = 1.0;
  isVoiced = estimate(lag, depth);
  Flt found = 0.f;
  if (isVoiced) {
    found = static_cast<Flt>(static_cast<Dbl>(SAMPLERATE) / lag);
    pitch = found;
    pitchClarity = static_cast<Flt>(depth >= 1.0 ? 0.0 : (depth <= 0.0 ? 1.0 : 1.0 - depth));
    pitchCount++;
  }

  bool onset = onsetAmp > 0.f && before < onsetAmp && peak >= onsetAmp;
  bool away = false;
  if (isVoiced) {
    if (notePitch <= 0.f) {
      // The first pitch of a note is no onset of its own.
      notePitch = found;
      sinceNote = 0;
    } else if (sinceNote < frameSize) {
      // The buffer still holds sound from before the note: let its pitch
      // settle.
      notePitch = found;
    } else if (onsetSemitones > 0.f && Semitones(found, notePitch) >= onsetSemitones) {
      // A new pitch counts once two analyses in a row agree on it, so one
      // stray estimate across a change is no onset.
      away = true;
      if (candidatePitch > 0.f && Semitones(found, candidatePitch) < onsetSemitones) {
        onset = true;
      }
    }
  }
  candidatePitch = away ? found : 0.f;
  if (onset && sinceOnset >= onsetGap) {
    onsetCount++;
    sinceOnset = 0;
    sinceNote = 0;
    onsetPitchValue = found;
    onsetPeakValue = peak;
    notePitch = found;
  }
}

bool pitchTracker::estimate(Dbl& lag, Dbl& depth) {
  const UInt n = frameSize;
  const UInt w = n / 2;

  // Range of lags: from 2 (so a tone above the range is not taken for its
  // subharmonic), up to the lowest frequency's lag, leaving one lag above
  // for the parabola.
  const Dbl rate = static_cast<Dbl>(SAMPLERATE);
  Flt low = lowest;
  Flt high = highest;
  if (low > high) std::swap(low, high);
  const Dbl longest = std::floor(rate / static_cast<Dbl>(low));
  const UInt last = longest < static_cast<Dbl>(w - 1) ? static_cast<UInt>(longest) : w - 1;
  if (last < 3 || rate <= 0.0) return false;

  // Cross-correlation r(t) = sum x[j] x[j + t], j < w, t <= w: the frame in
  // the real part, its first half in the imaginary part, one FFT, split by
  // symmetry, multiplied, one inverse FFT. No circular wrap reaches t <= w.
  for (UInt j = 0; j < n; ++j)
    im[j] = j < w ? re[j] : 0.0;
  transform(n, false);
  for (UInt k = 0; k <= n / 2; ++k) {
    const UInt m = (n - k) & (n - 1);
    // X = (Z[k] + conj Z[m]) / 2, Y = (Z[k] - conj Z[m]) / 2i
    const Dbl xr = 0.5 * (re[k] + re[m]);
    const Dbl xi = 0.5 * (im[k] - im[m]);
    const Dbl yr = 0.5 * (im[k] + im[m]);
    const Dbl yi = -0.5 * (re[k] - re[m]);
    // conj(Y) X
    const Dbl cr = yr * xr + yi * xi;
    const Dbl ci = yr * xi - yi * xr;
    re[k] = cr;
    im[k] = ci;
    if (m != k) {
      re[m] = cr;
      im[m] = -ci;
    }
  }
  transform(n, true);
  const Dbl scale = 1.0 / static_cast<Dbl>(n);

  // d(t) = e(0..w) + e(t..t+w) - 2 r(t), normalised by its cumulative mean.
  const Dbl head = energy[w];
  Dbl sum = 0.0;
  normalised[0] = 1.0;
  for (UInt t = 1; t <= last + 1; ++t) {
    Dbl d = head + (energy[t + w] - energy[t]) - 2.0 * re[t] * scale;
    if (d < 0.0) d = 0.0;
    difference[t] = d;
    sum += d;
    normalised[t] = sum > 0.0 ? d * static_cast<Dbl>(t) / sum : 1.0;
  }

  // The first dip below the tolerance, followed down to its minimum.
  const Dbl tol = static_cast<Dbl>(yinThreshold);
  UInt best = 0;
  for (UInt t = 2; t <= last; ++t) {
    if (normalised[t] < tol) {
      while (t + 1 <= last && normalised[t + 1] < normalised[t])
        t++;
      best = t;
      break;
    }
  }
  if (best == 0) return false;

  // The parabola goes through the raw difference, as YIN's step 5 does:
  // the normalisation is not symmetric around the dip.
  const Dbl s0 = difference[best - 1];
  const Dbl s1 = difference[best];
  const Dbl s2 = difference[best + 1];
  const Dbl curve = s0 - 2.0 * s1 + s2;
  Dbl shift = curve > 0.0 ? 0.5 * (s0 - s2) / curve : 0.0;
  if (shift > 1.0) shift = 1.0;
  if (shift < -1.0) shift = -1.0;
  lag = static_cast<Dbl>(best) + shift;
  depth = normalised[best];

  const Dbl frequency = rate / lag;
  return frequency >= static_cast<Dbl>(low) && frequency <= static_cast<Dbl>(high);
}

void pitchTracker::transform(UInt n, bool inverse) {
  // Bit-reversed order.
  for (UInt i = 1, j = 0; i < n; ++i) {
    UInt bit = n >> 1;
    for (; (j & bit) != 0; bit >>= 1)
      j ^= bit;
    j ^= bit;
    if (i < j) {
      std::swap(re[i], re[j]);
      std::swap(im[i], im[j]);
    }
  }
  const Dbl sign = inverse ? 1.0 : -1.0;
  for (UInt length = 2; length <= n; length <<= 1) {
    const UInt half = length >> 1;
    const UInt stride = MAX_SIZE / length;
    for (UInt i = 0; i < n; i += length) {
      for (UInt k = 0; k < half; ++k) {
        const Dbl wr = cosTable[k * stride];
        const Dbl wi = sign * sinTable[k * stride];
        const UInt a = i + k;
        const UInt b = a + half;
        const Dbl tr = re[b] * wr - im[b] * wi;
        const Dbl ti = re[b] * wi + im[b] * wr;
        re[b] = re[a] - tr;
        im[b] = im[a] - ti;
        re[a] += tr;
        im[a] += ti;
      }
    }
  }
}
