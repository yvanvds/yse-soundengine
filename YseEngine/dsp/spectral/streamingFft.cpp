#include "streamingFft.hpp"

#include <algorithm>
#include "../fourier/mayer.h"

namespace {
  using YSE::DSP::buffer;

  // Within the capacity set in the constructor: no allocation.
  inline void Fit(buffer& b, UInt length) {
    if (b.getLength() != length) b.resize(length);
  }

  inline UInt PowerOfTwoAtLeast(UInt value, UInt lowest, UInt highest) {
    UInt p = lowest;
    while (p < value && p < highest)
      p <<= 1;
    return p;
  }
} // namespace

namespace YSE {
  namespace DSP {

    UInt streamingTransform::fitSize(UInt size) {
      return PowerOfTwoAtLeast(size, MINIMUM_SIZE, MAXIMUM_SIZE);
    }

    UInt streamingTransform::fitInterval(UInt interval, UInt size) {
      const UInt frame = fitSize(size);
      if (interval == 0) return frame;
      return PowerOfTwoAtLeast(interval, frame, MAXIMUM_INTERVAL);
    }

    streamingTransform::streamingTransform(bool inverse, UInt size, UInt interval, UInt offset)
      : inverse(inverse),
        clock(fitSize(size), fitInterval(interval, size), offset),
        frameReal(fitSize(size), 0.f),
        frameImaginary(fitSize(size), 0.f),
        outReal(fitSize(size), 0.f),
        outImaginary(fitSize(size), 0.f),
        realOut(STANDARD_BUFFERSIZE),
        imaginaryOut(STANDARD_BUFFERSIZE),
        syncOut(STANDARD_BUFFERSIZE) {
      realOut = 0.f;
      imaginaryOut = 0.f;
      syncOut = 0.f;
    }

    void streamingTransform::reset() {
      std::fill(frameReal.begin(), frameReal.end(), 0.f);
      std::fill(frameImaginary.begin(), frameImaginary.end(), 0.f);
      std::fill(outReal.begin(), outReal.end(), 0.f);
      std::fill(outImaginary.begin(), outImaginary.end(), 0.f);
      outIndex = 0;
      outputting = false;
      clock.reset();
    }

    buffer& streamingTransform::operator()(buffer& real, buffer& imaginary) {
      return process(real.getPtr(), imaginary.getPtr(),
                     std::min(real.getLength(), imaginary.getLength()));
    }

    buffer& streamingTransform::operator()(buffer& real) {
      return process(real.getPtr(), nullptr, real.getLength());
    }

    void streamingTransform::transform() {
      const UInt n = clock.size();
      std::copy(frameReal.begin(), frameReal.end(), outReal.begin());
      std::copy(frameImaginary.begin(), frameImaginary.end(), outImaginary.begin());
      if (inverse)
        mayer_ifft(static_cast<int>(n), outReal.data(), outImaginary.data());
      else
        mayer_fft(static_cast<int>(n), outReal.data(), outImaginary.data());
      outIndex = 0;
      outputting = true;
    }

    buffer& streamingTransform::process(const Flt* realIn, const Flt* imaginaryIn, UInt length) {
      length = std::min(length, STANDARD_BUFFERSIZE);
      Fit(realOut, length);
      Fit(imaginaryOut, length);
      Fit(syncOut, length);
      Flt* re = realOut.getPtr();
      Flt* im = imaginaryOut.getPtr();
      Flt* sync = syncOut.getPtr();
      const UInt n = clock.size();

      // The block in runs, each ending where something changes: the frame
      // stops or starts taking samples in, a frame completes (and is
      // transformed right there), an output period ends.
      UInt i = 0;
      while (i < length) {
        const UInt pos = clock.position();
        const bool taking = pos < n;
        UInt run = std::min(length - i, taking ? n - pos : clock.interval() - pos);
        run = std::min(run, clock.untilFrame());
        if (outputting) run = std::min(run, n - outIndex);

        if (taking) {
          std::copy(realIn + i, realIn + i + run, frameReal.begin() + pos);
          if (imaginaryIn != nullptr)
            std::copy(imaginaryIn + i, imaginaryIn + i + run, frameImaginary.begin() + pos);
          else
            std::fill(frameImaginary.begin() + pos, frameImaginary.begin() + pos + run, 0.f);
        }

        if (outputting) {
          std::copy(outReal.begin() + outIndex, outReal.begin() + outIndex + run, re + i);
          std::copy(outImaginary.begin() + outIndex, outImaginary.begin() + outIndex + run, im + i);
          for (UInt j = 0; j < run; ++j)
            sync[i + j] = static_cast<Flt>(outIndex + j);
          outIndex += run;
          if (outIndex == n) outputting = false;
        } else {
          std::fill(re + i, re + i + run, 0.f);
          std::fill(im + i, im + i + run, 0.f);
          std::fill(sync + i, sync + i + run, 0.f);
        }

        i += run;
        if (clock.advance(run)) transform();
      }
      return realOut;
    }

  } // namespace DSP
} // namespace YSE
