#include "frameDelta.hpp"

#include <algorithm>
#include <cmath>
#include "../operators/polar.hpp"

namespace {
  using YSE::DSP::buffer;
  using YSE::DSP::stftFrame;

  inline UInt Clamp(UInt length) {
    return length > YSE::STANDARD_BUFFERSIZE ? YSE::STANDARD_BUFFERSIZE : length;
  }

  // Within the capacity set in the constructor: no allocation.
  inline void Fit(buffer& b, UInt length) {
    if (b.getLength() != length) b.resize(length);
  }

  inline UInt Capacity(UInt bins) {
    if (bins < 1) return 1;
    return bins > YSE::DSP::frameDelta::MAXIMUM_CAPACITY ? YSE::DSP::frameDelta::MAXIMUM_CAPACITY
                                                         : bins;
  }

  inline Flt Finite(Flt value) {
    return std::isfinite(value) ? value : 0.f;
  }

  // A frame of another size than the last: the stored bins belong to
  // another spectrum, so they start again at 0.
  inline void Follow(std::vector<Flt>& state, UInt& frameBins, UInt bins) {
    if (bins == frameBins) return;
    frameBins = bins;
    std::fill(state.begin(), state.end(), 0.f);
  }

  // How many of the run's bins the state holds; the rest put out 0.
  inline UInt Kept(const std::vector<Flt>& state, UInt firstBin, UInt count) {
    const auto capacity = static_cast<UInt>(state.size());
    if (firstBin >= capacity) return 0;
    return std::min(count, capacity - firstBin);
  }

  // The run of a block: `frame` over at most the block, the output as long
  // as the block with samples past the run at 0.
  template <class F>
  buffer& Block(buffer& output, const stftFrame& frame, buffer& in, F&& process) {
    const UInt length = Clamp(in.getLength());
    Fit(output, length);
    stftFrame run = frame;
    run.count = std::min(frame.count, length);
    process(run, in.getPtr(), output.getPtr());
    std::fill(output.getPtr() + run.count, output.getPtr() + length, 0.f);
    return output;
  }

  stftFrame WholeBlock(const buffer& in) {
    stftFrame frame;
    frame.count = Clamp(in.getLength());
    frame.bins = frame.count;
    return frame;
  }
} // namespace

namespace YSE {
  namespace DSP {

    // ---- frameDelta --------------------------------------------------------

    frameDelta::frameDelta(UInt capacity) : output(STANDARD_BUFFERSIZE), last(Capacity(capacity)) {}

    frameDelta& frameDelta::clear() {
      std::fill(last.begin(), last.end(), 0.f);
      return *this;
    }

    void frameDelta::operator()(const stftFrame& frame, const Flt* in, Flt* out) {
      Follow(last, frameBins, frame.bins);
      const UInt kept = Kept(last, frame.firstBin, frame.count);
      Flt* state = last.data() + (kept > 0 ? frame.firstBin : 0);
      for (UInt i = 0; i < kept; ++i) {
        const Flt x = Finite(in[i]);
        out[i] = x - state[i];
        state[i] = x;
      }
      std::fill(out + kept, out + frame.count, 0.f);
    }

    buffer& frameDelta::operator()(const stftFrame& frame, buffer& in) {
      return Block(output, frame, in,
                   [this](const stftFrame& run, const Flt* a, Flt* b) { (*this)(run, a, b); });
    }

    buffer& frameDelta::operator()(buffer& in) {
      return (*this)(WholeBlock(in), in);
    }

    // ---- frameAccumulate ---------------------------------------------------

    frameAccumulate::frameAccumulate(UInt capacity, bool wrapPhase)
      : output(STANDARD_BUFFERSIZE), sum(Capacity(capacity)), wrapping(wrapPhase) {}

    frameAccumulate& frameAccumulate::clear() {
      std::fill(sum.begin(), sum.end(), 0.f);
      return *this;
    }

    void frameAccumulate::operator()(const stftFrame& frame, const Flt* in, Flt* out) {
      Follow(sum, frameBins, frame.bins);
      const UInt kept = Kept(sum, frame.firstBin, frame.count);
      Flt* state = sum.data() + (kept > 0 ? frame.firstBin : 0);
      if (wrapping) {
        for (UInt i = 0; i < kept; ++i) {
          const Flt s = phaseWrap::process(state[i] + Finite(in[i]));
          state[i] = s;
          out[i] = s;
        }
      } else {
        for (UInt i = 0; i < kept; ++i) {
          const Flt s = state[i] + Finite(in[i]);
          state[i] = s;
          out[i] = s;
        }
      }
      std::fill(out + kept, out + frame.count, 0.f);
    }

    buffer& frameAccumulate::operator()(const stftFrame& frame, buffer& in) {
      return Block(output, frame, in,
                   [this](const stftFrame& run, const Flt* a, Flt* b) { (*this)(run, a, b); });
    }

    buffer& frameAccumulate::operator()(buffer& in) {
      return (*this)(WholeBlock(in), in);
    }

  } // namespace DSP
} // namespace YSE
