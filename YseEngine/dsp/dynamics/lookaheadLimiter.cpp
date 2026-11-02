#include "lookaheadLimiter.hpp"
#include "../smoother.hpp"

#include <algorithm>
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

  inline Flt DbToLin(Flt dB) {
    return std::pow(10.f, dB / 20.f);
  }

  constexpr Flt TWO_PI = 6.28318530717958647692f;
} // namespace

namespace YSE {
  namespace DSP {

    lookaheadLimiter::lookaheadLimiter(UInt channels, UInt maxLookahead)
      : channelCount(std::clamp<UInt>(channels, 1, MAX_CHANNELS)),
        capacity(std::min<UInt>(maxLookahead, MAX_LOOKAHEAD) + 1),
        delay(std::min<UInt>(DEFAULT_LOOKAHEAD, capacity - 1)),
        lines(static_cast<std::size_t>(channelCount) * capacity, 0.f),
        dcIn(channelCount, 0.f),
        dcOut(channelCount, 0.f),
        frameIn(channelCount, 0.f),
        frameOut(channelCount, 0.f),
        gains(capacity, 1.f),
        minValue(capacity, 1.f),
        minIndex(capacity, 0),
        outputs(channelCount, buffer(STANDARD_BUFFERSIZE)) {
      clear();
    }

    lookaheadLimiter& lookaheadLimiter::lookahead(UInt samples) {
      const UInt value = std::min(samples, capacity - 1);
      if (value != delay) {
        delay = value;
        clear();
      }
      return *this;
    }

    lookaheadLimiter& lookaheadLimiter::threshold(Flt dB) {
      if (std::isfinite(dB) && dB != thresholdDb) {
        thresholdDb = dB;
        dirty = true;
      }
      return *this;
    }

    lookaheadLimiter& lookaheadLimiter::release(Flt ms) {
      if (!std::isfinite(ms)) return *this;
      const Flt value = std::clamp(ms, 0.f, MAX_RELEASE);
      if (value != releaseMs) {
        releaseMs = value;
        dirty = true;
      }
      return *this;
    }

    lookaheadLimiter& lookaheadLimiter::preamp(Flt dB) {
      if (std::isfinite(dB) && dB != preampDb) {
        preampDb = dB;
        dirty = true;
      }
      return *this;
    }

    lookaheadLimiter& lookaheadLimiter::postamp(Flt dB) {
      if (std::isfinite(dB) && dB != postampDb) {
        postampDb = dB;
        dirty = true;
      }
      return *this;
    }

    lookaheadLimiter& lookaheadLimiter::mode(MODE value) {
      if ((value == LINEAR || value == EXPONENTIAL) && value != releaseMode) {
        releaseMode = value;
        dirty = true;
      }
      return *this;
    }

    lookaheadLimiter& lookaheadLimiter::dcBlock(bool on) {
      dcOn = on;
      return *this;
    }

    lookaheadLimiter& lookaheadLimiter::bypass(bool on) {
      bypassOn = on;
      return *this;
    }

    lookaheadLimiter& lookaheadLimiter::clear() {
      std::fill(lines.begin(), lines.end(), 0.f);
      std::fill(dcIn.begin(), dcIn.end(), 0.f);
      std::fill(dcOut.begin(), dcOut.end(), 0.f);
      std::fill(gains.begin(), gains.end(), 1.f);
      head = 0;
      tail = 0;
      frame = 0;
      writePos = 0;
      gainSum = static_cast<double>(delay) + 1.0;
      released = 1.f;
      currentGain = 1.f;
      lowestGain = 1.f;
      return *this;
    }

    Flt lookaheadLimiter::gainReduction() const {
      return lowestGain >= 1.f ? 0.f : 20.f * std::log10(std::max(lowestGain, 1e-12f));
    }

    void lookaheadLimiter::Prepare() {
      const Flt sr = static_cast<Flt>(SAMPLERATE > 0 ? SAMPLERATE : 1);
      thresholdLin = DbToLin(thresholdDb);
      preampLin = DbToLin(preampDb);
      postampLin = DbToLin(postampDb);
      const Flt releaseSamples = releaseMs * 0.001f * sr;
      if (releaseMode == LINEAR) {
        releaseStep = releaseSamples < 1.f ? 1.f : 1.f / releaseSamples;
      } else {
        releaseStep = releaseSamples < 1.f ? 1.f : onePoleCoef(releaseMs * 0.001f, sr);
      }
      dcPole = std::exp(-TWO_PI * DC_BLOCK_HZ / sr);
      builtRate = SAMPLERATE;
      dirty = false;
    }

