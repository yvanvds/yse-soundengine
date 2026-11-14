#include "spectralFreqShift.hpp"

#include <algorithm>
#include <cmath>
#include "../../headers/constants.hpp"
#include "spectralBins.hpp"

namespace {
  constexpr double PI = 3.14159265358979323846;
  constexpr double TWO_PI = 2.0 * PI;

  // An angle wrapped to -pi .. pi.
  inline double Wrap(double angle) {
    return angle - TWO_PI * std::floor(angle / TWO_PI + 0.5);
  }
} // namespace

namespace YSE {
  namespace DSP {

    using spectralBins::Finite;

    spectralFreqShift::spectralFreqShift(Flt hz, UInt capacity) : shiftHz(0.f) {
      const UInt bins = std::min(std::max(capacity, 1u), MAXIMUM_CAPACITY);
      real.assign(bins, 0.f);
      imaginary.assign(bins, 0.f);
      frequencyShift(hz);
    }

    spectralFreqShift& spectralFreqShift::frequencyShift(Flt hz) {
      shiftHz = std::isfinite(hz) ? hz : 0.f;
      return *this;
    }

    spectralFreqShift& spectralFreqShift::clip(bool on) {
      clipping = on;
      return *this;
    }

    spectralFreqShift& spectralFreqShift::reset() {
      started = false;
      theta = 0.0;
      return *this;
    }

    void spectralFreqShift::operator()(stftFrame& frame) {
      (*this)(frame, frame.real, frame.imaginary, frame.real, frame.imaginary);
    }

    void spectralFreqShift::operator()(const stftFrame& frame, const Flt* inReal,
                                       const Flt* inImaginary, Flt* outReal, Flt* outImaginary) {
      const UInt size = frame.size;

      // Not a whole frame: passed through, and the next frame is a first.
      if (frame.firstBin != 0 || size < 2) {
        spectralBins::PassThrough(frame.count, inReal, inImaginary, outReal, outImaginary);
        reset();
        return;
      }

      const UInt top = spectralBins::Top(frame, capacity());

      // The shift in bins at the live rate, held where every bin of the
      // frame folds or clips anyway and rounding stays exact.
      const double reach = 4.0 * static_cast<double>(size);
      const double shift = SAMPLERATE > 0 ? std::min(std::max(static_cast<double>(shiftHz) * size /
                                                                  static_cast<double>(SAMPLERATE),
                                                              -reach),
                                                     reach)
                                          : 0.0;
      const long long d = std::llround(shift);

      // The angle: 0 on a first frame, advanced by every frame since.
      if (started && frame.index > lastIndex && frame.hop == lastHop) {
        const auto frames = static_cast<double>(frame.index - lastIndex);
        theta = Wrap(theta + TWO_PI * shift * frame.hop / size * frames);
      } else {
        theta = 0.0;
      }

      for (UInt k = 0; k < top; ++k) {
        real[k] = 0.f;
        imaginary[k] = 0.f;
      }
      const double c = std::cos(theta);
      const double s = std::sin(theta);
      for (UInt k = 0; k < top; ++k) {
        const double re = inReal != nullptr ? Finite(inReal[k]) : 0.f;
        const double im = inImaginary != nullptr ? Finite(inImaginary[k]) : 0.f;
        if (re == 0.0 && im == 0.0) continue;
        std::size_t at = 0;
        bool mirrored = false;
        if (!spectralBins::Place(static_cast<long long>(k) + d, size, top, clipping, at, mirrored))
          continue;
        const double yr = re * c - im * s;
        const double yi = re * s + im * c;
        real[at] += static_cast<Flt>(yr);
        imaginary[at] += static_cast<Flt>(mirrored ? -yi : yi);
      }

      spectralBins::Write(frame, top, real.data(), imaginary.data(), outReal, outImaginary);

      started = true;
      lastIndex = frame.index;
      lastHop = frame.hop;
    }

  } // namespace DSP
} // namespace YSE
