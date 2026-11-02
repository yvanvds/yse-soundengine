/*
  ==============================================================================

    compressor.cpp
    Channel-strip dynamics compressor module (issue #163).

  ==============================================================================
*/

#include "compressor.hpp"
#include "../filters/filterCoefficients.hpp"
#include "../smoother.hpp"
#include <algorithm>
#include <cmath>

namespace {
  constexpr Flt MIN_THRESHOLD_DB = -60.0f;
  constexpr Flt MAX_THRESHOLD_DB = 0.0f;
  constexpr Flt MIN_RATIO = 1.0f;
  constexpr Flt MAX_RATIO = 20.0f;
  constexpr Flt MIN_TIME_MS = 0.1f;
  constexpr Flt MAX_TIME_MS = 2000.0f;
  constexpr Flt MAX_MAKEUP_DB = 24.0f;

  // Butterworth Q for the sidechain high-pass.
  constexpr Flt BUTTERWORTH_Q = 0.70710678f;
  // Below this the high-pass state is flushed to 0, against denormals.
  constexpr Dbl STATE_FLOOR = 1.0e-30;

  // Fixed RMS-detector averaging window (mean-square one-pole time constant).
  constexpr Flt RMS_WINDOW_SEC = 0.01f;

  // Floor added before the log so a silent detector maps to a finite, very low
  // dB value instead of -inf.
  constexpr Flt LEVEL_FLOOR = 1.0e-9f;

  inline Flt dbToLin(Flt db) {
    return std::pow(10.0f, db / 20.0f);
  }
  inline Flt linToDb(Flt lin) {
    return 20.0f * std::log10(lin + LEVEL_FLOOR);
  }
} // namespace

// The settings of one piece, read once from the atomics.
struct YSE::DSP::MODULES::compressor::pieceSettings {
  Flt thresholdDb;
  Flt slope; // <= 0 : dB out per dB over threshold
  Flt attackCoef;
  Flt releaseCoef;
  Flt rangeDb;
  bool limitRange;
  bool freeze;
  bool progressive;
  bool smoothing;
  Flt smoothCoef;
  bool gate;
  Flt gateThresholdLin;
};

YSE::DSP::MODULES::compressor::compressor(UInt maxLookahead, UInt maxChannels)
  : parmThreshold(-18.0f),
    parmRatio(4.0f),
    parmAttack(10.0f),
    parmRelease(100.0f),
    parmMakeup(0.0f),
    parmDetector(DETECT_PEAK),
    parmRange(MAX_RANGE),
    parmFreeze(false),
    parmProgressive(false),
    parmSmoothing(0.0f),
    parmGate(false),
    parmGateThreshold(DEFAULT_GATE_THRESHOLD),
    parmLookahead(0),
    parmSidechainFilter(false),
    parmSidechainFrequency(DEFAULT_SIDECHAIN_FREQUENCY),
    parmLinked(true),
    capacity(std::min<UInt>(maxLookahead, MAX_LOOKAHEAD) + 1),
    channelCapacity(std::clamp<UInt>(maxChannels, 1, MAX_CHANNELS)),
    rmsCoef(0.0f),
    channelStates(channelCapacity),
    highPasses(channelCapacity),
    lines(static_cast<std::size_t>(channelCapacity) * capacity, 0.0f),
    reductionDb(0.0f),
    gainBuf(STANDARD_BUFFERSIZE),
    wet(STANDARD_BUFFERSIZE),
    dry(STANDARD_BUFFERSIZE) {}

YSE::DSP::MODULES::compressor& YSE::DSP::MODULES::compressor::detector(compressorDetector value) {
  parmDetector.store(static_cast<Int>(value));
  return *this;
}

YSE::DSP::MODULES::compressorDetector YSE::DSP::MODULES::compressor::detector() {
  return static_cast<compressorDetector>(parmDetector.load());
}

YSE::DSP::MODULES::compressor& YSE::DSP::MODULES::compressor::threshold(Flt db) {
  parmThreshold.store(std::clamp(db, MIN_THRESHOLD_DB, MAX_THRESHOLD_DB));
  return *this;
}

