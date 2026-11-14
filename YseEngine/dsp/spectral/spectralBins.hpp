/*
  ==============================================================================

    spectralBins.hpp
    What the whole-frame spectral shifters share: which bins of a frame they
    process, where a bin moved out of 0 .. Nyquist goes, and how the frame
    they built is written out (issues #1164, #1165). Internal: not in
    yse.hpp.

  ==============================================================================
*/

#ifndef YSE_DSP_SPECTRAL_SPECTRALBINS_HPP
#define YSE_DSP_SPECTRAL_SPECTRALBINS_HPP

#include <algorithm>
#include <cmath>
#include <cstddef>
#include "../../headers/types.hpp"
#include "stft.hpp"

namespace YSE {
  namespace DSP {
    namespace spectralBins {

      /** @brief @p value, or 0 when it is not a finite number. */
      inline Flt Finite(Flt value) {
        return std::isfinite(value) ? value : 0.f;
      }

      /** @brief A run that is not a whole frame: @p count bins copied
       *         through, non-finite as 0. Either input may be null (all 0). */
      inline void PassThrough(UInt count, const Flt* inReal, const Flt* inImaginary, Flt* outReal,
                              Flt* outImaginary) {
        for (UInt k = 0; k < count; ++k) {
          const Flt re = inReal != nullptr ? Finite(inReal[k]) : 0.f;
          const Flt im = inImaginary != nullptr ? Finite(inImaginary[k]) : 0.f;
          // The outputs are never null. In place they are the inputs, and
          // then not null either: the analyzer's path through the input
          // test is one no caller takes.
          outReal[k] = re; // NOLINT(clang-analyzer-core.NullDereference): see above
          outImaginary[k] = im; // NOLINT(clang-analyzer-core.NullDereference): see above
        }
      }

      /** @brief How many bins of a whole @p frame are processed: the lower
       *         half it holds, DC to Nyquist, within @p capacity. */
      inline UInt Top(const stftFrame& frame, UInt capacity) {
        const UInt half = frame.size / 2;
        UInt top = frame.fullSpectrum ? std::min(frame.count, half + 1)
                                      : std::min(frame.count, frame.bins);
        return std::min(std::min(top, half + 1), capacity);
      }

      /** @brief Where bin @p j (of an FFT of @p size, any integer) lands in
       *         the lower half: itself within 0 .. Nyquist; outside it,
       *         folded as its frequency would alias (read modulo @p size,
       *         above Nyquist the conjugate at ``size - j``, @p mirrored set),
       *         or dropped with @p clip. False when dropped or not below
       *         @p top. */
      inline bool Place(long long j, UInt size, UInt top, bool clip, std::size_t& at,
                        bool& mirrored) {
        const auto n = static_cast<long long>(size);
        const auto half = static_cast<long long>(size / 2);
        mirrored = false;
        if (j < 0 || j > half) {
          if (clip) return false;
          j %= n;
          if (j < 0) j += n;
          if (j > half) {
            j = n - j;
            mirrored = true;
          }
        }
        if (j >= static_cast<long long>(top)) return false;
        at = static_cast<std::size_t>(j);
        return true;
      }

      /** @brief The built lower half (@p top bins of @p real /
       *         @p imaginary) into the frame's @p outReal / @p outImaginary:
       *         a full spectrum's upper half as its mirror (the complex
       *         conjugate), the rest 0. */
      inline void Write(const stftFrame& frame, UInt top, const Flt* real, const Flt* imaginary,
                        Flt* outReal, Flt* outImaginary) {
        const UInt size = frame.size;
        const UInt half = size / 2;
        for (UInt k = 0; k < top; ++k) {
          outReal[k] = real[k];
          outImaginary[k] = imaginary[k];
        }
        for (UInt k = top; k < frame.count; ++k) {
          if (frame.fullSpectrum && k > half && size - k < top) {
            outReal[k] = real[size - k];
            outImaginary[k] = -imaginary[size - k];
          } else {
            outReal[k] = 0.f;
            outImaginary[k] = 0.f;
          }
        }
      }

    } // namespace spectralBins
  } // namespace DSP
} // namespace YSE

#endif // YSE_DSP_SPECTRAL_SPECTRALBINS_HPP
