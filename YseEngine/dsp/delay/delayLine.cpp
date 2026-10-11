#include "delayLine.hpp"

#include <algorithm>
#include <cmath>

namespace {
  // Room past the longest delay: one block a write may lay down before a tap
  // reads (taps run before or after it), another so a tap's oldest read is
  // never this block's write, and the interpolator's neighbours.
  constexpr UInt GUARD = 2 * YSE::STANDARD_BUFFERSIZE + 8;

  inline UInt Clamp(UInt length) {
    return length > YSE::STANDARD_BUFFERSIZE ? YSE::STANDARD_BUFFERSIZE : length;
  }
} // namespace

namespace YSE {
  namespace DSP {

    delayLine::delayLine(Flt maximumDelayMs, UInt highestSampleRate)
      : size(0), writePos(0), origin(0), longestMs(0.f), capacityMs(0.f), isFrozen(false) {
      if (maximumDelayMs < 0.f) maximumDelayMs = 0.f;
      if (highestSampleRate == 0) highestSampleRate = HIGHEST_SAMPLERATE;
      const auto samples = static_cast<UInt>(
          std::ceil(maximumDelayMs * 0.001 * static_cast<Dbl>(highestSampleRate)));
      size = samples + GUARD;
      line.assign(size, 0.f);
      capacityMs = static_cast<Flt>(static_cast<Dbl>(samples) * 1000.0 /
                                    static_cast<Dbl>(highestSampleRate));
      longestMs = maximumDelayMs;
    }

    delayLine& delayLine::maximumDelay(Flt ms) {
      longestMs = std::min(std::max(ms, 0.f), capacityMs);
      return clear();
    }

    Flt delayLine::maximumDelay() const {
      return longestMs;
    }

    Flt delayLine::capacity() const {
      return capacityMs;
    }

    delayLine& delayLine::beginBlock() {
      origin = writePos;
      return *this;
    }

    UInt delayLine::longestDelay(UInt length) const {
      const auto wanted = static_cast<UInt>(
          std::lround(static_cast<Dbl>(longestMs) * 0.001 * static_cast<Dbl>(SAMPLERATE)));
      const UInt room = size - length - 4;
      return std::max(std::min(wanted, room), length + 2);
    }

    delayLine& delayLine::write(buffer& in) {
      const UInt length = Clamp(in.getLength());
      const Flt* samples = in.getPtr();
      if (isFrozen) {
        // Loop the last maximumDelay of audio: each new sample is the one that
        // many samples back, so the taps replay it with that period.
        const UInt period = longestDelay(length);
        for (UInt i = 0; i < length; ++i) {
          const UInt from = (writePos + size - period) % size;
          line[writePos] = line[from];
          writePos = (writePos + 1) % size;
        }
        return *this;
      }
      for (UInt i = 0; i < length; ++i) {
        line[writePos] = samples[i];
        writePos = (writePos + 1) % size;
      }
      return *this;
    }

    delayLine& delayLine::clear() {
      std::fill(line.begin(), line.end(), 0.f);
      return *this;
    }

    delayLine& delayLine::freeze(bool on) {
      isFrozen = on;
      return *this;
    }

    bool delayLine::frozen() const {
      return isFrozen;
    }

  } // namespace DSP
} // namespace YSE
