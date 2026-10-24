#include "oscillatorBank.hpp"

#include <cmath>

namespace {
  using YSE::DSP::oscillatorBank;

  inline UInt Clamp(UInt length) {
    return length > YSE::STANDARD_BUFFERSIZE ? YSE::STANDARD_BUFFERSIZE : length;
  }

  inline Flt Finite(Flt value) {
    return std::isfinite(value) ? value : 0.f;
  }

  // Into [0, 1); a hair below 0 rounds to 1, which is 0 again.
  inline Dbl Wrap(Dbl p) {
    p -= std::floor(p);
    return p >= 1.0 ? 0.0 : p;
  }

  // One cycle of a sine at the largest table size, shared by every bank and
  // read at a stride for the smaller ones. Built once, the first time a bank
  // is constructed (a function-local static: thread-safe), never on the audio
  // thread.
  struct sineTable {
    Flt values[oscillatorBank::MAX_TABLE_SIZE];
    sineTable() {
      const Dbl twoPi = 6.283185307179586476925286766559;
      for (UInt i = 0; i < oscillatorBank::MAX_TABLE_SIZE; ++i)
        values[i] = static_cast<Flt>(std::sin(twoPi * static_cast<Dbl>(i) /
                                              static_cast<Dbl>(oscillatorBank::MAX_TABLE_SIZE)));
    }
  };

  const Flt* Sine() {
    static const sineTable table;
    return table.values;
  }

  // The power of two nearest to `points`, the larger on a tie, within the
  // table limits.
  UInt RoundTableSize(UInt points) {
    if (points <= oscillatorBank::MIN_TABLE_SIZE) return oscillatorBank::MIN_TABLE_SIZE;
    if (points >= oscillatorBank::MAX_TABLE_SIZE) return oscillatorBank::MAX_TABLE_SIZE;
    UInt upper = oscillatorBank::MIN_TABLE_SIZE;
    while (upper < points)
      upper <<= 1;
    const UInt lower = upper >> 1;
    return points - lower < upper - points ? lower : upper;
  }
} // namespace

namespace YSE {
  namespace DSP {

    oscillatorBank::oscillatorBank(UInt capacity, bool interpolating, UInt tablePointCount)
      : oscillators(capacity < 1 ? 1 : capacity),
        active(capacity < 1 ? 1 : capacity),
        interpolate(interpolating),
        output(STANDARD_BUFFERSIZE) {
      tableSize(tablePointCount);
      // Build the shared sine here, never first on the audio thread.
      (void)Sine();
    }

    oscillatorBank& oscillatorBank::size(UInt count) {
      if (count < 1) count = 1;
      if (count > capacity()) count = capacity();
      active = count;
      return *this;
    }

    oscillatorBank& oscillatorBank::frequencySmoothing(UInt samples) {
      freqSmooth = samples;
      return *this;
    }

    oscillatorBank& oscillatorBank::magnitudeSmoothing(UInt samples) {
      magSmooth = samples;
      return *this;
    }

    oscillatorBank& oscillatorBank::frameSync(bool on) {
      sync = on;
      return *this;
    }

    oscillatorBank& oscillatorBank::tableSize(UInt points) {
      tablePoints = RoundTableSize(points);
      custom = false;
      stride = MAX_TABLE_SIZE / tablePoints;
      return *this;
    }

    oscillatorBank& oscillatorBank::setTable(const Flt* samples, UInt count) {
      if (samples == nullptr || count == 0) return tableSize(tablePoints);
      // Within the capacity of an earlier, larger table: no allocation.
      if (table.size() < tablePoints) table.resize(tablePoints);
      for (UInt i = 0; i < tablePoints; ++i)
        table[i] = i < count ? Finite(samples[i]) : 0.f;
      custom = true;
      stride = 1;
      return *this;
    }

    bool oscillatorBank::swapTable(std::vector<Flt>& samples) {
      if (samples.size() < tablePoints) return false;
      table.swap(samples);
      custom = true;
      stride = 1;
      return true;
    }

