#include "levelMeter.hpp"

#include <cmath>

namespace {
  // Below -200 dB the level is 0, so a release never decays into denormals.
  constexpr Flt FLOOR = 1e-10f;

  inline Flt NonNegative(Flt ms) {
    return (std::isnan(ms) || ms < 0.f) ? 0.f : ms;
  }
} // namespace

namespace YSE {
  namespace DSP {

    levelMeter::levelMeter()
      : mode(PEAK),
        intervalMs(20.f),
        attackMs(0.f),
        releaseMs(0.f),
        holdMs(0.f),
        offsetDb(0.f),
        windowPeak(0.f),
        windowSquares(0.0),
        windowCount(0),
        elapsed(0.0),
        current(0.f),
        held(0.f),
        holdLeft(0.f),
        clip(false),
        count(0) {}

    // The window in samples at the live rate, at least one.
    Dbl levelMeter::period() const {
      if (SAMPLERATE <= 0) return 1.0;
      const Dbl samples = static_cast<Dbl>(intervalMs) * static_cast<Dbl>(SAMPLERATE) / 1000.0;
      return samples < 1.0 ? 1.0 : samples;
    }

    void levelMeter::track(Flt in) {
      // NaN compares false and is skipped.
      if (std::isnan(in)) return;
      const Flt magnitude = std::fabs(in);
      if (magnitude > windowPeak) windowPeak = magnitude;
      if (magnitude > 1.f) clip = true;
      windowSquares += static_cast<Dbl>(in) * static_cast<Dbl>(in);
      windowCount++;
    }

    void levelMeter::endWindow(Dbl samples) {
      Flt value = windowPeak;
      if (mode == RMS) {
        value = windowCount == 0
                    ? 0.f
                    : static_cast<Flt>(std::sqrt(windowSquares / static_cast<Dbl>(windowCount)));
      }
      windowPeak = 0.f;
      windowSquares = 0.0;
      windowCount = 0;
      const Dbl rate = SAMPLERATE > 0 ? static_cast<Dbl>(SAMPLERATE) : 1.0;
      apply(value, static_cast<Flt>(samples * 1000.0 / rate));
    }

    void levelMeter::apply(Flt value, Flt ms) {
      const Flt time = value > current ? attackMs : releaseMs;
      // An infinite level has no distance to cover: it jumps.
      if (time <= 0.f || !std::isfinite(current)) {
        current = value;
      } else {
        current += (value - current) * (1.f - std::exp(-ms / time));
      }
      if (current < FLOOR) current = 0.f;

      if (current >= held) {
        held = current;
        holdLeft = holdMs;
      } else {
        holdLeft -= ms;
        if (holdLeft <= 0.f) {
          held = current;
          holdLeft = 0.f;
        }
      }
    }

    bool levelMeter::process(Flt in) {
      track(in);
      const Dbl samples = period();
      elapsed += 1.0;
      if (elapsed < samples) return false;
      // Keep the fraction so a fractional period does not drift; a period
      // that just shrank below the window starts over.
      elapsed -= samples;
      if (elapsed >= samples) elapsed = 0.0;
      endWindow(samples);
      return true;
    }

    UInt levelMeter::operator()(buffer& in) {
      const UInt length = in.getLength();
      const Flt* a = in.getPtr();
      const Dbl samples = period();
      UInt c = 0;
      for (UInt i = 0; i < length; ++i) {
        track(a[i]);
        elapsed += 1.0;
        if (elapsed < samples) continue;
        elapsed -= samples;
        if (elapsed >= samples) elapsed = 0.0;
        endWindow(samples);
        c++;
      }
      count = c;
      return c;
    }

    UInt levelMeter::reports() const {
      return count;
    }

    levelMeter& levelMeter::set(Flt amplitude) {
      if (std::isnan(amplitude)) return *this;
      const Flt magnitude = std::fabs(amplitude);
      if (magnitude > 1.f) clip = true;
      apply(magnitude, static_cast<Flt>(period() * 1000.0 /
                                        (SAMPLERATE > 0 ? static_cast<Dbl>(SAMPLERATE) : 1.0)));
      return *this;
    }

