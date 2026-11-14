#include "frameAverage.hpp"

#include <algorithm>
#include <cmath>

namespace {
  using YSE::DSP::buffer;
  using YSE::DSP::frameAverage;
  using YSE::DSP::frameSmooth;
  using YSE::DSP::stftFrame;

  constexpr UInt RING_MASK = frameSmooth::MAXIMUM_SMOOTHNESS - 1;
  static_assert((frameSmooth::MAXIMUM_SMOOTHNESS & RING_MASK) == 0,
                "the window ring is indexed with a mask");

  inline UInt Clamp(UInt length) {
    return length > YSE::STANDARD_BUFFERSIZE ? YSE::STANDARD_BUFFERSIZE : length;
  }

  inline UInt Between(UInt value, UInt lowest, UInt highest) {
    return value < lowest ? lowest : (value > highest ? highest : value);
  }

  // Within the capacity set in the constructor: no allocation.
  inline void Fit(buffer& b, UInt length) {
    if (b.getLength() != length) b.resize(length);
  }

  inline Flt Finite(Flt value) {
    return std::isfinite(value) ? value : 0.f;
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

  // A block of a stream of frames: cut where a frame ends, each piece a run
  // of the frame it belongs to, sample i of a frame being bin i. Frame size
  // 0: the block is a frame.
  template <class F>
  buffer& Stream(buffer& output, frameAverage::streamPlace& place, buffer& in, F&& process) {
    const UInt length = Clamp(in.getLength());
    Fit(output, length);
    const Flt* from = in.getPtr();
    Flt* to = output.getPtr();
    stftFrame run;
    if (place.size == 0) {
      run.count = length;
      run.bins = length;
      run.index = place.frame++;
      process(run, from, to);
      return output;
    }
    UInt done = 0;
    while (done < length) {
      const UInt piece = std::min(length - done, place.size - place.position);
      run.firstBin = place.position;
      run.count = piece;
      run.bins = place.size;
      run.index = place.frame;
      process(run, from + done, to + done);
      done += piece;
      place.position += piece;
      if (place.position == place.size) {
        place.position = 0;
        place.frame++;
      }
    }
    return output;
  }

  void Restart(frameAverage::streamPlace& place, UInt samples) {
    place.size = std::min(samples, frameAverage::MAXIMUM_FRAME_SIZE);
    place.position = 0;
    place.frame++;
  }
} // namespace

namespace YSE {
  namespace DSP {

    // ---- frameAverage ------------------------------------------------------

    frameAverage::frameAverage(UInt maxFrames, UInt capacity)
      : output(STANDARD_BUFFERSIZE),
        sums(Between(capacity, 1, MAXIMUM_CAPACITY), 0.0),
        slots(Between(maxFrames, 1, MAXIMUM_FRAMES)),
        frames(slots) {
      history.assign(static_cast<std::size_t>(slots) * sums.size(), 0.f);
    }

    frameAverage& frameAverage::frameCount(UInt count) {
      const UInt next = Between(count, 1, slots);
      if (next != frames) {
        frames = next;
        clear();
      }
      return *this;
    }

    frameAverage& frameAverage::frameSize(UInt samples) {
      Restart(stream, samples);
      return *this;
    }

    frameAverage& frameAverage::clear() {
      std::fill(history.begin(), history.end(), 0.f);
      std::fill(sums.begin(), sums.end(), 0.0);
      slot = 0;
      started = false;
      return *this;
    }

    Flt frameAverage::value(UInt bin) const {
      return bin < capacity() ? static_cast<Flt>(sums[bin] / frames) : 0.f;
    }

    void frameAverage::operator()(const stftFrame& frame, const Flt* in, Flt* out) {
      // A frame of another size: the stored bins belong to another spectrum.
      if (frame.bins != frameBins) {
        frameBins = frame.bins;
        clear();
      }
      if (started && (frame.index != lastIndex || frame.firstBin <= lastFirst))
        slot = slot + 1 < frames ? slot + 1 : 0;
      started = true;
      lastIndex = frame.index;
      lastFirst = frame.firstBin;

      const UInt bins = capacity();
      const UInt kept = frame.firstBin < bins ? std::min(frame.count, bins - frame.firstBin) : 0;
      if (kept > 0) {
        Flt* oldest = history.data() + static_cast<std::size_t>(slot) * bins + frame.firstBin;
        double* sum = sums.data() + frame.firstBin;
        const double scale = 1.0 / static_cast<double>(frames);
        for (UInt i = 0; i < kept; ++i) {
          const Flt x = Finite(in[i]);
          sum[i] += static_cast<double>(x) - static_cast<double>(oldest[i]);
          oldest[i] = x;
          out[i] = static_cast<Flt>(sum[i] * scale);
        }
      }
      std::fill(out + kept, out + frame.count, 0.f);
    }

