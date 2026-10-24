#include "sampleCounter.hpp"

#include <cmath>

namespace {

  inline UInt Clamp(UInt length) {
    return length > YSE::STANDARD_BUFFERSIZE ? YSE::STANDARD_BUFFERSIZE : length;
  }

  // No enable signal: start and stop alone decide.
  struct noEnable {
    static constexpr bool present = false;
    bool operator()(UInt) const {
      return false;
    }
  };

  // Counting at sample i: the enable signal is a number other than 0.
  struct signalEnable {
    static constexpr bool present = true;
    const Flt* values;
    bool operator()(UInt i) const {
      const Flt v = values[i];
      return v != 0.f && !std::isnan(v);
    }
  };

} // namespace

namespace YSE {
  namespace DSP {

    sampleCounter::sampleCounter()
      : output(STANDARD_BUFFERSIZE), carryOutput(STANDARD_BUFFERSIZE) {}

    sampleCounter& sampleCounter::minimum(Int value) {
      low = value;
      return *this;
    }

    sampleCounter& sampleCounter::limit(Int value) {
      high = value;
      return *this;
    }

    sampleCounter& sampleCounter::start() {
      current = low;
      wraps = 0;
      running = true;
      return *this;
    }

    sampleCounter& sampleCounter::start(Int value) {
      low = value;
      return start();
    }

    sampleCounter& sampleCounter::stop() {
      current = low;
      wraps = 0;
      running = false;
      return *this;
    }

    template <class Enable> buffer& sampleCounter::process(Enable enable, UInt length) {
      length = Clamp(length);
      // Within the capacity set in the constructor: no allocation.
      if (output.getLength() != length) output.resize(length);
      if (carryOutput.getLength() != length) carryOutput.resize(length);
      Flt* out = output.getPtr();
      Flt* carryOut = carryOutput.getPtr();

      for (UInt i = 0; i < length; ++i) {
        if constexpr (Enable::present) {
          const bool on = enable(i);
          if (on && !running) {
            start();
          } else if (!on && running) {
            stop();
          }
        }
        if (!running) {
          out[i] = static_cast<Flt>(low);
          carryOut[i] = 0.f;
          continue;
        }
        // Checked before the output, so a count already at or past a new
        // limit wraps at its next sample.
        if (high != 0 && current >= high) {
          current = low;
          ++wraps;
        }
        out[i] = static_cast<Flt>(current);
        carryOut[i] = static_cast<Flt>(wraps);
        ++current;
      }
      return output;
    }

    buffer& sampleCounter::operator()(UInt length) {
      return process(noEnable{}, length);
    }

    buffer& sampleCounter::operator()(buffer& enable) {
      return process(signalEnable{enable.getPtr()}, enable.getLength());
    }

  } // namespace DSP
} // namespace YSE
