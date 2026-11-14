#include "spectralPitchShift.hpp"

#include <algorithm>
#include <cmath>
#include "../../headers/constants.hpp"
#include "spectralBins.hpp"

namespace {
  constexpr double PI = 3.14159265358979323846;
  constexpr double TWO_PI = 2.0 * PI;

  // How far (in bins) a peak may have moved since the frame before and
  // still be the same partial, for its angle.
  constexpr UInt MATCH_DISTANCE = 2;

  // An angle wrapped to -pi .. pi.
  inline double Wrap(double angle) {
    return angle - TWO_PI * std::floor(angle / TWO_PI + 0.5);
  }

  inline UInt Between(UInt value, UInt lowest, UInt highest) {
    return value < lowest ? lowest : (value > highest ? highest : value);
  }
} // namespace

namespace YSE {
  namespace DSP {

    using spectralBins::Finite;

    spectralPitchShift::spectralPitchShift(Flt value, UInt capacity) : scale(1.f) {
      const UInt bins = Between(capacity, 1, MAXIMUM_CAPACITY);
      magnitude.assign(bins, 0.f);
      phase.assign(bins, 0.0);
      lastPhase.assign(bins, 0.0);
      peaks.assign(bins, 0);
      lastPeaks.assign(bins, 0);
      angle.assign(bins, 0.0);
      lastAngle.assign(bins, 0.0);
      real.assign(bins, 0.f);
      imaginary.assign(bins, 0.f);
      ratio(value);
    }

    spectralPitchShift& spectralPitchShift::ratio(Flt value) {
      if (!std::isfinite(value))
        scale = 1.f;
      else
        scale = std::min(std::max(value, 0.f), MAXIMUM_RATIO);
      return *this;
    }

    spectralPitchShift& spectralPitchShift::frequencyShift(Flt hz) {
      shiftHz = std::isfinite(hz) ? hz : 0.f;
      return *this;
    }

    spectralPitchShift& spectralPitchShift::clip(bool on) {
      clipping = on;
      return *this;
    }

    spectralPitchShift& spectralPitchShift::reset() {
      started = false;
      lastPeakCount = 0;
      return *this;
    }

    void spectralPitchShift::operator()(stftFrame& frame) {
      (*this)(frame, frame.real, frame.imaginary, frame.real, frame.imaginary);
    }

    void spectralPitchShift::operator()(const stftFrame& frame, const Flt* inReal,
                                        const Flt* inImaginary, Flt* outReal, Flt* outImaginary) {
      const UInt count = frame.count;
      const UInt size = frame.size;

      // Not a whole frame: passed through, and the next frame is a first.
      if (frame.firstBin != 0 || size < 2) {
        spectralBins::PassThrough(count, inReal, inImaginary, outReal, outImaginary);
        reset();
        return;
      }

      // The bins processed: the lower half the frame holds, DC to Nyquist,
      // within the capacity.
      const UInt top = spectralBins::Top(frame, capacity());

      const bool continues = started && frame.index == lastIndex + 1 && size == lastSize &&
                             frame.hop == lastHop && top == lastTop;
      if (!continues) lastPeakCount = 0;
      const bool estimate = continues && frame.hop > 0;

      // Magnitudes and phases.
      for (UInt k = 0; k < top; ++k) {
        const double re = inReal != nullptr ? Finite(inReal[k]) : 0.f;
        const double im = inImaginary != nullptr ? Finite(inImaginary[k]) : 0.f;
        magnitude[k] = static_cast<Flt>(std::sqrt(re * re + im * im));
        phase[k] = std::atan2(im, re);
        real[k] = 0.f;
        imaginary[k] = 0.f;
      }

      // Peaks: above 0, above the bin below, at least the bin above.
      peakCount = 0;
      for (UInt k = 0; k < top; ++k) {
        const Flt m = magnitude[k];
        if (!(m > 0.f)) continue;
        if (k > 0 && !(m > magnitude[k - 1])) continue;
        if (k + 1 < top && !(m >= magnitude[k + 1])) continue;
        peaks[peakCount++] = k;
      }

      const bool identity = scale == 1.f && shiftHz == 0.f;
      const double shiftBins =
          SAMPLERATE > 0 ? static_cast<double>(shiftHz) * size / static_cast<double>(SAMPLERATE)
                         : 0.0;
      // Far enough that every bin of every region folds or clips, near
      // enough that rounding stays exact.
      const double reach = 4.0 * static_cast<double>(size);

      UInt match = 0; // the first previous peak not below p - MATCH_DISTANCE
      UInt from = 0; // where this peak's region starts
      for (UInt i = 0; i < peakCount; ++i) {
        const UInt p = peaks[i];

        // Where the region ends: the quietest bin up to the next peak.
        UInt to = top;
        if (i + 1 < peakCount) {
          const UInt next = peaks[i + 1];
          to = p + 1;
          for (UInt k = p + 2; k <= next; ++k)
            if (magnitude[k] < magnitude[to]) to = k;
        }

        // The true frequency, in bins.
        double w = p;
        if (estimate) {
          const double expected =
              TWO_PI * static_cast<double>((static_cast<U64>(p) * frame.hop) % size) / size;
          const double deviation = Wrap(phase[p] - lastPhase[p] - expected);
          const double bins = deviation * size / (TWO_PI * frame.hop);
          w += std::min(std::max(bins, -1.0), 1.0);
        }

        long long d = 0;
        double step = 0.0;
        if (!identity) {
          const double target =
              std::min(std::max(static_cast<double>(scale) * w + shiftBins, -reach), reach);
          d = std::llround(target) - static_cast<long long>(p);
          step = TWO_PI * (target - w) * frame.hop / size;
        }

        // The angle: the nearest peak of the frame before, advanced.
        while (match < lastPeakCount && lastPeaks[match] + MATCH_DISTANCE < p)
          match++;
        double turn = 0.0;
        bool found = false;
        UInt best = 0;
        for (UInt j = match; j < lastPeakCount && lastPeaks[j] <= p + MATCH_DISTANCE; ++j) {
          const UInt distance = lastPeaks[j] > p ? lastPeaks[j] - p : p - lastPeaks[j];
          if (!found || distance < best) {
            found = true;
            best = distance;
            turn = lastAngle[j];
          }
        }
        if (found) turn = Wrap(turn + step);
        angle[i] = turn;

        // The region, moved and turned.
        const double c = std::cos(turn);
        const double s = std::sin(turn);
        for (UInt k = from; k < to; ++k) {
          const double re = inReal != nullptr ? Finite(inReal[k]) : 0.f;
          const double im = inImaginary != nullptr ? Finite(inImaginary[k]) : 0.f;
          if (re == 0.0 && im == 0.0) continue;
          std::size_t at = 0;
          bool mirrored = false;
          if (!spectralBins::Place(static_cast<long long>(k) + d, size, top, clipping, at,
                                   mirrored))
            continue;
          const double yr = re * c - im * s;
          const double yi = re * s + im * c;
          real[at] += static_cast<Flt>(yr);
          imaginary[at] += static_cast<Flt>(mirrored ? -yi : yi);
        }
        from = to;
      }

      // Out: the lower half, the upper as its mirror in a full spectrum,
      // the rest 0.
      spectralBins::Write(frame, top, real.data(), imaginary.data(), outReal, outImaginary);

      std::swap(phase, lastPhase);
      std::swap(peaks, lastPeaks);
      std::swap(angle, lastAngle);
      lastPeakCount = peakCount;
      started = true;
      lastIndex = frame.index;
      lastSize = size;
      lastHop = frame.hop;
      lastTop = top;
    }

  } // namespace DSP
} // namespace YSE
