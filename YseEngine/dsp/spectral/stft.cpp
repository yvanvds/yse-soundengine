#include "stft.hpp"

#include <algorithm>
#include <cmath>
#include "../fourier/mayer.h"

namespace {
  constexpr double TWO_PI = 6.283185307179586476925286766559;

  UInt PowerOfTwoAtLeast(UInt value, UInt lowest, UInt highest) {
    UInt p = lowest;
    while (p < value && p < highest)
      p <<= 1;
    return p;
  }

  // The window sums of an overlap below this share of their largest are too
  // small to divide by (see stftSynthesis).
  constexpr double SMALLEST_SUM = 1.0e-3;
} // namespace

namespace YSE {
  namespace DSP {

    // ─── stftWindow ────────────────────────────────────────────────────────

    Flt stftWindow::value(TYPE type, UInt n, UInt size) {
      if (size == 0) return 1.f;
      const double x = static_cast<double>(n % size) / static_cast<double>(size);
      switch (type) {
      case TRIANGLE:
        return static_cast<Flt>(1.0 - std::fabs(2.0 * x - 1.0));
      case HANNING:
        return static_cast<Flt>(0.5 - 0.5 * std::cos(TWO_PI * x));
      case HAMMING:
        return static_cast<Flt>(0.54 - 0.46 * std::cos(TWO_PI * x));
      case BLACKMAN:
        return static_cast<Flt>(0.42 - 0.5 * std::cos(TWO_PI * x) +
                                0.08 * std::cos(2.0 * TWO_PI * x));
      case SQUARE:
      default:
        return 1.f;
      }
    }

    void stftWindow::fill(TYPE type, Flt* out, UInt size) {
      for (UInt i = 0; i < size; ++i)
        out[i] = value(type, i, size);
    }

    // ─── stftAnalysis ──────────────────────────────────────────────────────

    stftAnalysis::stftAnalysis(UInt size, bool fullSpectrum, stftWindow::TYPE window)
      : n(stft::fitSize(size)),
        full(fullSpectrum),
        type(window),
        history(n, 0.f),
        shape(n, 0.f),
        re(n, 0.f),
        im(n, 0.f) {
      stftWindow::fill(type, shape.data(), n);
    }

    void stftAnalysis::window(stftWindow::TYPE shapeType) {
      if (shapeType == type) return;
      type = shapeType;
      stftWindow::fill(type, shape.data(), n);
    }

    void stftAnalysis::write(const Flt* samples, UInt count) {
      while (count > 0) {
        const UInt run = std::min(count, n - pos);
        std::copy(samples, samples + run, history.begin() + pos);
        samples += run;
        count -= run;
        pos += run;
        if (pos == n) pos = 0;
      }
    }

    void stftAnalysis::analyse() {
      // The oldest sample first: from pos to the end of the ring, then the
      // start of the ring up to pos.
      const UInt tail = n - pos;
      for (UInt k = 0; k < tail; ++k)
        re[k] = history[pos + k] * shape[k];
      for (UInt k = tail; k < n; ++k)
        re[k] = history[k - tail] * shape[k];
      std::fill(im.begin(), im.end(), 0.f);
      mayer_fft(static_cast<int>(n), re.data(), im.data());
    }

    void stftAnalysis::reset() {
      std::fill(history.begin(), history.end(), 0.f);
      std::fill(re.begin(), re.end(), 0.f);
      std::fill(im.begin(), im.end(), 0.f);
      pos = 0;
    }

    // ─── stftSynthesis ─────────────────────────────────────────────────────

    stftSynthesis::stftSynthesis(UInt size, UInt overlap, bool fullSpectrum,
                                 stftWindow::TYPE window, stftWindow::TYPE analysisWindow)
      : n(stft::fitSize(size)),
        h(n / stft::fitOverlap(overlap, size)),
        full(fullSpectrum),
        type(window),
        analysisType(analysisWindow),
        shape(n, 0.f),
        norm(h, 0.f),
        re(n, 0.f),
        im(n, 0.f),
        ola(n, 0.f) {
      tables();
    }

    void stftSynthesis::window(stftWindow::TYPE synthesis, stftWindow::TYPE analysis) {
      if (synthesis == type && analysis == analysisType) return;
      type = synthesis;
      analysisType = analysis;
      tables();
    }