    levelMeter& levelMeter::setDb(Flt dB) {
      if (std::isnan(dB)) return *this;
      return set(std::pow(10.f, (dB + offsetDb) / 20.f));
    }

    Flt levelMeter::toDb(Flt amplitude) const {
      if (!(amplitude > 0.f)) return SILENCE;
      const Flt db = 20.f * std::log10(amplitude) - offsetDb;
      return db < SILENCE ? SILENCE : db;
    }

    Flt levelMeter::level() const {
      return current;
    }

    Flt levelMeter::levelDb() const {
      return toDb(current);
    }

    Flt levelMeter::peakHold() const {
      return held;
    }

    Flt levelMeter::peakHoldDb() const {
      return toDb(held);
    }

    bool levelMeter::clipped() const {
      return clip;
    }

    levelMeter& levelMeter::clear() {
      clip = false;
      held = current;
      holdLeft = 0.f;
      return *this;
    }

    levelMeter& levelMeter::reset() {
      windowPeak = 0.f;
      windowSquares = 0.0;
      windowCount = 0;
      elapsed = 0.0;
      current = 0.f;
      held = 0.f;
      holdLeft = 0.f;
      clip = false;
      count = 0;
      return *this;
    }

    levelMeter& levelMeter::detector(DETECTOR d) {
      mode = d == RMS ? RMS : PEAK;
      return *this;
    }

    levelMeter::DETECTOR levelMeter::detector() const {
      return mode;
    }

    levelMeter& levelMeter::interval(Flt ms) {
      if (std::isnan(ms)) return *this;
      const Flt clean = ms < 0.f ? 0.f : ms;
      if (clean != intervalMs) {
        intervalMs = clean;
        elapsed = 0.0;
      }
      return *this;
    }

    Flt levelMeter::interval() const {
      return intervalMs;
    }

    levelMeter& levelMeter::attack(Flt ms) {
      attackMs = NonNegative(ms);
      return *this;
    }

    Flt levelMeter::attack() const {
      return attackMs;
    }

    levelMeter& levelMeter::release(Flt ms) {
      releaseMs = NonNegative(ms);
      return *this;
    }

    Flt levelMeter::release() const {
      return releaseMs;
    }

    levelMeter& levelMeter::hold(Flt ms) {
      holdMs = NonNegative(ms);
      return *this;
    }

    Flt levelMeter::hold() const {
      return holdMs;
    }

    levelMeter& levelMeter::offset(Flt dB) {
      offsetDb = std::isnan(dB) ? 0.f : dB;
      return *this;
    }

    Flt levelMeter::offset() const {
      return offsetDb;
    }

    levelMeter::ballisticsSettings levelMeter::settings(BALLISTICS standard) {
      switch (standard) {
      case VU:
        return {330.f, 330.f, -20.f};
      case DIN:
        return {10.f, 1500.f, -14.f};
      case BBC:
        return {10.f, 3000.f, -14.f};
      case NORDIC:
        return {5.f, 2000.f, -14.f};
      case EBU:
        return {0.f, 1500.f, -14.f};
      case FAST:
      default:
        return {0.f, 0.f, 0.f};
      }
    }

    levelMeter& levelMeter::ballistics(BALLISTICS standard) {
      const ballisticsSettings s = settings(standard);
      attackMs = s.attack;
      releaseMs = s.release;
      offsetDb = s.offset;
      return *this;
    }

    Flt levelMeter::position(Flt dB, Flt floorDb, Flt ceilingDb) {
      const Flt span = ceilingDb - floorDb;
      if (std::isnan(dB) || !(span > 0.f) || dB <= floorDb) return 0.f;
      return (dB - floorDb) / span;
    }

  } // namespace DSP
} // namespace YSE
