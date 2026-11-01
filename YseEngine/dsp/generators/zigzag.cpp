#include "zigzag.hpp"

#include <cmath>

namespace {
  using YSE::DSP::buffer;

  inline UInt Clamp(UInt length) {
    return length > YSE::STANDARD_BUFFERSIZE ? YSE::STANDARD_BUFFERSIZE : length;
  }

  // Within the capacity set in the constructor: no allocation.
  inline void Fit(buffer& b, UInt length) {
    if (b.getLength() != length) b.resize(length);
  }

  inline UInt Capacity(UInt requested) {
    if (requested < 1) return 1;
    if (requested > YSE::DSP::zigzag::MAX_POINTS) return YSE::DSP::zigzag::MAX_POINTS;
    return requested;
  }

  inline Flt CleanTime(Flt ms) {
    return ms > 0.f && std::isfinite(ms) ? ms : 0.f;
  }

  // The end of the range: `last`, or the last point when it is negative or
  // past the list. `count` is not 0.
  inline UInt LastIndex(Int last, UInt count) {
    if (last < 0 || static_cast<UInt>(last) >= count) return count - 1;
    return static_cast<UInt>(last);
  }

  // Milliseconds that pass in one sample at the live rate; 0 holds.
  inline Dbl MsPerSample() {
    if (YSE::SAMPLERATE <= 0) return 0.0;
    return 1000.0 / static_cast<Dbl>(YSE::SAMPLERATE);
  }
} // namespace

namespace YSE {
  namespace DSP {

    zigzag::zigzag(UInt capacity) : output(STANDARD_BUFFERSIZE), indexOutput(STANDARD_BUFFERSIZE) {
      const UInt size = Capacity(capacity);
      values.assign(size, 0.f);
      deltas.assign(size, 0.f);
    }

    UInt zigzag::Low() const {
      if (count == 0) return 0;
      const UInt a = first < count ? first : count - 1;
      const UInt b = LastIndex(last, count);
      return a < b ? a : b;
    }

    UInt zigzag::High() const {
      if (count == 0) return 0;
      const UInt a = first < count ? first : count - 1;
      const UInt b = LastIndex(last, count);
      return a < b ? b : a;
    }

    UInt zigzag::setLine(const Flt* newValues, const Flt* newDeltas, UInt length) {
      if (length == 0 || newValues == nullptr || newDeltas == nullptr) return 0;
      const UInt n = length > capacity() ? capacity() : length;
      for (UInt i = 0; i < n; ++i) {
        if (!std::isfinite(newValues[i])) return 0;
      }
      for (UInt i = 0; i < n; ++i) {
        values[i] = newValues[i];
        deltas[i] = CleanTime(newDeltas[i]);
      }
      count = n;
      running = false;
      target = 0;
      reached = 0;
      pendulum = 1;
      return n;
    }

    UInt zigzag::line(const Flt* newValues, const Flt* newDeltas, UInt length) {
      const UInt n = setLine(newValues, newDeltas, length);
      if (n > 0) play();
      return n;
    }

    zigzag& zigzag::clear() {
      count = 0;
      running = false;
      target = 0;
      reached = 0;
      pendulum = 1;
      return *this;
    }

    bool zigzag::append(Flt value, Flt ms, bool trig) {
      if (count >= capacity() || !std::isfinite(value)) return false;
      values[count] = value;
      deltas[count] = CleanTime(ms);
      count++;
      if (trig) Go(static_cast<Int>(count - 1), ms);
      return true;
    }

    bool zigzag::insert(UInt index, Flt value, Flt ms) {
      if (count >= capacity() || !std::isfinite(value)) return false;
      if (index > count) index = count;
      for (UInt i = count; i > index; --i) {
        values[i] = values[i - 1];
        deltas[i] = deltas[i - 1];
      }
      values[index] = value;
      deltas[index] = CleanTime(ms);
      count++;
      // The running ramp and the last point reached follow their points.
      if (running && target >= index) target++;
      if (reached >= index && count > 1) reached++;
      if (reached >= count) reached = count - 1;
      return true;
    }

