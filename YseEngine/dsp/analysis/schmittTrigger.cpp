#include "schmittTrigger.hpp"

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

    schmittTrigger::schmittTrigger() : output(STANDARD_BUFFERSIZE) {}

    schmittTrigger& schmittTrigger::low(Flt threshold) {
      if (!std::isnan(threshold)) lowThreshold = threshold;
      return *this;
    }

    schmittTrigger& schmittTrigger::high(Flt threshold) {
      if (!std::isnan(threshold)) highThreshold = threshold;
      return *this;
    }

    schmittTrigger& schmittTrigger::set(bool on) {
      state = on;
      return *this;
    }

    schmittTrigger& schmittTrigger::reset() {
      state = false;
      return *this;
    }

    Flt schmittTrigger::Step(Flt in) {
      // A NaN input fails every comparison: the state holds.
      if (!state) {
        state = in >= highThreshold;
      } else if (lowThreshold < highThreshold) {
        state = !(in <= lowThreshold);
      } else {
        // No hysteresis (a low threshold at or above the high one is the
        // high one): 1 exactly while the input is at or above it.
        state = !(in < highThreshold);
      }
      return state ? 1.f : 0.f;
    }

    Flt schmittTrigger::process(Flt in) {
      return Step(in);
    }

    buffer& schmittTrigger::operator()(buffer& in) {
      const UInt length = Clamp(in.getLength());
      Fit(output, length);
      const Flt* x = in.getPtr();
      Flt* y = output.getPtr();
      for (UInt i = 0; i < length; ++i)
        y[i] = Step(x[i]);
      return output;
    }

    buffer& schmittTrigger::operator()(buffer& in, buffer& lowSignal, buffer& highSignal) {
      const UInt length =
          std::min({Clamp(in.getLength()), lowSignal.getLength(), highSignal.getLength()});
      Fit(output, length);
      const Flt* x = in.getPtr();
      const Flt* l = lowSignal.getPtr();
      const Flt* h = highSignal.getPtr();
      Flt* y = output.getPtr();
      for (UInt i = 0; i < length; ++i) {
        low(l[i]);
        high(h[i]);
        y[i] = Step(x[i]);
      }
      return output;
    }

  } // namespace DSP
} // namespace YSE
