#pragma once

#include <algorithm>
#include <vector>
#include "../../headers/types.hpp"
#include "../../headers/constants.hpp"
#include "../../dsp/buffer.hpp"
#include "../../dsp/spectral/stft.hpp"
#include "spectralPass.h"

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief The frame a frame-wide object inside a ``~pfft`` works on
     *         (issue #1280): its input gathered pass by pass, its output
     *         handed out pass by pass.
     *
     *  A frame-wide object (pObject::SpectralFrameWide) answers true and,
     *  in its CALC, with ``pass = CurrentSpectralPass()`` not null:
     *
     *  - ``!pass->emitting`` (the consume step, every pass of its stage):
     *    ``consume(*pass, channel, buffer)`` for each signal input. Nothing
     *    is sent.
     *  - ``pass->emitting`` (the emit step, every pass of the next stage):
     *    ``if (due())`` compute the whole frame from ``input(c)`` into
     *    ``output(c)`` (its DSP class on ``frame(*pass, ...)``, bins 0 to
     *    ``frame().count - 1``), then send ``slice(*pass, c)`` on each
     *    signal outlet.
     *
     *  ``due()`` is true once per frame: at the first emit after a consume.
     *  Bins at or past ``capacity()`` are not stored and come out 0. All
     *  storage is allocated in the constructor; consume, due, slice
     *  allocate nothing and take no lock. Render thread only.
     */
    class spectralFrameStore {
    public:
      /** @brief Frames of up to 4096 bins: a half spectrum of an 8192-point
       *         FFT. */
      static constexpr UInt DEFAULT_CAPACITY = 4096;
      /** @brief A full spectrum of ``~pfft``'s largest FFT. */
      static constexpr UInt MAXIMUM_CAPACITY = 1u << 16;

      /** @brief Room for @p inputs input and @p outputs output channels of
       *         @p capacity bins (clamped to 1 to ``MAXIMUM_CAPACITY``).
       *         Allocates. */
      explicit spectralFrameStore(UInt inputs = 1, UInt outputs = 1,
                                  UInt capacity = DEFAULT_CAPACITY)
        : bins(std::min(std::max(capacity, 1u), MAXIMUM_CAPACITY)),
          ins(inputs),
          outs(std::max(outputs, 1u)),
          in(static_cast<std::size_t>(inputs) * bins, 0.f),
          out(static_cast<std::size_t>(outs) * bins, 0.f) {
        slices.reserve(outs);
        for (UInt c = 0; c < outs; ++c) {
          slices.emplace_back(STANDARD_BUFFERSIZE);
          slices.back() = 0.f;
        }
      }

      UInt capacity() const {
        return bins;
      }

      /** @brief Stores @p values (the pass's ``count`` bins) at
       *         ``pass.firstBin`` of input @p channel; bins past the capacity
       *         are dropped, a short buffer reads as 0 past its end. */
      void consume(const spectralPass& pass, UInt channel, DSP::buffer& values) {
        pending = true;
        if (channel >= ins || pass.firstBin >= bins) return;
        const UInt n = std::min(pass.count, bins - pass.firstBin);
        const UInt have = std::min(n, values.getLength());
        Flt* to = input(channel) + pass.firstBin;
        std::copy(values.getPtr(), values.getPtr() + have, to);
        std::fill(to + have, to + n, 0.f);
      }

      /** @brief True at the first emit step after a consume: the moment to
       *         compute the frame. */
      bool due() {
        const bool now = pending;
        pending = false;
        return now;
      }

      /** @brief The whole frame as the DSP classes take it: bins 0 to
       *         ``min(pass.bins, capacity()) - 1``, over @p real /
       *         @p imaginary (the caller's choice of channels). */
      DSP::stftFrame frame(const spectralPass& pass, Flt* real = nullptr,
                           Flt* imaginary = nullptr) const {
        DSP::stftFrame f = pass.Frame(real, imaginary);
        f.firstBin = 0;
        f.count = std::min(pass.bins, bins);
        return f;
      }

      Flt* input(UInt channel) {
        return in.data() + static_cast<std::size_t>(channel) * bins;
      }
      Flt* output(UInt channel) {
        return out.data() + static_cast<std::size_t>(channel) * bins;
      }

      /** @brief The pass's bins of output @p channel, ``pass.count``
       *         samples: what the emit step sends. 0 past the capacity. */
      DSP::buffer& slice(const spectralPass& pass, UInt channel) {
        DSP::buffer& b = slices[std::min(channel, outs - 1)];
        const UInt count = std::min(pass.count, STANDARD_BUFFERSIZE);
        // Within the capacity set in the constructor: no allocation.
        if (b.getLength() != count) b.resize(count);
        Flt* to = b.getPtr();
        const UInt n = pass.firstBin < bins ? std::min(count, bins - pass.firstBin) : 0;
        if (channel < outs && n > 0) {
          const Flt* from = output(channel) + pass.firstBin;
          std::copy(from, from + n, to);
        } else {
          std::fill(to, to + n, 0.f);
        }
        std::fill(to + n, to + count, 0.f);
        return b;
      }

    private:
      UInt bins;
      UInt ins;
      UInt outs;
      std::vector<Flt> in;
      std::vector<Flt> out;
      std::vector<DSP::buffer> slices;
      bool pending = false;
    };

  } // namespace PATCHER
} // namespace YSE