Flt YSE::DSP::MODULES::compressor::threshold() {
  return parmThreshold;
}

YSE::DSP::MODULES::compressor& YSE::DSP::MODULES::compressor::ratio(Flt value) {
  parmRatio.store(std::clamp(value, MIN_RATIO, MAX_RATIO));
  return *this;
}

Flt YSE::DSP::MODULES::compressor::ratio() {
  return parmRatio;
}

YSE::DSP::MODULES::compressor& YSE::DSP::MODULES::compressor::attack(Flt ms) {
  parmAttack.store(std::clamp(ms, MIN_TIME_MS, MAX_TIME_MS));
  return *this;
}

Flt YSE::DSP::MODULES::compressor::attack() {
  return parmAttack;
}

YSE::DSP::MODULES::compressor& YSE::DSP::MODULES::compressor::release(Flt ms) {
  parmRelease.store(std::clamp(ms, MIN_TIME_MS, MAX_TIME_MS));
  return *this;
}

Flt YSE::DSP::MODULES::compressor::release() {
  return parmRelease;
}

YSE::DSP::MODULES::compressor& YSE::DSP::MODULES::compressor::makeup(Flt db) {
  parmMakeup.store(std::clamp(db, -MAX_MAKEUP_DB, MAX_MAKEUP_DB));
  return *this;
}

Flt YSE::DSP::MODULES::compressor::makeup() {
  return parmMakeup;
}

Flt YSE::DSP::MODULES::compressor::gainReductionDb() {
  return reductionDb;
}

YSE::DSP::MODULES::compressor& YSE::DSP::MODULES::compressor::range(Flt db) {
  if (std::isfinite(db)) parmRange.store(std::clamp(db, 0.0f, MAX_RANGE));
  return *this;
}

Flt YSE::DSP::MODULES::compressor::range() {
  return parmRange;
}

YSE::DSP::MODULES::compressor& YSE::DSP::MODULES::compressor::freeze(bool on) {
  parmFreeze.store(on);
  return *this;
}

bool YSE::DSP::MODULES::compressor::freeze() {
  return parmFreeze;
}

YSE::DSP::MODULES::compressor& YSE::DSP::MODULES::compressor::progressiveRelease(bool on) {
  parmProgressive.store(on);
  return *this;
}

bool YSE::DSP::MODULES::compressor::progressiveRelease() {
  return parmProgressive;
}

YSE::DSP::MODULES::compressor& YSE::DSP::MODULES::compressor::gainSmoothing(Flt ms) {
  if (std::isfinite(ms)) parmSmoothing.store(std::clamp(ms, 0.0f, MAX_SMOOTHING));
  return *this;
}

Flt YSE::DSP::MODULES::compressor::gainSmoothing() {
  return parmSmoothing;
}

YSE::DSP::MODULES::compressor& YSE::DSP::MODULES::compressor::gate(bool on) {
  parmGate.store(on);
  return *this;
}

bool YSE::DSP::MODULES::compressor::gate() {
  return parmGate;
}

YSE::DSP::MODULES::compressor& YSE::DSP::MODULES::compressor::gateThreshold(Flt db) {
  if (std::isfinite(db)) parmGateThreshold.store(std::clamp(db, MIN_GATE_THRESHOLD, 0.0f));
  return *this;
}

Flt YSE::DSP::MODULES::compressor::gateThreshold() {
  return parmGateThreshold;
}

YSE::DSP::MODULES::compressor& YSE::DSP::MODULES::compressor::lookahead(UInt samples) {
  parmLookahead.store(std::min(samples, capacity - 1));
  return *this;
}

UInt YSE::DSP::MODULES::compressor::lookahead() {
  return parmLookahead;
}

UInt YSE::DSP::MODULES::compressor::latency() {
  return parmLookahead;
}

YSE::DSP::MODULES::compressor& YSE::DSP::MODULES::compressor::sidechainFilter(bool on) {
  parmSidechainFilter.store(on);
  return *this;
}

