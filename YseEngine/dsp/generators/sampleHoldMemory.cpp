#include "sampleHoldMemory.hpp"
#include "phaseStep.hpp"
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

  // The trigger rose from at or below `level` to above it.
  inline bool Rises(Flt previous, Flt current, Flt level) {
    return previous <= level && current > level;
  }

  // `x` rounded down and wrapped into 0 .. n - 1.
  inline UInt WrapIndex(Flt x, UInt n) {
    Dbl d = std::floor(static_cast<Dbl>(x));
    d = std::fmod(d, static_cast<Dbl>(n));
    if (d < 0.0) d += static_cast<Dbl>(n);
    const UInt k = static_cast<UInt>(d);
    return k < n ? k : 0;
  }
} // namespace

namespace YSE {
  namespace DSP {

    sampleHoldMemory::sampleHoldMemory(UInt capacity)
      : output(STANDARD_BUFFERSIZE), state(detail::WHITE_DEFAULT_SEED) {
      maxSize(capacity);
    }

    sampleHoldMemory& sampleHoldMemory::maxSize(UInt capacity) {
      if (capacity < 1) capacity = 1;
      memory.assign(capacity, 0.f);
      urn.assign(capacity, 0);
      left = 0;
      if (length > capacity) length = capacity;
      writeAt %= length;
      readAt %= length;
      return *this;
    }

    sampleHoldMemory& sampleHoldMemory::size(UInt places) {
      if (places < 1) places = 1;
      if (places > maxSize()) places = maxSize();
      if (places == length) return *this;
      length = places;
      writeAt %= length;
      readAt %= length;
      left = 0;
      return *this;
    }

    sampleHoldMemory& sampleHoldMemory::sampleLevel(Flt level) {
      if (std::isfinite(level)) sampleAt = level;
      return *this;
    }

    sampleHoldMemory& sampleHoldMemory::advanceLevel(Flt level) {
      if (std::isfinite(level)) advanceAt = level;
      return *this;
    }

    sampleHoldMemory& sampleHoldMemory::direction(DIRECTION d) {
      if (d < FORWARD || d > URN || d == dir) return *this;
      dir = d;
      left = 0;
      return *this;
    }

    sampleHoldMemory& sampleHoldMemory::mode(MODE m) {
      if (m >= LEVEL && m <= SUBDIVISION) how = m;
      return *this;
    }

    sampleHoldMemory& sampleHoldMemory::seed(std::uint32_t value) {
      state = value;
      return *this;
    }

    sampleHoldMemory& sampleHoldMemory::load(const Flt* values, UInt count) {
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

    UInt sampleHoldMemory::Draw(UInt n) {
      const Dbl u = (detail::NextWhite(state) + 1.0) * 0.5;
      const UInt k = static_cast<UInt>(u * static_cast<Dbl>(n));
      return k < n ? k : n - 1;
    }

    void sampleHoldMemory::Advance() {
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

    Flt sampleHoldMemory::process(Flt in, Flt sampleTrigger, Flt advanceTrigger) {
      if (std::isfinite(sampleTrigger)) {
        if (Rises(lastSample, sampleTrigger, sampleAt) && std::isfinite(in)) {
          memory[writeAt] = in;
          writeAt = writeAt + 1 >= length ? 0 : writeAt + 1;
        }
        lastSample = sampleTrigger;
      }

      if (std::isfinite(advanceTrigger)) {
        switch (how) {
        case LEVEL:
          if (Rises(lastAdvance, advanceTrigger, advanceAt)) Advance();
          break;
        case INDEX:
          readAt = WrapIndex(advanceTrigger, length);
          break;
        case SUBDIVISION: {
          const Dbl phase = phaseStep::Wrap(static_cast<Dbl>(advanceTrigger));
          const UInt k = static_cast<UInt>(phase * static_cast<Dbl>(length));
          readAt = k < length ? k : length - 1;
          break;
        }
        }
        lastAdvance = advanceTrigger;
      }

      return memory[readAt];
    }

    buffer& sampleHoldMemory::operator()(buffer& in, buffer& sampleTrigger,
                                         buffer& advanceTrigger) {
      UInt n = Clamp(in.getLength());
      if (sampleTrigger.getLength() < n) n = sampleTrigger.getLength();
      if (advanceTrigger.getLength() < n) n = advanceTrigger.getLength();
      Fit(output, n);
      const Flt* x = in.getPtr();
      const Flt* s = sampleTrigger.getPtr();
      const Flt* a = advanceTrigger.getPtr();
      Flt* out = output.getPtr();
      for (UInt i = 0; i < n; ++i)
        out[i] = process(x[i], s[i], a[i]);
      return output;
    }

  } // namespace DSP
} // namespace YSE