    bool zigzag::remove(UInt index) {
      if (index >= count) return false;
      for (UInt i = index; i + 1 < count; ++i) {
        values[i] = values[i + 1];
        deltas[i] = deltas[i + 1];
      }
      count--;
      if (count == 0) {
        running = false;
        target = 0;
        reached = 0;
        return true;
      }
      // A removed target is replaced by the point that took its place.
      if (target > index) target--;
      if (target >= count) target = count - 1;
      if (reached > index) reached--;
      if (reached >= count) reached = count - 1;
      return true;
    }

    bool zigzag::set(UInt index, Flt value, Flt ms) {
      if (index >= count || !std::isfinite(value)) return false;
      values[index] = value;
      deltas[index] = CleanTime(ms);
      return true;
    }

    void zigzag::Go(Int index, Flt ms) {
      if (count == 0) return;
      if (index < 0) index = 0;
      if (static_cast<UInt>(index) >= count) index = static_cast<Int>(count - 1);
      target = static_cast<UInt>(index);
      from = current;
      duration = CleanTime(ms);
      elapsed = 0.0;
      running = true;
    }

    zigzag& zigzag::play() {
      if (count == 0) return *this;
      if (Direction() > 0) {
        const UInt lo = Low();
        Go(static_cast<Int>(lo), deltas[lo]);
      } else {
        Go(static_cast<Int>(High()), 0.f);
      }
      return *this;
    }

    zigzag& zigzag::bang() {
      if (count == 0) return *this;
      if (trigger == STEP) return skip(Direction(), bangMs);
      Go(static_cast<Int>(Direction() > 0 ? Low() : High()), bangMs);
      return *this;
    }

    zigzag& zigzag::jump(Int index, Flt ms) {
      Go(index, ms);
      return *this;
    }

    zigzag& zigzag::jumpStart(Flt ms) {
      Go(static_cast<Int>(Low()), ms);
      return *this;
    }

    zigzag& zigzag::jumpEnd(Flt ms) {
      Go(static_cast<Int>(High()), ms);
      return *this;
    }

    zigzag& zigzag::skip(Int points, Flt ms) {
      Go(static_cast<Int>(index()) + points, ms);
      return *this;
    }

    zigzag& zigzag::stop() {
      running = false;
      return *this;
    }

    zigzag& zigzag::hold(Flt value) {
      if (!std::isfinite(value)) return *this;
      current = value;
      running = false;
      return *this;
    }

    zigzag& zigzag::bangDelta(Flt ms) {
      bangMs = CleanTime(ms);
      return *this;
    }

    zigzag& zigzag::start(UInt index) {
      first = index;
      return *this;
    }

    zigzag& zigzag::end(Int index) {
      last = index < 0 ? -1 : index;
      return *this;
    }

    zigzag& zigzag::speed(Flt value) {
      if (!std::isfinite(value)) return *this;
      rate = value;
      if (value == 0.f) return *this;
      const int wanted = value < 0.f ? -1 : 1;
      if (wanted == sign) return *this;
      const int before = Direction();
      sign = wanted;
      if (!running) return *this;
      // Turn back to the point on the other side, over the part of the
      // segment between the two points that lies behind: on a segment
      // played as written, the way back takes as long as the way so far.
      const Int back = static_cast<Int>(target) - before;
      if (back < 0 || static_cast<UInt>(back) >= count) return *this;
      const UInt behind = static_cast<UInt>(back);
      const Dbl segment = deltas[behind > target ? behind : target];
      const Dbl span = static_cast<Dbl>(values[target]) - static_cast<Dbl>(values[behind]);
      Dbl part = 0.0;
      if (span != 0.0) {
        part = (static_cast<Dbl>(current) - static_cast<Dbl>(values[behind])) / span;
      } else if (duration > 0.0) {
        part = elapsed / duration;
      }
      if (!(part > 0.0)) part = 0.0; // also NaN
      if (part > 1.0) part = 1.0;
      target = behind;
      from = current;
      duration = part * segment;
      elapsed = 0.0;
      return *this;
    }

    zigzag& zigzag::loopMode(LOOP value) {
      if (value != LOOP_ON && value != LOOP_PENDULUM) value = LOOP_OFF;
      loop = value;
      if (loop != LOOP_PENDULUM) pendulum = 1;
      return *this;
    }

    zigzag& zigzag::mode(MODE value) {
      if (value != TRIGGER && value != STEP) value = START;
      trigger = value;
      return *this;
    }

