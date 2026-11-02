/*
  ==============================================================================

    compressor.cpp
    Channel-strip dynamics compressor module (issue #163).

  ==============================================================================
*/

#include "compressor.hpp"
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

YSE::DSP::MODULES::compressor::compressor()
  : parmThreshold(-18.0f),
    parmRatio(4.0f),
    parmAttack(10.0f),
    parmRelease(100.0f),
    parmMakeup(0.0f),
    parmDetector(DETECT_PEAK),
    gain(1.0f),
    msEnv(0.0f),
    rmsCoef(0.0f),
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

void YSE::DSP::MODULES::compressor::create() {
  rmsCoef = YSE::DSP::onePoleCoef(RMS_WINDOW_SEC, static_cast<Flt>(SAMPLERATE));
  gain = 1.0f;
  msEnv = 0.0f;
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
  const Flt thresholdDb = parmThreshold.load();
  const Flt ratioVal = parmRatio.load();
  const Flt slope = 1.0f / ratioVal - 1.0f; // <= 0 : dB out per dB over threshold
  const Flt attackCoef = YSE::DSP::onePoleCoef(parmAttack.load() * 0.001f, sr);
  const Flt releaseCoef = YSE::DSP::onePoleCoef(parmRelease.load() * 0.001f, sr);
  const Flt makeupLin = dbToLin(parmMakeup.load());
  const bool rms = (parmDetector.load() == DETECT_RMS);
  // Refresh the RMS window coefficient in case the sample rate changed.
  rmsCoef = YSE::DSP::onePoleCoef(RMS_WINDOW_SEC, sr);

  Flt g = gain;
  Flt ms = msEnv;
  Flt* gb = gainBuf.getPtr();

  // ─── Pass 1: one linked sidechain → one gain curve for every channel ───
  for (UInt i = 0; i < length; ++i) {
    // Linked detector level across all channels at this sample.
    Flt level;
    if (rms) {
      Flt sumSq = 0.0f;
      for (std::size_t ch = 0; ch < keys; ++ch) {
        const Flt s = key[ch].getPtr()[offset + i];
        sumSq += s * s;
      }
      const Flt meanSq = sumSq / static_cast<Flt>(keys);
      ms = YSE::DSP::onePoleSmooth(ms, meanSq, rmsCoef);
      level = std::sqrt(ms);
    } else {
      Flt peak = 0.0f;
      for (std::size_t ch = 0; ch < keys; ++ch) {
        const Flt a = std::abs(key[ch].getPtr()[offset + i]);
        if (a > peak) peak = a;
      }
      level = peak;
    }

    const Flt levelDb = linToDb(level);
    // Static curve: reduction only above threshold (slope <= 0).
    Flt targetGainDb = 0.0f;
    if (levelDb > thresholdDb) targetGainDb = (levelDb - thresholdDb) * slope;
    const Flt targetGain = dbToLin(targetGainDb);

    // Attack when the gain must drop (more reduction), release when it recovers.
    const Flt coef = (targetGain < g) ? attackCoef : releaseCoef;
    g = YSE::DSP::onePoleSmooth(g, targetGain, coef);

    gb[i] = g * makeupLin;
  }

  gain = g;
  msEnv = ms;
  reductionDb.store(linToDb(g)); // pre-makeup reduction, for metering

  // ─── Pass 2: apply the shared gain to every channel, then wet/dry mix ───
  for (std::size_t ch = 0; ch < n; ++ch) {
    Flt* x = buffer[ch].getPtr() + offset; // dry input (preserved for calculateImpact)
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
}
