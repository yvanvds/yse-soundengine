#include "signalStash.hpp"
#include "phaseStep.hpp"
#include "triggerCondition.hpp"
#include "whiteSource.hpp"

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

  // `x` wrapped into [0, n).
  inline Dbl WrapInto(Dbl x, UInt n) {
    const Dbl size = static_cast<Dbl>(n);
    Dbl w = std::fmod(x, size);
    if (w < 0.0) w += size;
    return w >= size ? 0.0 : w;
  }
} // namespace

namespace YSE {
  namespace DSP {

    signalStash::signalStash(UInt capacity)
      : output(STANDARD_BUFFERSIZE), state(detail::WHITE_DEFAULT_SEED) {
      maxSize(capacity);
    }

    signalStash& signalStash::maxSize(UInt capacity) {
      if (capacity < 1) capacity = 1;
      memory.assign(capacity, 0.f);
      urn.assign(capacity, 0);
      left = 0;
      if (length > capacity) length = capacity;
      writeAt %= length;
      readAt %= length;
      return *this;
    }

    signalStash& signalStash::size(UInt places) {
      if (places < 1) places = 1;
      if (places > maxSize()) places = maxSize();
      if (places == length) return *this;
      length = places;
      writeAt %= length;
      readAt %= length;
      left = 0;
      return *this;
    }

    signalStash& signalStash::sampleLevel(Flt level) {
      if (!std::isfinite(level)) return *this;
      sampleAt = level;
      sampleWrapped = phaseStep::Wrap(static_cast<Dbl>(level));
      return *this;
    }

    signalStash& signalStash::advanceLevel(Flt level) {
      if (!std::isfinite(level)) return *this;
      advanceAt = level;
      advanceWrapped = phaseStep::Wrap(static_cast<Dbl>(level));
      return *this;
    }

    signalStash& signalStash::sampleTriggerMode(TRIGGER_MODE m) {
      if (m >= ASCENDING && m <= PHASE) sampleHow = m;
      return *this;
    }

    signalStash& signalStash::advanceTriggerMode(TRIGGER_MODE m) {
      if (m >= ASCENDING && m <= PHASE) advanceHow = m;
      return *this;
    }

    signalStash& signalStash::direction(DIRECTION d) {
      if (d < FORWARD || d > URN || d == dir) return *this;
      dir = d;
      left = 0;
      return *this;
    }

    signalStash& signalStash::mode(MODE m) {
      if (m >= NEXT && m <= NORMALIZED) how = m;
      return *this;
    }

    signalStash& signalStash::writeMode(MODE m) {
      if (m >= NEXT && m <= NORMALIZED) writeHow = m;
      return *this;
    }

    signalStash& signalStash::range(RANGE r) {
      if (r >= ZERO && r <= KEEP) outside = r;
      return *this;
    }

    signalStash& signalStash::interpolation(INTERPOLATION i) {
      if (i >= TRUNCATE && i <= ROUND) between = i;
      return *this;
    }

    signalStash& signalStash::duration(UInt samples) {
      hold = samples;
      if (remaining > hold) remaining = hold;
      return *this;
    }

    signalStash& signalStash::seed(std::uint32_t value) {
      state = value;
      return *this;
    }

    signalStash& signalStash::load(const Flt* values, UInt count) {
      if (values == nullptr) return *this;
      UInt n = 0;
      for (UInt i = 0; i < count && n < maxSize(); ++i) {
        if (!std::isfinite(values[i])) continue;
        memory[n++] = values[i];
      }
      if (n == 0) return *this;
      length = n;
      writeAt = 0;
      readAt %= length;
      left = 0;
      return *this;
    }

    signalStash& signalStash::write(Flt value) {
      if (std::isfinite(value)) memory[writeAt] = value;
      return *this;
    }

    signalStash& signalStash::writePosition(Flt index) {
      if (!std::isfinite(index)) return *this;
      Dbl x = static_cast<Dbl>(index);
      if (writeHow == NORMALIZED) x *= static_cast<Dbl>(length);
      UInt place = 0;
      if (Place(x, place)) writeAt = place;
      return *this;
    }

    signalStash& signalStash::advanceWrite() {
      writeAt = writeAt + 1 >= length ? 0 : writeAt + 1;
      return *this;
    }

    signalStash& signalStash::advance() {
      Advance();
      remaining = hold;
      return *this;
    }

    signalStash& signalStash::capture() {
      armed = true;
      return *this;
    }

    UInt signalStash::Draw(UInt n) {
      const Dbl u = (detail::NextWhite(state) + 1.0) * 0.5;
      const UInt k = static_cast<UInt>(u * static_cast<Dbl>(n));
      return k < n ? k : n - 1;
    }