    zigzag& zigzag::reset() {
      count = 0;
      first = 0;
      last = -1;
      rate = 1.f;
      sign = 1;
      pendulum = 1;
      loop = LOOP_OFF;
      trigger = START;
      bangMs = 0.f;
      running = false;
      target = 0;
      reached = 0;
      from = 0.f;
      current = 0.f;
      duration = 0.0;
      elapsed = 0.0;
      endCount = 0;
      primed = false;
      previous = 0.f;
      slope = 0.f;
      return *this;
    }

    bool zigzag::Advance() {
      const int dir = Direction();
      const UInt lo = Low();
      const UInt hi = High();
      const bool atEnd = dir > 0 ? target >= hi : target <= lo;
      if (!atEnd) {
        if (dir > 0) {
          target++;
          duration = deltas[target];
        } else {
          duration = deltas[target];
          target--;
        }
        from = current;
        return true;
      }

      endCount++;
      if (loop == LOOP_OFF || lo == hi) {
        running = false;
        return false;
      }
      from = current;
      if (loop == LOOP_ON) {
        // Round again over the range's first delta, either way.
        target = dir > 0 ? lo : hi;
        duration = deltas[lo];
        return true;
      }
      // Pendulum: turn round.
      pendulum = -pendulum;
      if (dir > 0) {
        const UInt end = target > hi ? hi : target;
        duration = deltas[end];
        target = end - 1;
      } else {
        const UInt start = target < lo ? lo : target;
        target = start + 1;
        duration = deltas[target];
      }
      return true;
    }

    Flt zigzag::Step() {
      if (!running || count == 0) return current;
      const Dbl step = MsPerSample() * std::fabs(static_cast<Dbl>(rate));
      if (step <= 0.0) return current;
      elapsed += step;
      // Zero-length ramps take no time; this bounds a range of them.
      for (UInt guard = 0; guard <= count + 1; ++guard) {
        if (elapsed < duration) {
          const Flt f = static_cast<Flt>(elapsed / duration);
          current = from + (values[target] - from) * f;
          return current;
        }
        current = values[target];
        reached = target;
        elapsed -= duration;
        if (!Advance()) {
          elapsed = 0.0;
          return current;
        }
      }
      elapsed = 0.0;
      return current;
    }

    buffer& zigzag::operator()(UInt length) {
      return (*this)(nullptr, nullptr, length);
    }

    buffer& zigzag::operator()(buffer* input, buffer* rateSignal, UInt length) {
      endCount = 0;
      const UInt n = Clamp(length);
      Fit(output, n);
      Fit(indexOutput, n);
      Flt* out = output.getPtr();
      Flt* idx = indexOutput.getPtr();
      if (rateSignal != nullptr && rateSignal->getLength() > 0) speed(rateSignal->getPtr()[0]);
      const UInt available = input != nullptr ? input->getLength() : 0;
      const Flt* in = input != nullptr ? input->getPtr() : nullptr;

      if (in != nullptr && trigger == START) {
        // The input is an index into the list.
        for (UInt i = 0; i < n; ++i) {
          Flt x = i < available ? in[i] : 0.f;
          if (count == 0) {
            out[i] = current;
            idx[i] = 0.f;
            continue;
          }
          const Flt top = static_cast<Flt>(count - 1);
          if (!(x > 0.f)) x = 0.f; // also NaN
          if (x > top) x = top;
          const UInt a = static_cast<UInt>(x);
          const UInt b = a + 1 < count ? a + 1 : a;
          const Flt f = x - static_cast<Flt>(a);
          current = values[a] + (values[b] - values[a]) * f;
          out[i] = current;
          idx[i] = x;
        }
        return output;
      }

      for (UInt i = 0; i < n; ++i) {
        if (in != nullptr) {
          Flt x = i < available ? in[i] : previous;
          if (!std::isfinite(x)) x = previous;
          if (primed) {
            const Flt d = x - previous;
            if (d < 0.f && slope >= 0.f) bang();
            slope = d;
          }
          primed = true;
          previous = x;
        }
        out[i] = Step();
        idx[i] = static_cast<Flt>(index());
      }
      return output;
    }

  } // namespace DSP
} // namespace YSE
