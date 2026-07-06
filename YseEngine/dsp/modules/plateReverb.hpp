/*
  ==============================================================================

    plateReverb.hpp
    Dattorro plate reverb module (issue #162).

  ==============================================================================
*/

#ifndef PLATEREVERB_HPP_INCLUDED
#define PLATEREVERB_HPP_INCLUDED

#include "../../headers/defines.hpp"
#include "../../headers/types.hpp"
#include "../dspObject.hpp"
#include "../buffer.hpp"
#include <cstddef>
#include <vector>

namespace YSE {
  namespace DSP {
    namespace MODULES {

      /**
       *  @brief Insert/return-grade plate reverb packaged as a chainable ``dspObject``.
       *
       *  Implements the classic Dattorro plate topology ("Effect Design, Part 1",
       *  JAES 1997): a four-allpass input diffuser feeding a cross-coupled
       *  figure-eight tank of two symmetric halves, each a modulated allpass, a
       *  long delay, a damping low-pass, a second allpass, and a final delay. The
       *  stereo output is read from seven fixed taps across the tank nodes, giving
       *  the smooth, dense, slightly shimmering plate character.
       *
       *  This is distinct from the engine's existing global *spatial* reverb
       *  (``INTERNAL::reverbDSP``, welded to the listener/zone system): the plate
       *  is a plain effect you drop on a channel insert or a send return.
       *
       *  ### N-channel behaviour
       *
       *  The plate is a genuinely stereo device — its figure-eight tank
       *  cross-couples its two halves — so it is *not* a per-channel fan-out.
       *  Instead the whole multichannel buffer is downmixed to a mono tank input
       *  (the average of all input channels), the single stereo tank runs, and the
       *  two wet outputs are distributed back across the channels:
       *
       *  - **1 channel (mono):** the two tank outputs are averaged into the one
       *    channel.
       *  - **2 channels (stereo):** left tank output to channel 0, right to
       *    channel 1.
       *  - **N > 2:** even channels receive the left tank output, odd channels the
       *    right (a plain, predictable spread).
       *
       *  Every input channel contributes to the tank and every output channel
       *  receives wet signal, and the module tolerates a changing channel count
       *  between calls (see the N-channel contract on ``dspObject::process``). The
       *  tank buffers are sized once from the engine sample rate, so a device
       *  restart at a new channel count allocates nothing.
       *
       *  The wet/dry balance is the inherited ``impact()``: ``impact(0.25)`` is a
       *  natural insert mix, ``impact(1)`` is fully wet (send-return use). All
       *  buffers are sized on the ``create()`` / sample-rate-change path;
       *  ``process`` allocates nothing in steady state. Every delay, allpass, and
       *  output-tap length is derived from ``SAMPLERATE`` (relative to the paper's
       *  29761 Hz reference), so the reverb sounds the same at any rate.
       */
      class API plateReverb : public dspObject {
      public:
        plateReverb();
        virtual ~plateReverb() {};

        /** @brief Set the tank decay in [0, 0.98]. Higher values recirculate the
         *  tank longer, lengthening the reverb tail (RT60). */
        plateReverb& decay(Flt value);

        /** @brief Current tank decay. */
        Flt decay();

        /** @brief Set the damping cut-off in Hz — a low-pass inside each tank
         *  half. Lower values shed high-frequency energy faster, so the tail
         *  darkens as it decays. */
        plateReverb& damping(Flt hz);

        /** @brief Current damping cut-off in Hz. */
        Flt damping();

        /** @brief Set the pre-delay in milliseconds (clamped to the module's
         *  maximum). Offsets the onset of the reverb from the dry signal. */
        plateReverb& preDelay(Flt ms);

        /** @brief Current pre-delay in milliseconds. */
        Flt preDelay();

        /** @brief dspObject lifecycle hook. */
        virtual void create();

        /** @brief dspObject audio-thread entry point. */
        virtual void process(MULTICHANNELBUFFER& buffer);

      private:
        aFlt parmDecay; // tank decay, [0, 0.98]
        aFlt parmDamping; // damping low-pass cut-off, Hz
        aFlt parmPreDelay; // pre-delay, milliseconds

        // ─── Audio-thread-only DSP building blocks (all owned buffers) ───

        /** @brief Fixed-length delay line with node tapping for output reads. */
        struct DelayLine {
          std::vector<Flt> buf;
          std::size_t pos; // next write index
          void init(std::size_t len);
          Flt process(Flt x); // push x, return the sample len taps ago
          Flt tap(std::size_t k) const; // read k samples before the last write
          std::size_t length() const {
            return buf.size();
          }
        };

        /** @brief Schroeder allpass with node tapping. */
        struct Allpass {
          std::vector<Flt> buf;
          std::size_t pos;
          void init(std::size_t len);
          Flt process(Flt x, Flt g);
          Flt tap(std::size_t k) const;
          std::size_t length() const {
            return buf.size();
          }
        };

        /** @brief Allpass whose delay is modulated by a fractional excursion —
         *  the plate's tank chorusing. Read is linearly interpolated. */
        struct ModAllpass {
          std::vector<Flt> buf;
          std::size_t pos;
          Flt baseDelay; // nominal delay in samples
          void init(std::size_t len, Flt base);
          Flt process(Flt x, Flt g, Flt delaySamps); // fractional delay
        };

        // Input diffusers (four series allpasses).
        Allpass diff1, diff2, diff3, diff4;

        // Pre-delay line (read at a variable offset).
        DelayLine preLine;

        // Left tank half.
        ModAllpass apL1;
        DelayLine delL1;
        Allpass apL2;
        DelayLine delL2;
        Flt dampL; // damping low-pass state

        // Right tank half.
        ModAllpass apR1;
        DelayLine delR1;
        Allpass apR2;
        DelayLine delR2;
        Flt dampR; // damping low-pass state

        // Cross-coupling feedback (previous sample's tank outputs).
        Flt fbL, fbR;

        Flt lfoPhase; // tank modulation LFO phase (radians)
        Flt modExcursion; // modulation excursion in samples (from SR)
        Flt modInc; // per-sample LFO phase increment (from SR)

        // Output tap offsets (samples), pre-scaled from the reference rate.
        std::size_t tapL[7];
        std::size_t tapR[7];

        UInt builtRate; // SAMPLERATE the buffers were sized for (0 = unbuilt)
        std::size_t blockLength; // last seen block length (scratch sizing)

        DSP::buffer wetL, wetR; // per-block wet scratch

        void build(); // size every buffer/tap from the current SAMPLERATE
      };

    } // namespace MODULES
  } // namespace DSP
} // namespace YSE

#endif // PLATEREVERB_HPP_INCLUDED
