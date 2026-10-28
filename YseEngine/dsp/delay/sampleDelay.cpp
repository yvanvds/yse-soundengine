#include "sampleDelay.hpp"

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

  // Samples for `ms` at the live sample rate, unrounded.
  inline Dbl MsToSamples(Flt ms) {
    return static_cast<Dbl>(ms) * static_cast<Dbl>(YSE::SAMPLERATE) / 1000.0;
  }
} // namespace

namespace YSE {
  namespace DSP {

    sampleDelay::sampleDelay(UInt memorySamples)
      : output(STANDARD_BUFFERSIZE),
        memorySize(std::min(memorySamples, MAX_MEMORY)),
        maxDelay(memorySize) {
      // Two past the memory: the write position itself, and the neighbour an
      // interpolated read at the longest delay needs.
      line.assign(static_cast<std::size_t>(memorySize) + 2, 0.f);
    }

    sampleDelay& sampleDelay::maximumDelay(UInt samples) {
      maxDelay = std::min(samples, memorySize);
      clear();
      // The line is silent, so the new delay can start at once.
      fadeLength = 0;
      fadeDone = 0;
      current = delaySamples();
      previous = current;
      return *this;
    }

    sampleDelay& sampleDelay::delay(Flt samples) {
      if (!std::isfinite(samples)) return *this;
      timeValue = samples;
      timeInMs = false;
      return *this;
    }

    sampleDelay& sampleDelay::delayMs(Flt ms) {
      if (!std::isfinite(ms)) return *this;
      timeValue = ms;
      timeInMs = true;
      return *this;
    }

    UInt sampleDelay::delaySamples() const {
      const Dbl samples =
          timeInMs ? std::round(MsToSamples(timeValue)) : std::trunc(static_cast<Dbl>(timeValue));
      if (!(samples > 0.0)) return 0;
      if (samples >= static_cast<Dbl>(maxDelay)) return maxDelay;
      return static_cast<UInt>(samples);
    }

    sampleDelay& sampleDelay::ramp(Flt ms) {
      if (std::isfinite(ms) && ms >= 0.f) rampMs = ms;
      return *this;
    }

    sampleDelay& sampleDelay::clear() {
      std::fill(line.begin(), line.end(), 0.f);
      return *this;
    }

    void sampleDelay::Follow() {
      const UInt target = delaySamples();
      if (target == current) return;
      const Dbl samples = std::round(MsToSamples(rampMs));
      if (!(samples >= 1.0)) {
        current = target;
        previous = target;
        fadeLength = 0;
        fadeDone = 0;
        return;
      }
      // From the delay fading in (or playing), so a change during a
      // crossfade starts from where that one was heading.
      previous = current;
      current = target;
      fadeLength = samples > static_cast<Dbl>(MAX_MEMORY) ? MAX_MEMORY : static_cast<UInt>(samples);
      fadeDone = 0;
    }

    void sampleDelay::Write(Flt in) {
      if (++writePos == line.size()) writePos = 0;
      line[writePos] = std::isfinite(in) ? in : 0.f;
    }

    Flt sampleDelay::Tap(UInt back) const {
      // back <= memorySize + 1 < line.size().
      const std::size_t b = back;
      return line[writePos >= b ? writePos - b : writePos + line.size() - b];
    }

    Flt sampleDelay::Step(Flt in) {
      Write(in);
      if (fadeLength == 0) return Tap(current);
      ++fadeDone;
      const Flt g = static_cast<Flt>(fadeDone) / static_cast<Flt>(fadeLength);
      const Flt y = Tap(previous) + g * (Tap(current) - Tap(previous));
      if (fadeDone >= fadeLength) {
        fadeLength = 0;
        fadeDone = 0;
        previous = current;
      }
      return y;
    }

    Flt sampleDelay::process(Flt in) {
      Follow();
      return Step(in);
    }

    buffer& sampleDelay::operator()(buffer& in) {
      const UInt length = Clamp(in.getLength());
      Fit(output, length);
      Follow();
      const Flt* x = in.getPtr();
      Flt* y = output.getPtr();
      for (UInt i = 0; i < length; ++i)
        y[i] = Step(x[i]);
      return output;
    }

    buffer& sampleDelay::operator()(buffer& in, buffer& delaySignal) {
      const UInt length = std::min(Clamp(in.getLength()), delaySignal.getLength());
      Fit(output, length);
      const Flt longest = static_cast<Flt>(maxDelay);
      const Flt* x = in.getPtr();
      const Flt* d = delaySignal.getPtr();
      Flt* y = output.getPtr();
      for (UInt i = 0; i < length; ++i) {
        Write(x[i]);
        const Flt back = std::isfinite(d[i]) ? std::clamp(d[i], 0.f, longest) : 0.f;
        const auto whole = static_cast<UInt>(back);
        const Flt frac = back - static_cast<Flt>(whole);
        const Flt a = Tap(whole);
        y[i] = frac == 0.f ? a : a + frac * (Tap(whole + 1) - a);
      }
      return output;
    }

  } // namespace DSP
} // namespace YSE
