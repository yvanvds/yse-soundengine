/*
  ==============================================================================

    delayHistory.hpp
    The input and output history of a recursive delay filter, read back with
    linear interpolation at a delay given in ms. Shared by DSP::allpass
    (#1093) and DSP::comb (#1094).

  ==============================================================================
*/

#ifndef YSE_DSP_DELAY_DELAYHISTORY_HPP
#define YSE_DSP_DELAY_DELAYHISTORY_HPP

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>
#include "../../headers/types.hpp"
#include "../../headers/constants.hpp"

namespace YSE {
  namespace DSP {
    namespace detail {

      /**
       *  @brief Two delay lines with one write position: the input ``x`` and
       *         the output ``y`` of a filter of the form
       *         ``y(n) = f(x(n), x(n-F), y(n-B))``.
       *
       *  The constructor allocates both lines for @p maximumDelayMs (clamped
       *  to 0 .. @p longestMs; not finite: @p defaultMs) at
       *  @p highestSampleRate; nothing allocates afterwards. A delay is given
       *  in ms, converted with the live ``SAMPLERATE`` and clamped to one
       *  sample at the least (``y(n-0)`` is the sample being computed) and
       *  the line at the most. Reads come before ``Write()`` within a sample.
       *  Implementation detail; not part of the public API.
       */
      class delayHistory {
      public:
        delayHistory(Flt maximumDelayMs, UInt highestSampleRate, Flt defaultMs, Flt longestMs) {
          maximumMs = std::isfinite(maximumDelayMs) ? std::clamp(maximumDelayMs, 0.f, longestMs)
                                                    : defaultMs;
          const Dbl samples =
              std::ceil(static_cast<Dbl>(maximumMs) * static_cast<Dbl>(highestSampleRate) * 0.001);
          // At least one sample: the shortest delay the recursion can read.
          lineSamples = std::max<UInt>(1, static_cast<UInt>(samples));
          // Two more, so a read at the longest delay can interpolate without
          // meeting the write position.
          x.assign(static_cast<std::size_t>(lineSamples) + 2, 0.f);
          y.assign(static_cast<std::size_t>(lineSamples) + 2, 0.f);
        }

        /** Samples per ms at the live sample rate. */
        static Flt PerMs() {
          // Divided rather than multiplied by 0.001f, so whole-sample delays
          // at the usual rates come out whole.
          return static_cast<Flt>(SAMPLERATE) / 1000.f;
        }

        /** The delay in samples for @p ms at @p perMs samples per ms, clamped
         *  to 1 .. @p longest. */
        static Flt Samples(Flt ms, Flt perMs, Flt longest) {
          return std::clamp(ms * perMs, 1.f, std::max(1.f, longest));
        }

        /** The longest delay in samples at @p perMs samples per ms. */
        Flt Longest(Flt perMs) const {
          return std::min(maximumMs * perMs, static_cast<Flt>(lineSamples));
        }

        /** The longest delay in ms at the live sample rate. */
        Flt Capacity() const {
          const Flt perMs = PerMs();
          if (!(perMs > 0.f)) return 0.f;
          return Longest(perMs) / perMs;
        }

        /** The delay in samples for @p ms at the live sample rate. */
        Flt SamplesNow(Flt ms) const {
          const Flt perMs = PerMs();
          return Samples(ms, perMs, Longest(perMs));
        }

        Flt MaximumMs() const {
          return maximumMs;
        }

        /** The input @p back samples ago (1 .. the longest), interpolated. */
        Flt ReadX(Flt back) const {
          return Read(x, back);
        }

        /** The output @p back samples ago (1 .. the longest), interpolated. */
        Flt ReadY(Flt back) const {
          return Read(y, back);
        }

        /** Stores this sample's input and output and moves on. */
        void Write(Flt in, Flt out) {
          x[writePos] = in;
          y[writePos] = out;
          if (++writePos == x.size()) writePos = 0;
        }

        /** Silences both lines. */
        void Clear() {
          std::fill(x.begin(), x.end(), 0.f);
          std::fill(y.begin(), y.end(), 0.f);
        }

      private:
        Flt Read(const std::vector<Flt>& line, Flt back) const {
          // 1 <= back <= lineSamples, so both taps are older than the write
          // position and inside the line.
          const auto whole = static_cast<std::size_t>(back);
          const Flt frac = back - static_cast<Flt>(whole);
          const std::size_t size = line.size();
          const std::size_t first = writePos >= whole ? writePos - whole : writePos + size - whole;
          const std::size_t second = first == 0 ? size - 1 : first - 1;
          return line[first] + frac * (line[second] - line[first]);
        }

        std::vector<Flt> x;
        std::vector<Flt> y;
        std::size_t writePos = 0;
        UInt lineSamples = 1;
        Flt maximumMs = 0.f;
      };

    } // namespace detail
  } // namespace DSP
} // namespace YSE

#endif // YSE_DSP_DELAY_DELAYHISTORY_HPP
