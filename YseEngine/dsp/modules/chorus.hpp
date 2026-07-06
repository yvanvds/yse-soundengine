/*
  ==============================================================================

    chorus.hpp
    Multichannel chorus/flanger module (issue #161).

  ==============================================================================
*/

#ifndef CHORUS_HPP_INCLUDED
#define CHORUS_HPP_INCLUDED

#include "../../headers/defines.hpp"
#include "../../headers/types.hpp"
#include "../dspObject.hpp"
#include "../buffer.hpp"
#include "../perChannel.hpp"
#include <cstddef>
#include <vector>

namespace YSE {
  namespace DSP {
    namespace MODULES {

      /** @brief Operating mode for the ``chorus`` module. */
      enum chorusMode {
        MODE_CHORUS, ///< Longer base delay, wide slow sweep — thickening/detune.
        MODE_FLANGER, ///< Short base delay, feedback — sweeping comb notches.
      };

      /**
       *  @brief Chorus / flanger effect packaged as a chainable ``dspObject``.
       *
       *  Chorus and flanger are the same modulated-delay topology with different
       *  ranges and feedback, so this is one module with a ``mode()`` switch. An
       *  internal LFO sweeps a per-channel fractional (linearly interpolated)
       *  delay line; the sweep is click-free by construction — the delay time is
       *  a continuous signal and the read is fractional, so no read-pointer step
       *  is ever taken. ``MODE_CHORUS`` uses a longer base delay and no feedback
       *  by default (thickening / detune); ``MODE_FLANGER`` uses a short base
       *  delay and a feedback path (the classic resonant jet-sweep comb).
       *
       *  Processes every channel of the multichannel buffer independently, each
       *  with its own delay line and LFO phase (see the N-channel contract on
       *  ``dspObject::process``). The LFO is shared — one sweep for the whole
       *  buffer — but ``spread()`` fans a per-channel phase offset across the
       *  channels for stereo width; at ``spread(0)`` every channel is in phase
       *  (and a mono buffer is the degenerate single-channel case).
       *
       *  The wet/dry balance is the inherited ``impact()``: ``impact(0.5)`` is a
       *  natural insert mix, ``impact(1)`` is fully wet. All buffers are sized on
       *  the ``create()`` / channel-count-change path; ``process`` allocates
       *  nothing in steady state.
       */
      class API chorus : public dspObject {
      public:
        chorus();
        virtual ~chorus() {};

        /** @brief Select chorus or flanger topology. */
        chorus& mode(chorusMode value);

        /** @brief Current mode. */
        chorusMode mode();

        /** @brief Set the LFO sweep rate in Hz (clamped to a sane range). */
        chorus& rate(Flt hz);

        /** @brief Current LFO rate in Hz. */
        Flt rate();

        /** @brief Set the modulation depth in [0, 1] — scales the sweep width. */
        chorus& depth(Flt value);

        /** @brief Current modulation depth. */
        Flt depth();

        /** @brief Set the feedback amount in [-0.95, 0.95]. Drives the resonant
         *  flanger comb; negative values invert the fed-back signal (the hollow
         *  "through-zero"-style comb). Typically 0 for chorus. */
        chorus& feedback(Flt value);

        /** @brief Current feedback amount. */
        Flt feedback();

        /** @brief Set the stereo spread in [0, 1] — the per-channel LFO phase
         *  offset. 0 keeps every channel in phase; 1 spreads the channels evenly
         *  around a full LFO cycle for maximum width. */
        chorus& spread(Flt value);

        /** @brief Current stereo spread. */
        Flt spread();

        /** @brief dspObject lifecycle hook. */
        virtual void create();

        /** @brief dspObject audio-thread entry point. */
        virtual void process(MULTICHANNELBUFFER& buffer);

      private:
        aFlt parmRate; // LFO rate, Hz
        aFlt parmDepth; // modulation depth, [0, 1]
        aFlt parmFeedback; // feedback amount, [-0.95, 0.95]
        aFlt parmSpread; // stereo spread, [0, 1]
        aInt parmMode; // chorusMode

        // Audio-thread-only state.
        Flt lfoCursor; // shared LFO phase carried across blocks (radians)
        Flt delaySmoothCoef; // one-pole coef for per-sample delay smoothing
        std::size_t lineSize; // per-channel delay-line length (samples)
        std::size_t blockLength; // last seen block length (scratch sizing)

        /** @brief One channel's modulated-delay state. */
        struct voice {
          std::vector<Flt> line; // circular fractional delay line (owned)
          std::size_t writePos; // next write index into line
          Flt smoothedDelay; // last smoothed delay time (ms)
          Bool primed; // whether smoothedDelay has been seeded yet
          voice();
        };

        perChannel<voice> voices;

        // Per-block scratch, shared across channels (sized on length change).
        DSP::buffer phaseBuf; // shared LFO phase, one value per sample (radians)
        DSP::buffer wet; // this block's wet (delayed) signal

        Flt baseDelayMs() const; // mode-dependent base delay
        Flt sweepDelayMs() const; // mode-dependent sweep width (pre-depth)
      };

    } // namespace MODULES
  } // namespace DSP
} // namespace YSE

#endif // CHORUS_HPP_INCLUDED