    Flt lookaheadLimiter::Step(const Flt* in, Flt* out) {
      // 1. Condition each channel and find the linked level.
      Flt level = 0.f;
      for (UInt c = 0; c < channelCount; ++c) {
        Flt x = in == nullptr ? 0.f : in[c];
        if (!std::isfinite(x)) x = 0.f;
        Flt conditioned = x;
        if (dcOn) {
          conditioned = x - dcIn[c] + dcPole * dcOut[c];
          dcIn[c] = x;
          dcOut[c] = conditioned;
        }
        conditioned *= preampLin;
        // Bypass delays the raw input; otherwise the conditioned one.
        lines[static_cast<std::size_t>(c) * capacity + writePos] = bypassOn ? x : conditioned;
        level = std::max(level, std::fabs(conditioned));
      }

      // 2. The needed gain, and its minimum over the last delay + 1 frames.
      const Flt needed = level > thresholdLin ? thresholdLin / level : 1.f;
      while (head != tail && minIndex[head % capacity] + delay < frame)
        ++head;
      while (head != tail && minValue[(tail - 1) % capacity] >= needed)
        --tail;
      minValue[tail % capacity] = needed;
      minIndex[tail % capacity] = frame;
      ++tail;
      const Flt target = minValue[head % capacity];

      // 3. Down at once, up over the release.
      if (target < released) {
        released = target;
      } else if (releaseMode == LINEAR) {
        released = std::min(target, released + releaseStep);
      } else {
        const Flt next = onePoleSmooth(released, target, releaseStep);
        // Near the target a float step rounds to nothing; finish there.
        released = next == released ? target : next;
      }

      // 4. Averaged over the last delay + 1 frames: a ramp as long as the
      // lookahead.
      const UInt leaving = (writePos + capacity - (delay + 1)) % capacity;
      gainSum += static_cast<double>(released) - static_cast<double>(gains[leaving]);
      gains[writePos] = released;
      const Flt g = static_cast<Flt>(gainSum / (static_cast<double>(delay) + 1.0));

      // 5. The delayed frame.
      const UInt readPos = (writePos + capacity - delay) % capacity;
      for (UInt c = 0; c < channelCount; ++c) {
        const Flt delayed = lines[static_cast<std::size_t>(c) * capacity + readPos];
        Flt y = delayed;
        if (!bypassOn) {
          y = std::clamp(delayed * g, -thresholdLin, thresholdLin) * postampLin;
        }
        if (out != nullptr) out[c] = y;
      }

      ++frame;
      ++writePos;
      if (writePos == capacity) {
        writePos = 0;
        // Once per lap, the sum from scratch, so rounding cannot build up.
        double sum = 0.0;
        for (UInt k = 0; k <= delay; ++k)
          sum += gains[(capacity - 1 - k) % capacity];
        gainSum = sum;
      }

      currentGain = g;
      return g;
    }

    void lookaheadLimiter::process(const Flt* in, Flt* out) {
      if (dirty || builtRate != SAMPLERATE) Prepare();
      lowestGain = std::min(1.f, Step(in, out));
    }

    Flt lookaheadLimiter::process(Flt in) {
      // Only channel 0 is fed; the others read 0.
      std::fill(frameIn.begin(), frameIn.end(), 0.f);
      frameIn[0] = in;
      process(frameIn.data(), frameOut.data());
      return frameOut[0];
    }

    buffer& lookaheadLimiter::operator()(buffer& in) {
      buffer* list[1] = {&in};
      return (*this)(list, 1);
    }

    buffer& lookaheadLimiter::operator()(buffer* const* in, UInt count) {
      if (dirty || builtRate != SAMPLERATE) Prepare();
      const UInt used = std::min(count, channelCount);
      UInt size = STANDARD_BUFFERSIZE;
      for (UInt c = 0; c < used; ++c)
        size = std::min(size, Clamp(in[c]->getLength()));
      if (used == 0) size = 0;
      for (buffer& b : outputs)
        Fit(b, size);

      Flt lowest = 1.f;
      for (UInt i = 0; i < size; ++i) {
        for (UInt c = 0; c < channelCount; ++c)
          frameIn[c] = c < used ? in[c]->getPtr()[i] : 0.f;
        Step(frameIn.data(), frameOut.data());
        for (UInt c = 0; c < channelCount; ++c)
          outputs[c].getPtr()[i] = frameOut[c];
        lowest = std::min(lowest, currentGain);
      }
      lowestGain = lowest;
      return outputs[0];
    }

    buffer& lookaheadLimiter::output(UInt channel) {
      return outputs[std::min(channel, channelCount - 1)];
    }

  } // namespace DSP
} // namespace YSE