    void stftSynthesis::tables() {
      stftWindow::fill(type, shape.data(), n);
      // The sum of analysis x synthesis window over the frames overlapping
      // at each position of the hop, kept in norm for now.
      double total = 0.0;
      double largest = 0.0;
      for (UInt p = 0; p < h; ++p) {
        double sum = 0.0;
        for (UInt k = p; k < n; k += h)
          sum += static_cast<double>(stftWindow::value(analysisType, k, n)) *
                 static_cast<double>(shape[k]);
        norm[p] = static_cast<Flt>(sum);
        total += sum;
        largest = std::max(largest, sum);
      }
      double smallest = largest;
      for (UInt p = 0; p < h; ++p)
        smallest = std::min(smallest, static_cast<double>(norm[p]));
      const double size = static_cast<double>(n);
      const double mean = total / static_cast<double>(h);
      const bool perSample = largest > 0.0 && smallest >= SMALLEST_SUM * largest;
      for (UInt p = 0; p < h; ++p) {
        const double sum = perSample ? static_cast<double>(norm[p]) : mean;
        norm[p] = sum > 0.0 ? static_cast<Flt>(1.0 / (size * sum)) : 0.f;
      }
    }

    void stftSynthesis::clear() {
      std::fill(re.begin(), re.end(), 0.f);
      std::fill(im.begin(), im.end(), 0.f);
    }

    void stftSynthesis::synthesise() {
      if (!full) {
        // A real stream's spectrum: the upper half mirrors the lower, and DC
        // and Nyquist are real.
        const UInt half = n / 2;
        for (UInt k = 1; k < half; ++k) {
          re[n - k] = re[k];
          im[n - k] = -im[k];
        }
        im[0] = 0.f;
        im[half] = 0.f;
      }
      mayer_ifft(static_cast<int>(n), re.data(), im.data());
      // Added from the next output sample on: sample k of the frame lands k
      // samples after it.
      const UInt tail = n - pos;
      for (UInt k = 0; k < tail; ++k)
        ola[pos + k] += re[k] * shape[k] * norm[k % h];
      for (UInt k = tail; k < n; ++k)
        ola[k - tail] += re[k] * shape[k] * norm[k % h];
      clear();
    }

    void stftSynthesis::read(Flt* out, UInt count) {
      while (count > 0) {
        const UInt run = std::min(count, n - pos);
        std::copy(ola.begin() + pos, ola.begin() + pos + run, out);
        std::fill(ola.begin() + pos, ola.begin() + pos + run, 0.f);
        out += run;
        count -= run;
        pos += run;
        if (pos == n) pos = 0;
      }
    }

    void stftSynthesis::reset() {
      clear();
      std::fill(ola.begin(), ola.end(), 0.f);
      pos = 0;
    }

    // ─── stft ──────────────────────────────────────────────────────────────

    UInt stft::fitSize(UInt size) {
      return PowerOfTwoAtLeast(size, MINIMUM_SIZE, MAXIMUM_SIZE);
    }

    UInt stft::fitOverlap(UInt overlap, UInt size) {
      return PowerOfTwoAtLeast(overlap, 1, fitSize(size));
    }

    stft::stft(UInt size, UInt overlap, bool fullSpectrum, UInt offset)
      : clock(fitSize(size), fitSize(size) / fitOverlap(overlap, size), offset),
        in(size, fullSpectrum),
        out(size, overlap, fullSpectrum),
        result(STANDARD_BUFFERSIZE) {
      result = 0.f;
    }

    void stft::window(stftWindow::TYPE analysis, stftWindow::TYPE synthesis) {
      in.window(analysis);
      out.window(synthesis, analysis);
    }

    void stft::reset() {
      clock.reset();
      in.reset();
      out.reset();
      completed = 0;
    }

    buffer& stft::process(buffer& input, frameFunction function, void* user) {
      const UInt length = std::min(input.getLength(), STANDARD_BUFFERSIZE);
      // Within the capacity set in the constructor: no allocation.
      if (result.getLength() != length) result.resize(length);
      const Flt* src = input.getPtr();
      Flt* dst = result.getPtr();
      const UInt bins = in.bins();

      // The block in runs, each ending where a frame completes; the frame's
      // work runs right there, inside the block.
      UInt i = 0;
      while (i < length) {
        const UInt run = std::min(length - i, clock.untilFrame());
        in.write(src + i, run);
        out.read(dst + i, run);
        i += run;
        if (!clock.advance(run)) continue;

        in.analyse();
        if (function != nullptr) {
          stftFrame frame;
          frame.real = in.real();
          frame.imaginary = in.imaginary();
          frame.firstBin = 0;
          frame.count = bins;
          frame.bins = bins;
          frame.size = clock.size();
          frame.hop = clock.interval();
          frame.fullSpectrum = in.fullSpectrum();
          frame.index = completed;
          function(frame, user);
        }
        std::copy(in.real(), in.real() + bins, out.real());
        std::copy(in.imaginary(), in.imaginary() + bins, out.imaginary());
        out.synthesise();
        completed++;
      }
      return result;
    }

  } // namespace DSP
} // namespace YSE
