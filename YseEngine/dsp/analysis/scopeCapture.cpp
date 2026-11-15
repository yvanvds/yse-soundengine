#include "scopeCapture.hpp"

#include <cmath>

namespace {
  inline UInt Clamp(UInt value, UInt low, UInt high) {
    return value < low ? low : (value > high ? high : value);
  }

  // A float setting as a whole number, MIN to MAX, NaN the default.
  inline UInt Whole(Flt value, UInt low, UInt high, UInt fallback) {
    if (std::isnan(value)) return fallback;
    if (value <= static_cast<Flt>(low)) return low;
    if (value >= static_cast<Flt>(high)) return high;
    return static_cast<UInt>(value);
  }
} // namespace

namespace YSE {
  namespace DSP {

    scopeCapture::scopeCapture(UInt capacity)
      : cap(Clamp(capacity, MIN_SIZE, MAX_SIZE)),
        points(DEFAULT_SIZE < cap ? DEFAULT_SIZE : cap),
        samples(DEFAULT_PERIOD),
        delayMs(0.f),
        when(NONE),
        threshold(0.f),
        mode(WAVEFORM),
        phase(ARMED),
        waitLeft(0),
        point(0),
        inPeriod(0),
        low(0.f),
        high(0.f),
        previous(std::nanf("")),
        serial(0),
        writeSlot(0),
        published(1),
        finished(0),
        readSlot(2) {
      store.assign(static_cast<std::size_t>(cap) * 6, 0.f);
    }

    Flt* scopeCapture::first(unsigned int s) {
      return store.data() + static_cast<std::size_t>(s) * 2 * cap;
    }

    Flt* scopeCapture::second(unsigned int s) {
      return first(s) + cap;
    }

    const Flt* scopeCapture::first(unsigned int s) const {
      return store.data() + static_cast<std::size_t>(s) * 2 * cap;
    }

    const Flt* scopeCapture::second(unsigned int s) const {
      return first(s) + cap;
    }

    scopeCapture& scopeCapture::size(UInt value) {
      const UInt wanted = Clamp(value, MIN_SIZE, cap);
      if (wanted != points) {
        points = wanted;
        restart();
      }
      return *this;
    }

    UInt scopeCapture::size() const {
      return points;
    }

    scopeCapture& scopeCapture::period(UInt value) {
      const UInt wanted = Clamp(value, MIN_PERIOD, MAX_PERIOD);
      if (wanted != samples) {
        samples = wanted;
        restart();
      }
      return *this;
    }

    UInt scopeCapture::period() const {
      return samples;
    }

    scopeCapture& scopeCapture::delay(Flt ms) {
      if (std::isnan(ms)) return *this;
      delayMs = ms < 0.f ? 0.f : ms;
      return *this;
    }

    Flt scopeCapture::delay() const {
      return delayMs;
    }

    scopeCapture& scopeCapture::trigger(TRIGGER value) {
      when = value;
      return *this;
    }

    scopeCapture::TRIGGER scopeCapture::trigger() const {
      return when;
    }

    scopeCapture& scopeCapture::level(Flt value) {
      if (!std::isnan(value)) threshold = value;
      return *this;
    }

    Flt scopeCapture::level() const {
      return threshold;
    }

    UInt scopeCapture::capacity() const {
      return cap;
    }

    scopeCapture& scopeCapture::restart() {
      phase = ARMED;
      point = 0;
      inPeriod = 0;
      return *this;
    }

    void scopeCapture::setMode(MODE wanted) {
      if (wanted == mode) return;
      mode = wanted;
      restart();
    }

    void scopeCapture::step(Flt a, Flt b, bool xy) {
      if (std::isnan(a)) a = 0.f;
      if (std::isnan(b)) b = 0.f;

      if (phase == WAITING) {
        if (waitLeft > 0) {
          waitLeft--;
          previous = a;
          return;
        }
        phase = ARMED;
      }
      if (phase == ARMED) {
        const Flt before = previous;
        previous = a;
        // NaN before the first sample: no crossing.
        if (when == UP && !(before < threshold && a >= threshold)) return;
        if (when == DOWN && !(before > threshold && a <= threshold)) return;
        phase = CAPTURING;
        point = 0;
        inPeriod = 0;
      } else {
        previous = a;
      }

      Flt* x = first(writeSlot);
      if (inPeriod == 0) {
        if (xy) {
          x[point] = a;
          second(writeSlot)[point] = b;
        } else {
          low = a;
          high = a;
        }
      } else if (!xy) {
        if (a < low) low = a;
        if (a > high) high = a;
      }
      if (++inPeriod < samples) return;
      if (!xy) {
        x[point] = low;
        second(writeSlot)[point] = high;
      }
      inPeriod = 0;
      if (++point < points) return;
      publish();
    }

    void scopeCapture::publish() {
      slot& s = slots[writeSlot];
      s.mode = mode;
      s.size = points;
      s.serial = ++serial;
      // Hand the trace over and take back the slot the reader let go of.
      writeSlot = published.exchange(writeSlot | FRESH, std::memory_order_acq_rel) & INDEX;
      finished.store(serial, std::memory_order_release);

      phase = WAITING;
      point = 0;
      inPeriod = 0;
      waitLeft = 0;
      if (SAMPLERATE > 0 && delayMs > 0.f) {
        const Dbl wait = static_cast<Dbl>(delayMs) * static_cast<Dbl>(SAMPLERATE) / 1000.0;
        waitLeft = static_cast<U64>(std::llround(wait));
      }
    }

    void scopeCapture::operator()(buffer& in) {
      setMode(WAVEFORM);
      const Flt* p = in.getPtr();
      const UInt length = in.getLength();
      for (UInt i = 0; i < length; i++)
        step(p[i], 0.f, false);
    }

    void scopeCapture::operator()(buffer& x, buffer& y) {
      setMode(XY);
      const Flt* px = x.getPtr();
      const Flt* py = y.getPtr();
      const UInt length = x.getLength() < y.getLength() ? x.getLength() : y.getLength();
      for (UInt i = 0; i < length; i++)
        step(px[i], py[i], true);
    }

    UInt scopeCapture::traces() const {
      return finished.load(std::memory_order_acquire);
    }

    scopeCapture::trace scopeCapture::acquire() {
      if ((published.load(std::memory_order_acquire) & FRESH) != 0)
        readSlot = published.exchange(readSlot, std::memory_order_acq_rel) & INDEX;
      return held();
    }

    scopeCapture::trace scopeCapture::held() const {
      trace t;
      const slot& s = slots[readSlot];
      if (s.serial == 0) return t;
      t.mode = s.mode;
      t.size = s.size;
      t.serial = s.serial;
      t.first = first(readSlot);
      t.second = second(readSlot);
      return t;
    }

    UInt scopeCapture::Size(Flt value) {
      return Whole(value, MIN_SIZE, MAX_SIZE, DEFAULT_SIZE);
    }

    UInt scopeCapture::Period(Flt value) {
      return Whole(value, MIN_PERIOD, MAX_PERIOD, DEFAULT_PERIOD);
    }

    scopeCapture::TRIGGER scopeCapture::Trigger(Flt value) {
      if (std::isnan(value)) return NONE;
      const Flt whole = std::trunc(value);
      if (whole == 1.f) return UP;
      if (whole == 2.f) return DOWN;
      return NONE;
    }

  } // namespace DSP
} // namespace YSE
