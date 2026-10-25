#include "comb.hpp"

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

    comb::comb(Flt maximumDelayMs, UInt highestSampleRate)
      : output(STANDARD_BUFFERSIZE),
        history(maximumDelayMs, highestSampleRate, DEFAULT_MAXIMUM_DELAY, MAX_MAXIMUM_DELAY) {}

    comb& comb::delay(Flt ms) {
      return feedforwardDelay(ms).feedbackDelay(ms);
    }

    comb& comb::feedforwardDelay(Flt ms) {
      if (std::isfinite(ms)) ffMs = ms;
      return *this;
    }

    comb& comb::feedbackDelay(Flt ms) {
      if (std::isfinite(ms)) fbMs = ms;
      return *this;
    }

    comb& comb::gain(Flt value) {
      if (std::isfinite(value)) a = value;
      return *this;
    }

    comb& comb::feedforward(Flt value) {
      if (std::isfinite(value)) b = value;
      return *this;
    }

    comb& comb::feedback(Flt value) {
      if (std::isfinite(value)) c = std::clamp(value, -1.f, 1.f);
      return *this;
    }

    comb& comb::clear() {
      history.Clear();
      return *this;
    }

    Flt comb::capacity() const {
      return history.Capacity();
    }

    Flt comb::feedforwardSamples() const {
      return history.SamplesNow(ffMs);
    }

    Flt comb::feedbackSamples() const {
      return history.SamplesNow(fbMs);
    }

    Flt comb::Step(Flt in, Flt ffSamples, Flt fbSamples) {
      const Flt x = std::isfinite(in) ? in : 0.f;
      Flt y = a * x + b * history.ReadX(ffSamples) + c * history.ReadY(fbSamples);
      if (!std::isfinite(y)) y = 0.f;
      history.Write(x, y);
      return y;
    }

    Flt comb::process(Flt in) {
      return Step(in, feedforwardSamples(), feedbackSamples());
    }

    buffer& comb::operator()(buffer& in) {
      const UInt length = Clamp(in.getLength());
      Fit(output, length);
      const Flt ff = feedforwardSamples();
      const Flt fb = feedbackSamples();
      const Flt* x = in.getPtr();
      Flt* y = output.getPtr();
      for (UInt i = 0; i < length; ++i)
        y[i] = Step(x[i], ff, fb);
      return output;
    }

    buffer& comb::operator()(buffer& in, buffer& delaySignal, buffer& gainSignal,
                             buffer& feedforwardSignal, buffer& feedbackSignal) {
      return (*this)(in, delaySignal, delaySignal, gainSignal, feedforwardSignal, feedbackSignal);
    }

    buffer& comb::operator()(buffer& in, buffer& feedforwardDelaySignal,
                             buffer& feedbackDelaySignal, buffer& gainSignal,
                             buffer& feedforwardSignal, buffer& feedbackSignal) {
      const UInt length = std::min({Clamp(in.getLength()), feedforwardDelaySignal.getLength(),
                                    feedbackDelaySignal.getLength(), gainSignal.getLength(),
                                    feedforwardSignal.getLength(), feedbackSignal.getLength()});
      Fit(output, length);
      const Flt perMs = detail::delayHistory::PerMs();
      const Flt longest = history.Longest(perMs);
      const Flt* x = in.getPtr();
      const Flt* df = feedforwardDelaySignal.getPtr();
      const Flt* db = feedbackDelaySignal.getPtr();
      const Flt* ga = gainSignal.getPtr();
      const Flt* gb = feedforwardSignal.getPtr();
      const Flt* gc = feedbackSignal.getPtr();
      Flt* y = output.getPtr();
      for (UInt i = 0; i < length; ++i) {
        feedforwardDelay(df[i]);
        feedbackDelay(db[i]);
        gain(ga[i]);
        feedforward(gb[i]);
        feedback(gc[i]);
        y[i] = Step(x[i], detail::delayHistory::Samples(ffMs, perMs, longest),
                    detail::delayHistory::Samples(fbMs, perMs, longest));
      }
      return output;
    }

  } // namespace DSP
} // namespace YSE