bool YSE::DSP::MODULES::compressor::sidechainFilter() {
  return parmSidechainFilter;
}

YSE::DSP::MODULES::compressor& YSE::DSP::MODULES::compressor::sidechainFrequency(Flt hz) {
  if (std::isfinite(hz))
    parmSidechainFrequency.store(std::clamp(hz, MIN_SIDECHAIN_FREQUENCY, MAX_SIDECHAIN_FREQUENCY));
  return *this;
}

Flt YSE::DSP::MODULES::compressor::sidechainFrequency() {
  return parmSidechainFrequency;
}

YSE::DSP::MODULES::compressor& YSE::DSP::MODULES::compressor::linked(bool on) {
  parmLinked.store(on);
  return *this;
}

bool YSE::DSP::MODULES::compressor::linked() {
  return parmLinked;
}

void YSE::DSP::MODULES::compressor::create() {
  rmsCoef = YSE::DSP::onePoleCoef(RMS_WINDOW_SEC, static_cast<Flt>(SAMPLERATE));
  shared = detectorState{};
}

void YSE::DSP::MODULES::compressor::process(MULTICHANNELBUFFER& buffer) {
  run(buffer, buffer);
}

void YSE::DSP::MODULES::compressor::process(MULTICHANNELBUFFER& buffer, MULTICHANNELBUFFER& key) {
  if (buffer.empty() || key.empty()) {
    run(buffer, buffer);
    return;
  }
  const UInt length = buffer[0].getLength();
  for (DSP::buffer& k : key)
    if (k.getLength() < length) {
      run(buffer, buffer);
      return;
    }
  run(buffer, key);
}

void YSE::DSP::MODULES::compressor::run(MULTICHANNELBUFFER& buffer, MULTICHANNELBUFFER& key) {
  createIfNeeded();

  if (buffer.empty()) return;

  // The scratch holds STANDARD_BUFFERSIZE samples from the constructor, so a
  // longer block is processed in pieces of at most that size: nothing is
  // allocated on the first block or when the block length grows (#1235).
  // The gain recursion runs per sample, so the cut does not change the result
  // while the settings stay put.
  const UInt length = buffer[0].getLength();
  for (UInt offset = 0; offset < length;) {
    const UInt piece = std::min<UInt>(length - offset, STANDARD_BUFFERSIZE);
    runPiece(buffer, key, offset, piece);
    offset += piece;
  }
}

Flt YSE::DSP::MODULES::compressor::highPass(highPassState& state, Flt in) const {
  const Dbl x = static_cast<Dbl>(in);
  Dbl y = hpA0 * x + hpA1 * state.x1 + hpA2 * state.x2 - hpB1 * state.y1 - hpB2 * state.y2;
  if (std::abs(y) < STATE_FLOOR) y = 0.0;
  state.x2 = state.x1;
  state.x1 = x;
  state.y2 = state.y1;
  state.y1 = y;
  return static_cast<Flt>(y);
}

Flt YSE::DSP::MODULES::compressor::step(detectorState& state, Flt level, const pieceSettings& p) {
  const Flt levelDb = linToDb(level);
  // Static curve: reduction only above threshold (slope <= 0).
  Flt targetGainDb = 0.0f;
  if (levelDb > p.thresholdDb) targetGainDb = (levelDb - p.thresholdDb) * p.slope;
  if (p.limitRange && targetGainDb < -p.rangeDb) targetGainDb = -p.rangeDb;
  const Flt targetGain = dbToLin(targetGainDb);

  Flt g = state.gain;
  if (!p.freeze) {
    // Attack when the gain must drop (more reduction), release when it recovers.
    Flt coef = p.attackCoef;
    if (!(targetGain < g)) {
      coef = p.releaseCoef;
      if (p.progressive) {
        const Flt depthDb = std::max(0.0f, -linToDb(g));
        coef = std::min(1.0f, coef * (1.0f + depthDb / PROGRESSIVE_DB));
      }
    }
    g = YSE::DSP::onePoleSmooth(g, targetGain, coef);
    state.gain = g;
  }

  Flt applied = g;
  if (p.smoothing) {
    state.smoothed = YSE::DSP::onePoleSmooth(state.smoothed, g, p.smoothCoef);
    applied = state.smoothed;
  } else {
    state.smoothed = g;
  }

  if (p.gate) {
    // The gate listens to the level held at its peaks and falling over the
    // release, so it does not chatter at a wave's zero crossings. It opens
    // over the attack and closes over the release.
    state.gateLevel =
        std::max(level, YSE::DSP::onePoleSmooth(state.gateLevel, 0.0f, p.releaseCoef));
    const Flt open = state.gateLevel < p.gateThresholdLin ? 0.0f : 1.0f;
    const Flt coef = open < state.gate ? p.releaseCoef : p.attackCoef;
    state.gate = YSE::DSP::onePoleSmooth(state.gate, open, coef);
    applied *= state.gate;
  } else {
    state.gate = 1.0f;
    state.gateLevel = level;
  }
  return applied;
}

