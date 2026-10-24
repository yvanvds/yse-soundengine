#include "allpass.hpp"

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

  inline Flt PerMs() {
    // Divided rather than multiplied by 0.001f, so whole-sample delays at the
    // usual rates come out whole.
    return static_cast<Flt>(YSE::SAMPLERATE) / 1000.f;
  }
} // namespace

namespace YSE {
  namespace DSP {

    allpass::allpass(Flt maximumDelayMs, UInt highestSampleRate) : output(STANDARD_BUFFERSIZE) {
      maximumMs = std::isfinite(maximumDelayMs) ? std::clamp(maximumDelayMs, 0.f, MAX_MAXIMUM_DELAY)
                                                : DEFAULT_MAXIMUM_DELAY;
      const Dbl samples =
          std::ceil(static_cast<Dbl>(maximumMs) * static_cast<Dbl>(highestSampleRate) * 0.001);
      // At least one sample: the shortest delay the recursion can read.
      lineSamples = std::max<UInt>(1, static_cast<UInt>(samples));
      xLine.assign(static_cast<std::size_t>(lineSamples) + 2, 0.f);
      yLine.assign(static_cast<std::size_t>(lineSamples) + 2, 0.f);
    }

    allpass& allpass::delay(Flt ms) {
      if (std::isfinite(ms)) delayMs = ms;
      return *this;
    }

    allpass& allpass::gain(Flt value) {
      if (std::isfinite(value)) g = std::clamp(value, -1.f, 1.f);
      return *this;
    }

    allpass& allpass::clear() {
      std::fill(xLine.begin(), xLine.end(), 0.f);
      std::fill(yLine.begin(), yLine.end(), 0.f);
      return *this;
    }

    Flt allpass::Longest(Flt perMs) const {
      return std::min(maximumMs * perMs, static_cast<Flt>(lineSamples));
    }

    Flt allpass::Samples(Flt ms, Flt perMs, Flt longest) {
      return std::clamp(ms * perMs, 1.f, std::max(1.f, longest));
    }

    Flt allpass::capacity() const {
      const Flt perMs = PerMs();
      if (!(perMs > 0.f)) return 0.f;
      return Longest(perMs) / perMs;
    }

    Flt allpass::delaySamples() const {
      const Flt perMs = PerMs();
      return Samples(delayMs, perMs, Longest(perMs));
    }

    Flt allpass::Read(const std::vector<Flt>& line, Flt back) const {
      // 1 <= back <= lineSamples, so both taps are older than the write
      // position and inside the line.
      const auto whole = static_cast<std::size_t>(back);
      const Flt frac = back - static_cast<Flt>(whole);
      const std::size_t size = line.size();
      const std::size_t a = writePos >= whole ? writePos - whole : writePos + size - whole;
      const std::size_t b = a == 0 ? size - 1 : a - 1;
      return line[a] + frac * (line[b] - line[a]);
    }

    Flt allpass::Step(Flt in, Flt samples) {
      const Flt x = std::isfinite(in) ? in : 0.f;
      Flt y = 0.f;
      if (g == 1.f || g == -1.f) {
        // The poles sit on the zeros: H(z) = -g exactly.
        y = -g * x;
      } else {
        y = -g * x + Read(xLine, samples) + g * Read(yLine, samples);
        if (!std::isfinite(y)) y = 0.f;
      }
      xLine[writePos] = x;
      yLine[writePos] = y;
      if (++writePos == xLine.size()) writePos = 0;
      return y;
    }

    Flt allpass::process(Flt in) {
      return Step(in, delaySamples());
    }

    buffer& allpass::operator()(buffer& in) {
      const UInt length = Clamp(in.getLength());
      Fit(output, length);
      const Flt samples = delaySamples();
      const Flt* x = in.getPtr();
      Flt* y = output.getPtr();
      for (UInt i = 0; i < length; ++i)
        y[i] = Step(x[i], samples);
      return output;
    }

    buffer& allpass::operator()(buffer& in, buffer& delaySignal, buffer& gainSignal) {
      const UInt length =
          std::min({Clamp(in.getLength()), delaySignal.getLength(), gainSignal.getLength()});
      Fit(output, length);
      const Flt perMs = PerMs();
      const Flt longest = Longest(perMs);
      const Flt* x = in.getPtr();
      const Flt* d = delaySignal.getPtr();
      const Flt* s = gainSignal.getPtr();
      Flt* y = output.getPtr();
      for (UInt i = 0; i < length; ++i) {
        delay(d[i]);
        gain(s[i]);
        y[i] = Step(x[i], Samples(delayMs, perMs, longest));
      }
      return output;
    }

  } // namespace DSP
} // namespace YSE