    void oscillatorBank::Retarget(ramp& r, Flt target, UInt samples) {
      target = Finite(target);
      if (target == r.target) return;
      r.target = target;
      if (samples <= 1) {
        r.value = target;
        r.left = 0;
        return;
      }
      r.step = (target - r.value) / static_cast<Flt>(samples);
      r.left = samples;
    }

    oscillatorBank& oscillatorBank::set(UInt index, Flt frequency, Flt magnitude) {
      if (index >= capacity()) return *this;
      Retarget(oscillators[index].freq, frequency, freqSmooth);
      Retarget(oscillators[index].mag, magnitude, magSmooth);
      return *this;
    }

    oscillatorBank& oscillatorBank::setFrequency(UInt index, Flt frequency) {
      if (index < capacity()) Retarget(oscillators[index].freq, frequency, freqSmooth);
      return *this;
    }

    oscillatorBank& oscillatorBank::setMagnitude(UInt index, Flt magnitude) {
      if (index < capacity()) Retarget(oscillators[index].mag, magnitude, magSmooth);
      return *this;
    }

    oscillatorBank& oscillatorBank::setPhase(UInt index, Flt value) {
      if (index < capacity()) oscillators[index].phase = Wrap(static_cast<Dbl>(Finite(value)));
      return *this;
    }

    oscillatorBank& oscillatorBank::setPairs(const Flt* pairs, UInt count) {
      if (pairs == nullptr) count = 0;
      if (count > capacity()) count = capacity();
      for (UInt i = 0; i < count; ++i)
        set(i, pairs[2 * i], pairs[(2 * i) + 1]);
      return fadeFrom(count);
    }

    oscillatorBank& oscillatorBank::fadeFrom(UInt first) {
      for (UInt i = first; i < capacity(); ++i)
        Retarget(oscillators[i].mag, 0.f, magSmooth);
      return *this;
    }

    oscillatorBank& oscillatorBank::clear() {
      for (oscillator& o : oscillators) {
        o.freq = ramp{};
        o.mag = ramp{};
        o.held = false;
        o.heldHasPhase = false;
      }
      heldCount = 0;
      return *this;
    }

    oscillatorBank& oscillatorBank::silence() {
      for (oscillator& o : oscillators)
        o.mag = ramp{};
      return *this;
    }

    oscillatorBank& oscillatorBank::reset() {
      clear();
      for (oscillator& o : oscillators)
        o.phase = 0.0;
      return *this;
    }

    Flt oscillatorBank::frequency(UInt index) const {
      return index < capacity() ? oscillators[index].freq.value : 0.f;
    }

    Flt oscillatorBank::magnitude(UInt index) const {
      return index < capacity() ? oscillators[index].mag.value : 0.f;
    }

    Flt oscillatorBank::phase(UInt index) const {
      return index < capacity() ? static_cast<Flt>(oscillators[index].phase) : 0.f;
    }

    // One sample's values for oscillator `index`, from the signals.
    void oscillatorBank::Write(UInt index, Flt frequency, Flt magnitude, const Flt* value) {
      oscillator& o = oscillators[index];
      if (!sync) {
        Retarget(o.freq, frequency, freqSmooth);
        Retarget(o.mag, magnitude, magSmooth);
        return;
      }
      if (!o.held) heldCount++;
      o.held = true;
      o.heldFreq = frequency;
      o.heldMag = magnitude;
      // A phase sent with a frame replaces one sent earlier in it; a frame
      // without one leaves the phase running.
      if (value != nullptr) {
        o.heldPhase = *value;
        o.heldHasPhase = true;
      }
    }