void YSE::DSP::MODULES::compressor::apply(MULTICHANNELBUFFER& buffer, std::size_t ch, UInt offset,
                                          UInt length, UInt delay) {
  Flt* x = buffer[ch].getPtr() + offset; // dry input (preserved for calculateImpact)
  if (delay > 0 && ch < channelCapacity) {
    // The lookahead: the dry signal comes out delayed, gain and dry alike.
    Flt* line = lines.data() + ch * capacity;
    UInt pos = writePos;
    for (UInt i = 0; i < length; ++i) {
      line[pos] = x[i];
      x[i] = line[pos >= delay ? pos - delay : pos + capacity - delay];
      if (++pos == capacity) pos = 0;
    }
  }
  const Flt* gb = gainBuf.getPtr();
  Flt* w = wet.getPtr();
  for (UInt i = 0; i < length; ++i)
    w[i] = x[i] * gb[i];
  if (length == buffer[ch].getLength()) {
    calculateImpact(buffer[ch], wet);
  } else {
    // A piece of a longer block: mix through the dry scratch.
    if (dry.getLength() != length) dry.resize(length);
    Flt* d = dry.getPtr();
    std::copy(x, x + length, d);
    calculateImpact(dry, wet);
    std::copy(d, d + length, x);
  }
}

void YSE::DSP::MODULES::compressor::runPiece(MULTICHANNELBUFFER& buffer, MULTICHANNELBUFFER& key,
                                             UInt offset, UInt length) {
  const std::size_t n = buffer.size();
  // The detector's channels: the buffer's own, or the sidechain's.
  const std::size_t keys = key.size();

  // Within the capacity set in the constructor: no allocation.
  if (gainBuf.getLength() != length) gainBuf.resize(length);
  if (wet.getLength() != length) wet.resize(length);

  // Snapshot parameters once per piece (control-thread atomics → audio locals).
  const Flt sr = static_cast<Flt>(SAMPLERATE);
  pieceSettings p{};
  p.thresholdDb = parmThreshold.load();
  p.slope = 1.0f / parmRatio.load() - 1.0f;
  p.attackCoef = YSE::DSP::onePoleCoef(parmAttack.load() * 0.001f, sr);
  p.releaseCoef = YSE::DSP::onePoleCoef(parmRelease.load() * 0.001f, sr);
  p.rangeDb = parmRange.load();
  p.limitRange = p.rangeDb < MAX_RANGE;
  p.freeze = parmFreeze.load();
  p.progressive = parmProgressive.load();
  const Flt smoothingMs = parmSmoothing.load();
  p.smoothing = smoothingMs > 0.0f;
  p.smoothCoef = p.smoothing ? YSE::DSP::onePoleCoef(smoothingMs * 0.001f, sr) : 1.0f;
  p.gate = parmGate.load();
  p.gateThresholdLin = dbToLin(parmGateThreshold.load());
  const Flt makeupLin = dbToLin(parmMakeup.load());
  const bool rms = (parmDetector.load() == DETECT_RMS);
  const bool filter = parmSidechainFilter.load();
  const bool link = parmLinked.load();
  // Refresh the RMS window coefficient in case the sample rate changed.
  rmsCoef = YSE::DSP::onePoleCoef(RMS_WINDOW_SEC, sr);

  if (filter) {
    const Flt frequency = parmSidechainFrequency.load();
    if (frequency != hpBuiltFrequency || SAMPLERATE != hpBuiltRate) {
      const YSE::DSP::filterCoefficients::preciseSet c =
          YSE::DSP::filterCoefficients::ComputePrecise(YSE::DSP::filterCoefficients::HIGHPASS,
                                                       frequency, 1.0f, BUTTERWORTH_Q, sr);
      hpA0 = c.a0;
      hpA1 = c.a1;
      hpA2 = c.a2;
      hpB1 = c.b1;
      hpB2 = c.b2;
      hpBuiltFrequency = frequency;
      hpBuiltRate = SAMPLERATE;
    }
  }

  // A new lookahead empties the delay line, so it never reads stale samples.
  const UInt wantDelay = std::min(parmLookahead.load(), capacity - 1);
  if (wantDelay != delay) {
    std::fill(lines.begin(), lines.end(), 0.0f);
    writePos = 0;
    delay = wantDelay;
  }

  // Switching between linked and per-channel detection carries the gain over.
  if (link != wasLinked) {
    if (link)
      shared = channelStates[0];
    else
      std::fill(channelStates.begin(), channelStates.end(), shared);
    wasLinked = link;
  }

  Flt* gb = gainBuf.getPtr();

  if (link) {
    // ─── Pass 1: one linked sidechain → one gain curve for every channel ───
    detectorState state = shared;
    Flt applied = state.gain;
    for (UInt i = 0; i < length; ++i) {
      // Linked detector level across all channels at this sample.
      Flt level;
      if (rms) {
        Flt sumSq = 0.0f;
        for (std::size_t ch = 0; ch < keys; ++ch) {
          Flt s = key[ch].getPtr()[offset + i];
          if (filter && ch < channelCapacity) s = highPass(highPasses[ch], s);
          sumSq += s * s;
        }
        const Flt meanSq = sumSq / static_cast<Flt>(keys);
        state.ms = YSE::DSP::onePoleSmooth(state.ms, meanSq, rmsCoef);
        level = std::sqrt(state.ms);
      } else {
        Flt peak = 0.0f;
        for (std::size_t ch = 0; ch < keys; ++ch) {
          Flt s = key[ch].getPtr()[offset + i];
          if (filter && ch < channelCapacity) s = highPass(highPasses[ch], s);
          const Flt a = std::abs(s);
          if (a > peak) peak = a;
        }
        level = peak;
      }
      applied = step(state, level, p);
      gb[i] = applied * makeupLin;
    }
    shared = state;
    reductionDb.store(linToDb(applied)); // pre-makeup reduction, for metering

    // ─── Pass 2: apply the shared gain to every channel, then wet/dry mix ───
    for (std::size_t ch = 0; ch < n; ++ch)
      apply(buffer, ch, offset, length, delay);
  } else {
    // ─── One detector and gain per channel ───
    Flt deepest = 1.0f;
    for (std::size_t ch = 0; ch < n && ch < channelCapacity; ++ch) {
      const Flt* k = key[std::min(ch, keys - 1)].getPtr() + offset;
      detectorState state = channelStates[ch];
      highPassState& hp = highPasses[ch];
      Flt applied = state.gain;
      for (UInt i = 0; i < length; ++i) {
        Flt s = k[i];
        if (filter) s = highPass(hp, s);
        Flt level;
        if (rms) {
          state.ms = YSE::DSP::onePoleSmooth(state.ms, s * s, rmsCoef);
          level = std::sqrt(state.ms);
        } else {
          level = std::abs(s);
        }
        applied = step(state, level, p);
        gb[i] = applied * makeupLin;
      }
      channelStates[ch] = state;
      deepest = std::min(deepest, applied);
      apply(buffer, ch, offset, length, delay);
    }
    reductionDb.store(linToDb(deepest));
  }

  if (delay > 0) writePos = (writePos + length) % capacity;
}
