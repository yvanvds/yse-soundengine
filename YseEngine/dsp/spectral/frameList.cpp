#include "frameList.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace {
  using YSE::DSP::buffer;
  using YSE::DSP::frameSource;
  using YSE::DSP::stftFrame;

  inline UInt Clamp(UInt length) {
    return length > YSE::STANDARD_BUFFERSIZE ? YSE::STANDARD_BUFFERSIZE : length;
  }

  inline UInt Capacity(UInt bins) {
    if (bins < 1) return 1;
    return bins > frameSource::MAXIMUM_CAPACITY ? frameSource::MAXIMUM_CAPACITY : bins;
  }

  // Within the capacity set in the constructor: no allocation.
  inline void Fit(buffer& b, UInt length) {
    if (b.getLength() != length) b.resize(length);
  }

  inline Flt Finite(Flt value) {
    return std::isfinite(value) ? value : 0.f;
  }

  // A bin an offset skipped: not set, so it puts out the fill value. Values
  // are kept finite, so a NaN is never a value.
  constexpr Flt SKIPPED = std::numeric_limits<Flt>::quiet_NaN();
} // namespace

namespace YSE {
  namespace DSP {

    // ---- frameSource -------------------------------------------------------

    frameSource::frameSource(UInt capacity)
      : output(STANDARD_BUFFERSIZE), values(Capacity(capacity), 0.f) {}

    frameSource& frameSource::set(const Flt* in, UInt count, UInt offset) {
      const UInt cap = capacity();
      const UInt start = std::min(offset, cap);
      const UInt end = count > cap - start ? cap : start + count;
      // Bins between the last one set and the offset are skipped, not set.
      for (UInt k = specified; k < start; ++k)
        values[k] = SKIPPED;
      for (UInt k = start; k < end; ++k)
        values[k] = Finite(in[k - start]);
      specified = end;
      return *this;
    }

    frameSource& frameSource::clear() {
      specified = 0;
      return *this;
    }

    frameSource& frameSource::fill(Flt value) {
      filler = Finite(value);
      return *this;
    }

    Flt frameSource::value(UInt bin) const {
      if (bin >= specified) return filler;
      const Flt v = values[bin];
      return std::isnan(v) ? filler : v;
    }

    frameSource& frameSource::copy(const frameSource& other) {
      const UInt n = std::min(other.specified, capacity());
      std::copy(other.values.begin(), other.values.begin() + n, values.begin());
      specified = n;
      filler = other.filler;
      mirroring = other.mirroring;
      return *this;
    }

    void frameSource::operator()(const stftFrame& frame, Flt* out) {
      // Bins above Nyquist exist only in a full-spectrum frame.
      const UInt nyquist = frame.fullSpectrum && frame.size > 0 ? frame.size / 2 : ~UInt(0);
      for (UInt i = 0; i < frame.count; ++i) {
        const UInt k = frame.firstBin + i;
        if (k <= nyquist) {
          out[i] = value(k);
        } else {
          out[i] = mirroring && k < frame.size ? value(frame.size - k) : 0.f;
        }
      }
    }

    buffer& frameSource::operator()(const stftFrame& frame) {
      stftFrame run = frame;
      run.count = Clamp(frame.count);
      Fit(output, run.count);
      (*this)(run, output.getPtr());
      return output;
    }

    buffer& frameSource::operator()(UInt length) {
      stftFrame frame;
      frame.count = Clamp(length);
      frame.bins = frame.count;
      return (*this)(frame);
    }

    // ---- frameSnapshot -----------------------------------------------------

    frameSnapshot::frameSnapshot(UInt capacity)
      : taking(Capacity(capacity), 0.f), latest(Capacity(capacity), 0.f) {}

    void frameSnapshot::operator()(const stftFrame& frame, const Flt* in) {
      if (frame.count == 0) return;
      const UInt bins = frame.bins > 0 ? frame.bins : frame.firstBin + frame.count;
      if (!started || frame.index != lastIndex || bins != takingBins ||
          frame.firstBin <= lastFirst) {
        // A new frame; one still being taken is dropped.
        started = true;
        intact = true;
        lastIndex = frame.index;
        takingBins = bins;
        nextBin = 0;
      }
      // A run must start where the one before ended.
      if (frame.firstBin != nextBin) intact = false;
      lastFirst = frame.firstBin;
      nextBin = frame.firstBin + frame.count;

      const UInt cap = capacity();
      if (frame.firstBin < cap) {
        const UInt kept = std::min(frame.count, cap - frame.firstBin);
        Flt* to = taking.data() + frame.firstBin;
        for (UInt i = 0; i < kept; ++i)
          to[i] = Finite(in[i]);
      }

      if (intact && nextBin >= bins) {
        // Whole: it becomes the frame read, and the old one's storage takes
        // the next. A swap moves no values and allocates nothing.
        taking.swap(latest);
        latestSize = std::min(bins, cap);
        latestBins = bins;
        completed++;
        intact = false;
      }
    }

    void frameSnapshot::operator()(buffer& in) {
      stftFrame frame;
      frame.count = Clamp(in.getLength());
      frame.bins = frame.count;
      frame.index = blocks++;
      (*this)(frame, in.getPtr());
    }

    UInt frameSnapshot::copy(Flt* to, UInt max) const {
      const UInt n = std::min(latestSize, max);
      std::copy(latest.begin(), latest.begin() + n, to);
      return n;
    }

    frameSnapshot& frameSnapshot::clear() {
      latestSize = 0;
      latestBins = 0;
      completed = 0;
      started = false;
      intact = false;
      nextBin = 0;
      return *this;
    }

  } // namespace DSP
} // namespace YSE