    void signalStash::Advance() {
      switch (dir) {
      case FORWARD:
        readAt = readAt + 1 >= length ? 0 : readAt + 1;
        break;
      case REVERSE:
        readAt = readAt == 0 ? length - 1 : readAt - 1;
        break;
      case RANDOM:
        readAt = Draw(length);
        break;
      case URN: {
        if (left == 0) {
          for (UInt i = 0; i < length; ++i)
            urn[i] = i;
          left = length;
        }
        const UInt j = Draw(left);
        readAt = urn[j];
        urn[j] = urn[left - 1];
        --left;
        break;
      }
      }
    }

    bool signalStash::Place(Dbl x, UInt& place) const {
      Dbl whole = between == ROUND ? std::floor(x + 0.5) : std::floor(x);
      const Dbl top = static_cast<Dbl>(length - 1);
      if (whole < 0.0 || whole > top) {
        switch (outside) {
        case ZERO:
        case KEEP:
          return false;
        case EXTEND:
          whole = whole < 0.0 ? 0.0 : top;
          break;
        case WRAP:
          whole = WrapInto(whole, length);
          break;
        }
      }
      const UInt k = static_cast<UInt>(whole);
      place = k < length ? k : length - 1;
      return true;
    }

    bool signalStash::Lookup(Dbl x, Flt& result) {
      if (between != LINEAR) {
        UInt k = 0;
        if (!Place(x, k)) return false;
        readAt = k;
        result = memory[k];
        return true;
      }
      const Dbl top = static_cast<Dbl>(length - 1);
      bool wraps = false;
      switch (outside) {
      case ZERO:
      case KEEP:
        if (x < 0.0 || x > top) return false;
        break;
      case EXTEND:
        if (x < 0.0) x = 0.0;
        if (x > top) x = top;
        break;
      case WRAP:
        x = WrapInto(x, length);
        wraps = true;
        break;
      }
      const Dbl whole = std::floor(x);
      UInt k = static_cast<UInt>(whole);
      if (k >= length) k = length - 1;
      UInt above = k + 1;
      if (above >= length) above = wraps ? 0 : length - 1;
      const Dbl f = x - whole;
      readAt = k;
      const Dbl a = static_cast<Dbl>(memory[k]);
      const Dbl b = static_cast<Dbl>(memory[above]);
      result = static_cast<Flt>(a + (b - a) * f);
      return true;
    }

    void signalStash::Record(Flt in, Flt writeIndex) {
      if (!std::isfinite(in)) return;
      if (writeHow == NEXT) {
        memory[writeAt] = in;
        writeAt = writeAt + 1 >= length ? 0 : writeAt + 1;
        return;
      }
      if (!std::isfinite(writeIndex)) return;
      Dbl x = static_cast<Dbl>(writeIndex);
      if (writeHow == NORMALIZED) x *= static_cast<Dbl>(length);
      UInt place = 0;
      if (Place(x, place)) memory[place] = in;
    }

    Flt signalStash::process(Flt in, Flt sampleTrigger, Flt advanceTrigger, Flt writeIndex) {
      bool record = armed;
      armed = false;
      if (std::isfinite(sampleTrigger)) {
        if (triggerCondition::Meets(sampleHow, lastSample, sampleTrigger, sampleAt, sampleWrapped))
          record = true;
        lastSample = sampleTrigger;
      }
      if (record) Record(in, writeIndex);

      Flt out = 0.f;
      if (how == NEXT) {
        if (std::isfinite(advanceTrigger)) {
          if (triggerCondition::Meets(advanceHow, lastAdvance, advanceTrigger, advanceAt,
                                      advanceWrapped)) {
            Advance();
            remaining = hold;
          }
          lastAdvance = advanceTrigger;
        }
        out = memory[readAt];
      } else {
        if (std::isfinite(advanceTrigger)) {
          Dbl x = static_cast<Dbl>(advanceTrigger);
          if (how == NORMALIZED) x *= static_cast<Dbl>(length);
          Flt found = 0.f;
          if (Lookup(x, found))
            held = found;
          else if (outside == ZERO)
            held = 0.f;
          lastAdvance = advanceTrigger;
        }
        out = held;
        if (out != lastRead) {
          lastRead = out;
          remaining = hold;
        }
      }

      if (hold == 0) return out;
      if (remaining == 0) return 0.f;
      --remaining;
      return out;
    }

    buffer& signalStash::operator()(buffer& in, buffer& sampleTrigger, buffer& advanceTrigger,
                                    buffer& writeIndex) {
      UInt n = Clamp(in.getLength());
      if (sampleTrigger.getLength() < n) n = sampleTrigger.getLength();
      if (advanceTrigger.getLength() < n) n = advanceTrigger.getLength();
      if (writeIndex.getLength() < n) n = writeIndex.getLength();
      Fit(output, n);
      const Flt* x = in.getPtr();
      const Flt* s = sampleTrigger.getPtr();
      const Flt* a = advanceTrigger.getPtr();
      const Flt* w = writeIndex.getPtr();
      Flt* out = output.getPtr();
      for (UInt i = 0; i < n; ++i)
        out[i] = process(x[i], s[i], a[i], w[i]);
      return output;
    }

  } // namespace DSP
} // namespace YSE
