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
} // namespace

namespace YSE {
  namespace DSP {

    allpass::allpass(Flt maximumDelayMs, UInt highestSampleRate)
      : output(STANDARD_BUFFERSIZE),
        history(maximumDelayMs, highestSampleRate, DEFAULT_MAXIMUM_DELAY, MAX_MAXIMUM_DELAY) {}

    allpass& allpass::delay(Flt ms) {
      if (std::isfinite(ms)) delayMs = ms;
      return *this;
    }

    allpass& allpass::gain(Flt value) {
      if (std::isfinite(value)) g = std::clamp(value, -1.f, 1.f);
      return *this;
    }

    allpass& allpass::clear() {
      history.Clear();
      return *this;
    }

    Flt allpass::capacity() const {
      return history.Capacity();
    }

    Flt allpass::delaySamples() const {
      return history.SamplesNow(delayMs);
    }

    Flt allpass::Step(Flt in, Flt samples) {
      const Flt x = std::isfinite(in) ? in : 0.f;
      Flt y = 0.f;
      if (g == 1.f || g == -1.f) {
        // The poles sit on the zeros: H(z) = -g exactly.
        y = -g * x;
      } else {
        y = -g * x + history.ReadX(samples) + g * history.ReadY(samples);
        if (!std::isfinite(y)) y = 0.f;
      }
      history.Write(x, y);
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
      const Flt perMs = detail::delayHistory::PerMs();
      const Flt longest = history.Longest(perMs);
      const Flt* x = in.getPtr();
      const Flt* d = delaySignal.getPtr();
      const Flt* s = gainSignal.getPtr();
      Flt* y = output.getPtr();
      for (UInt i = 0; i < length; ++i) {
        delay(d[i]);
        gain(s[i]);
        y[i] = Step(x[i], detail::delayHistory::Samples(delayMs, perMs, longest));
      }
      return output;
    }

  } // namespace DSP
} // namespace YSE