    // Frame sync: the held values of every oscillator take effect together.
    void oscillatorBank::Commit() {
      if (heldCount == 0) return;
      for (oscillator& o : oscillators) {
        if (!o.held) continue;
        Retarget(o.freq, o.heldFreq, freqSmooth);
        Retarget(o.mag, o.heldMag, magSmooth);
        if (o.heldHasPhase) o.phase = Wrap(static_cast<Dbl>(Finite(o.heldPhase)));
        o.held = false;
        o.heldHasPhase = false;
      }
      heldCount = 0;
    }

    // The sum of the active oscillators at this sample, then one step on.
    Flt oscillatorBank::Render(Dbl increment, const Flt* lookup) {
      const UInt mask = tablePoints - 1;
      const Dbl scale = static_cast<Dbl>(tablePoints);
      Flt sum = 0.f;
      for (UInt j = 0; j < active; ++j) {
        oscillator& o = oscillators[j];
        const Dbl x = o.phase * scale;
        const UInt i0 = static_cast<UInt>(x) & mask;
        Flt v = lookup[i0 * stride];
        if (interpolate) {
          const Flt next = lookup[((i0 + 1) & mask) * stride];
          v += static_cast<Flt>(x - std::floor(x)) * (next - v);
        }
        sum += v * o.mag.value;

        Dbl p = o.phase + (static_cast<Dbl>(o.freq.value) * increment);
        if (p >= 1.0 || p < 0.0) p = Wrap(p);
        o.phase = p;

        if (o.freq.left > 0) {
          o.freq.value += o.freq.step;
          if (--o.freq.left == 0) o.freq.value = o.freq.target;
        }
        if (o.mag.left > 0) {
          o.mag.value += o.mag.step;
          if (--o.mag.left == 0) o.mag.value = o.mag.target;
        }
      }
      return Finite(sum);
    }

    buffer& oscillatorBank::operator()(UInt length) {
      length = Clamp(length);
      // Within the capacity set in the constructor: no allocation.
      if (output.getLength() != length) output.resize(length);
      const Dbl increment = 1.0 / static_cast<Dbl>(SAMPLERATE);
      const Flt* lookup = custom ? table.data() : Sine();
      Flt* out = output.getPtr();
      for (UInt i = 0; i < length; ++i)
        out[i] = Render(increment, lookup);
      return output;
    }

    buffer& oscillatorBank::Process(buffer& frequency, buffer& magnitude, buffer* phase,
                                    buffer& index) {
      UInt length = frequency.getLength();
      if (magnitude.getLength() < length) length = magnitude.getLength();
      if (phase != nullptr && phase->getLength() < length) length = phase->getLength();
      if (index.getLength() < length) length = index.getLength();
      length = Clamp(length);
      if (output.getLength() != length) output.resize(length);

      const Dbl increment = 1.0 / static_cast<Dbl>(SAMPLERATE);
      const Flt* lookup = custom ? table.data() : Sine();
      const Flt* f = frequency.getPtr();
      const Flt* m = magnitude.getPtr();
      const Flt* p = phase != nullptr ? phase->getPtr() : nullptr;
      const Flt* n = index.getPtr();
      Flt* out = output.getPtr();
      for (UInt i = 0; i < length; ++i) {
        const Flt at = n[i];
        // Truncated toward zero, so -0.5 is oscillator 0, as a cast has it.
        if (std::isfinite(at) && at > -1.f && at < static_cast<Flt>(active)) {
          const UInt which = static_cast<UInt>(at < 0.f ? 0.f : at);
          if (sync && which == 0) Commit();
          Write(which, f[i], m[i], p != nullptr ? &p[i] : nullptr);
        }
        out[i] = Render(increment, lookup);
      }
      return output;
    }

    buffer& oscillatorBank::operator()(buffer& frequency, buffer& magnitude, buffer& phase,
                                       buffer& index) {
      return Process(frequency, magnitude, &phase, index);
    }

    buffer& oscillatorBank::operator()(buffer& frequency, buffer& magnitude, buffer& index) {
      return Process(frequency, magnitude, nullptr, index);
    }
  } // namespace DSP
} // namespace YSE
