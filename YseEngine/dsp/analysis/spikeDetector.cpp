#include "spikeDetector.hpp"

#include <cmath>

namespace {
  inline UInt Clamp(UInt length) {
    return length > YSE::STANDARD_BUFFERSIZE ? YSE::STANDARD_BUFFERSIZE : length;
  }

  // The live sample rate, never 0 (an engine that is not open yet).
  inline Dbl Rate() {
    return YSE::SAMPLERATE > 0 ? static_cast<Dbl>(YSE::SAMPLERATE) : 1.0;
  }
} // namespace

namespace YSE {
  namespace DSP {

    spikeDetector::spikeDetector()
      : refractoryMs(0.f),
        now(0),
        last(0),
        holdFrom(0),
        holding(false),
        latest(0.f),
        count(0),
        pos{},
        intervals{} {}

    bool spikeDetector::accept(std::uint64_t at, Dbl holdSamples, Dbl msPerSample) {
      if (holding && static_cast<Dbl>(at - holdFrom) < holdSamples) return false;
      latest = static_cast<Flt>(static_cast<Dbl>(at - last) * msPerSample);
      last = at;
      holdFrom = at;
      holding = true;
      return true;
    }

    bool spikeDetector::process(Flt in) {
      const Dbl rate = Rate();
      const std::uint64_t at = now++;
      if (edges.process(in) != 1) return false;
      return accept(at, static_cast<Dbl>(refractoryMs) * rate / 1000.0, 1000.0 / rate);
    }

    UInt spikeDetector::operator()(buffer& in) {
      const UInt length = Clamp(in.getLength());
      // The edge detector clamps to the same length.
      edges(in);
      const Dbl rate = Rate();
      const Dbl holdSamples = static_cast<Dbl>(refractoryMs) * rate / 1000.0;
      const Dbl msPerSample = 1000.0 / rate;
      UInt c = 0;
      for (UInt i = 0; i < edges.rises(); ++i) {
        const UInt index = edges.rise(i);
        if (accept(now + index, holdSamples, msPerSample)) {
          pos[c] = index;
          intervals[c] = latest;
          c++;
        }
      }
      count = c;
      now += length;
      return c;
    }

    UInt spikeDetector::spikes() const {
      return count;
    }

    UInt spikeDetector::spike(UInt i) const {
      return pos[i];
    }

    Flt spikeDetector::interval(UInt i) const {
      return intervals[i];
    }

    Flt spikeDetector::lastInterval() const {
      return latest;
    }

    spikeDetector& spikeDetector::refractory(Flt ms) {
      refractoryMs = (std::isnan(ms) || ms < 0.f) ? 0.f : ms;
      return *this;
    }

    Flt spikeDetector::refractory() const {
      return refractoryMs;
    }

    spikeDetector& spikeDetector::restart() {
      holdFrom = now;
      holding = true;
      return *this;
    }

    spikeDetector& spikeDetector::reset() {
      edges.reset();
      now = 0;
      last = 0;
      holdFrom = 0;
      holding = false;
      latest = 0.f;
      count = 0;
      return *this;
    }

  } // namespace DSP
} // namespace YSE