    buffer& frameAverage::operator()(const stftFrame& frame, buffer& in) {
      return Block(output, frame, in,
                   [this](const stftFrame& run, const Flt* a, Flt* b) { (*this)(run, a, b); });
    }

    buffer& frameAverage::operator()(buffer& in) {
      return Stream(output, stream, in,
                    [this](const stftFrame& run, const Flt* a, Flt* b) { (*this)(run, a, b); });
    }

    // ---- frameSmooth -------------------------------------------------------

    frameSmooth::frameSmooth(UInt values)
      : output(STANDARD_BUFFERSIZE),
        recent(MAXIMUM_SMOOTHNESS, 0.f),
        window(MAXIMUM_SMOOTHNESS, 0.f),
        size(Between(values, 1, MAXIMUM_SMOOTHNESS)) {}

    // The window over the last `size` values of this frame, from the ring.
    void frameSmooth::resum() {
      held = std::min(size, taken);
      sum = 0.0;
      for (UInt i = 1; i <= held; ++i)
        sum += static_cast<double>(recent[(head - i) & RING_MASK]);
    }

    frameSmooth& frameSmooth::smoothness(UInt values) {
      size = Between(values, 1, MAXIMUM_SMOOTHNESS);
      resum();
      return *this;
    }

    frameSmooth& frameSmooth::frameSize(UInt samples) {
      Restart(stream, samples);
      return *this;
    }

    frameSmooth& frameSmooth::clear() {
      sum = 0.0;
      held = 0;
      taken = 0;
      return *this;
    }

    void frameSmooth::operator()(const stftFrame& frame, const Flt* in, Flt* out) {
      const bool continues = started && frame.index == lastIndex && frame.bins == frameBins &&
                             frame.firstBin == nextBin;
      if (!continues) clear();
      started = true;
      lastIndex = frame.index;
      frameBins = frame.bins;
      nextBin = frame.firstBin + frame.count;

      for (UInt i = 0; i < frame.count; ++i) {
        const Flt x = Finite(in[i]);
        if (held == size)
          sum -= static_cast<double>(recent[(head - size) & RING_MASK]);
        else
          held++;
        sum += static_cast<double>(x);
        recent[head] = x;
        head = (head + 1) & RING_MASK;
        if (taken < MAXIMUM_SMOOTHNESS) taken++;
        out[i] = static_cast<Flt>(sum / static_cast<double>(held));
      }
    }

    void frameSmooth::centred(const stftFrame& frame, const Flt* in, Flt* out) {
      // The window [k - before, k + after], cut to the frame. Values leave
      // before new ones come in, so the ring never holds more than `size`:
      // the ones still to leave are read from it, not from `in`, which `out`
      // may already have overwritten.
      const UInt n = frame.count;
      const UInt before = size / 2;
      const UInt after = size - 1 - before;
      double total = 0.0;
      UInt lo = 0;
      UInt hi = 0;
      for (UInt k = 0; k < n; ++k) {
        const UInt from = k > before ? k - before : 0;
        for (; lo < from; ++lo)
          total -= static_cast<double>(window[lo & RING_MASK]);
        const UInt to = std::min(n, k + after + 1);
        for (; hi < to; ++hi) {
          const Flt x = Finite(in[hi]);
          window[hi & RING_MASK] = x;
          total += static_cast<double>(x);
        }
        out[k] = static_cast<Flt>(total / static_cast<double>(hi - lo));
      }
    }

    buffer& frameSmooth::centred(const stftFrame& frame, buffer& in) {
      return Block(output, frame, in,
                   [this](const stftFrame& run, const Flt* a, Flt* b) { centred(run, a, b); });
    }

    buffer& frameSmooth::operator()(const stftFrame& frame, buffer& in) {
      return Block(output, frame, in,
                   [this](const stftFrame& run, const Flt* a, Flt* b) { (*this)(run, a, b); });
    }

    buffer& frameSmooth::operator()(buffer& in) {
      return Stream(output, stream, in,
                    [this](const stftFrame& run, const Flt* a, Flt* b) { (*this)(run, a, b); });
    }

  } // namespace DSP
} // namespace YSE
