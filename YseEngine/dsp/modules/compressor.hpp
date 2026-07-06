/*
  ==============================================================================

    compressor.hpp
    Channel-strip dynamics compressor module (issue #163).

  ==============================================================================
*/

#ifndef COMPRESSOR_HPP_INCLUDED
#define COMPRESSOR_HPP_INCLUDED

#include "../../headers/defines.hpp"
#include "../../headers/types.hpp"
#include "../dspObject.hpp"
#include "../buffer.hpp"
#include <cstddef>

namespace YSE {
  namespace DSP {
    namespace MODULES {

      /** @brief Level-detector mode for the ``compressor``. */
      enum compressorDetector {
        DETECT_PEAK, ///< Track the instantaneous linked peak — fast, hits transients.
        DETECT_RMS, ///< Track a short mean-square window — smoother, loudness-like.
      };

      /**
       *  @brief Feed-forward dynamics compressor packaged as a chainable ``dspObject``.
       *
       *  The dynamics half of the channel strip (the parametric EQ is the tone
       *  half). A classic feed-forward design: a switchable peak/RMS detector
       *  drives a static compression curve (threshold + ratio), the resulting
       *  gain is smoothed by separate attack and release time constants, and a
       *  makeup gain restores level.
       *
       *  ### Stereo-linked detection (N-channel)
       *
       *  The detector and the gain it computes are **shared across every
       *  channel** — one sidechain for the whole buffer. Each sample's detector
       *  level is taken from all channels at once (the peak across them, or their
       *  summed mean-square for RMS), one gain-reduction value is derived, and
       *  that *same* gain is applied to every channel. This is what keeps a
       *  stereo (or N-channel) image stable: if the channels were each detected
       *  and compressed independently, a transient on one side would duck only
       *  that side and the stereo image would wobble/pump. Linking guarantees the
       *  relative balance between channels is preserved through gain changes.
       *
       *  A mono buffer is the degenerate single-channel case. The module holds no
       *  per-channel history (the gain is global), so it tolerates a changing
       *  channel count between calls for free (see the N-channel contract on
       *  ``dspObject::process``).
       *
       *  The wet/dry balance is the inherited ``impact()``; ``impact(1)`` (the
       *  default) is the fully-compressed insert. ``process`` allocates nothing
       *  in steady state.
       */
      class API compressor : public dspObject {
      public:
        compressor();
        virtual ~compressor() {};

        /** @brief Select the peak or RMS level detector. */
        compressor& detector(compressorDetector value);

        /** @brief Current detector mode. */
        compressorDetector detector();

        /** @brief Set the threshold in dBFS (clamped to [-60, 0]). Signal above
         *  the threshold is compressed; below it passes unchanged. */
        compressor& threshold(Flt db);

        /** @brief Current threshold in dBFS. */
        Flt threshold();

        /** @brief Set the compression ratio (clamped to [1, 20]). 1 is no
         *  compression; 4 means 4 dB in above threshold yields 1 dB out. */
        compressor& ratio(Flt value);

        /** @brief Current compression ratio. */
        Flt ratio();

        /** @brief Set the attack time in milliseconds (clamped to a sane range) —
         *  how fast gain reduction engages when the signal rises. */
        compressor& attack(Flt ms);

        /** @brief Current attack time in milliseconds. */
        Flt attack();

        /** @brief Set the release time in milliseconds (clamped to a sane range) —
         *  how fast gain recovers when the signal falls. */
        compressor& release(Flt ms);

        /** @brief Current release time in milliseconds. */
        Flt release();

        /** @brief Set the makeup gain in dB (clamped to [-24, 24]) — a fixed gain
         *  applied after compression to restore level. */
        compressor& makeup(Flt db);

        /** @brief Current makeup gain in dB. */
        Flt makeup();

        /** @brief Current gain reduction being applied, in dB (<= 0). Useful for
         *  metering; reflects the last processed sample. */
        Flt gainReductionDb();

        /** @brief dspObject lifecycle hook. */
        virtual void create();

        /** @brief dspObject audio-thread entry point. */
        virtual void process(MULTICHANNELBUFFER& buffer);

      private:
        aFlt parmThreshold; // dBFS
        aFlt parmRatio;
        aFlt parmAttack; // ms
        aFlt parmRelease; // ms
        aFlt parmMakeup; // dB
        aInt parmDetector; // compressorDetector

        // Audio-thread-only state (shared across all channels — the stereo link).
        Flt gain; // current smoothed linear gain applied to every channel
        Flt msEnv; // RMS mean-square follower state
        Flt rmsCoef; // one-pole coefficient for the RMS window (from SR)

        aFlt reductionDb; // last gain reduction (dB, <= 0) for metering

        DSP::buffer gainBuf; // per-sample applied linear gain (shared across channels)
        DSP::buffer wet; // per-block compressed scratch for the wet/dry mix
        std::size_t blockLength; // last seen block length (scratch sizing)
      };

    } // namespace MODULES
  } // namespace DSP
} // namespace YSE

#endif // COMPRESSOR_HPP_INCLUDED
