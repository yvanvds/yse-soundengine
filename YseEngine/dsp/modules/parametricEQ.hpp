/*
  ==============================================================================

    parametricEQ.hpp
    Channel-strip parametric EQ module (issue #163).

  ==============================================================================
*/

#ifndef PARAMETRICEQ_HPP_INCLUDED
#define PARAMETRICEQ_HPP_INCLUDED

#include "../../headers/defines.hpp"
#include "../../headers/types.hpp"
#include "../dspObject.hpp"
#include "../buffer.hpp"
#include "../perChannel.hpp"
#include <cstddef>

namespace YSE {
  namespace DSP {
    namespace MODULES {

      /** @brief The four fixed bands of the ``parametricEQ``. */
      enum eqBand {
        EQ_LOW_SHELF, ///< Low shelf — boosts/cuts everything below its corner.
        EQ_PEAK_1, ///< Lower peaking (bell) band.
        EQ_PEAK_2, ///< Upper peaking (bell) band.
        EQ_HIGH_SHELF, ///< High shelf — boosts/cuts everything above its corner.
        EQ_BAND_COUNT, ///< Number of bands (sentinel).
      };

      /**
       *  @brief Channel-strip parametric EQ packaged as a chainable ``dspObject``.
       *
       *  Four cascaded biquad bands — a low shelf, two peaking (bell) bands, and
       *  a high shelf — each with its own frequency, gain (dB), and Q. This is
       *  the corrective/tone-shaping half of the channel strip (the compressor is
       *  the dynamics half); together they make the channel insert chain a usable
       *  mixing tool.
       *
       *  The bands are the standard RBJ "Audio EQ Cookbook" biquads (the same
       *  transfer functions the engine's ``biQuad`` primitive implements). Each
       *  band's five coefficients are shared across all channels and recomputed
       *  only when one of its parameters changes (or the sample rate changes),
       *  gated by a dirty flag — never per sample and never allocating. The
       *  recompute is a bounded handful of transcendentals for at most four bands
       *  and runs at the very top of ``process`` only on a block where a
       *  parameter actually moved, which keeps the audio path allocation- and
       *  lock-free.
       *
       *  ### N-channel behaviour
       *
       *  Every channel is filtered independently: the coefficients are shared but
       *  each channel owns its biquad delay memory (via ``perChannel``), so no
       *  filter state bleeds between channels (see the N-channel contract on
       *  ``dspObject::process``). A mono buffer is the degenerate single-channel
       *  case, and the module tolerates a changing channel count between calls
       *  (the device-restart resize path is the only place per-channel state
       *  allocates).
       *
       *  The wet/dry balance is the inherited ``impact()``; ``impact(1)`` (the
       *  default) is the natural fully-processed insert. All state is sized on
       *  the ``create()`` / channel-count-change path; ``process`` allocates
       *  nothing in steady state.
       */
      class API parametricEQ : public dspObject {
      public:
        parametricEQ();
        virtual ~parametricEQ() {};

        /** @brief Set a band's centre/corner frequency in Hz (clamped to a sane
         *  audio range). */
        parametricEQ& frequency(eqBand band, Flt hz);

        /** @brief Current centre/corner frequency of a band, in Hz. */
        Flt frequency(eqBand band);

        /** @brief Set a band's gain in dB (clamped to +/-24 dB). Positive boosts,
         *  negative cuts; 0 leaves the band flat (a bypass for that band). */
        parametricEQ& gain(eqBand band, Flt db);

        /** @brief Current gain of a band, in dB. */
        Flt gain(eqBand band);

        /** @brief Set a band's Q (clamped to a sane range). For the peaking bands
         *  Q is the bell width; for the shelves it shapes the corner steepness. */
        parametricEQ& q(eqBand band, Flt value);

        /** @brief Current Q of a band. */
        Flt q(eqBand band);

        /** @brief dspObject lifecycle hook. */
        virtual void create();

        /** @brief dspObject audio-thread entry point. */
        virtual void process(MULTICHANNELBUFFER& buffer);

      private:
        // Per-band user parameters (control-thread writes, audio-thread reads).
        aFlt parmFreq[EQ_BAND_COUNT];
        aFlt parmGain[EQ_BAND_COUNT]; // dB
        aFlt parmQ[EQ_BAND_COUNT];

        // Set whenever a parameter changes; the audio thread recomputes the
        // coefficient set and clears it. Also forced by a sample-rate change.
        std::atomic<bool> dirty;

        /** @brief One band's shared biquad coefficients (Direct Form I,
         *  normalised so a0 == 1). */
        struct Coeffs {
          Flt b0, b1, b2, a1, a2;
          Coeffs();
        };
        Coeffs coeffs[EQ_BAND_COUNT]; // audio-thread-only, recomputed on dirty

        /** @brief One channel's per-band biquad delay memory (Direct Form I). */
        struct BandState {
          Flt x1, x2, y1, y2;
          BandState();
        };
        struct ChannelState {
          BandState bands[EQ_BAND_COUNT];
        };
        perChannel<ChannelState> channels;

        UInt builtRate; // SAMPLERATE the coefficients were computed for (0 = none)

        DSP::buffer wet; // per-block wet scratch
        std::size_t blockLength; // last seen block length (scratch sizing)

        void computeBand(eqBand band); // fill coeffs[band] from its parameters
      };

    } // namespace MODULES
  } // namespace DSP
} // namespace YSE

#endif // PARAMETRICEQ_HPP_INCLUDED
