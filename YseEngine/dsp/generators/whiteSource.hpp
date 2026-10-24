/*
  ==============================================================================

    whiteSource.hpp
    The white-noise generator shared by the noise-based generators.

  ==============================================================================
*/

#ifndef YSE_DSP_GENERATORS_WHITESOURCE_HPP
#define YSE_DSP_GENERATORS_WHITESOURCE_HPP

#include <cstdint>
#include "../../headers/types.hpp"

namespace YSE {
  namespace DSP {
    namespace detail {

      /** @brief The seed ``DSP::noise`` starts from, shared by the generators
       *         built on the same source. */
      constexpr std::uint32_t WHITE_DEFAULT_SEED = 307u * 1319u;

      /**
       *  @brief The next white sample in [-1, 1) and advance @p state.
       *
       *  Same generator and mapping as ``DSP::noise`` (dsp/oscillators.cpp),
       *  in unsigned arithmetic so the wrap-around is defined: the low 31
       *  bits, centred. Used by ``DSP::pinkNoise`` and
       *  ``DSP::bandLimitedRandom``. Allocation-free; audio-thread safe.
       */
      inline Dbl NextWhite(std::uint32_t& state) {
        const auto bits = static_cast<std::int32_t>(state & 0x7fffffffu);
        const Dbl sample = static_cast<Dbl>(bits - 0x40000000) * (1.0 / 0x40000000);
        state = state * 435898247u + 382842987u;
        return sample;
      }

    } // namespace detail
  } // namespace DSP
} // namespace YSE

#endif // YSE_DSP_GENERATORS_WHITESOURCE_HPP
