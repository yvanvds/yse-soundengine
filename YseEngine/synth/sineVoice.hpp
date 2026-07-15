/*
  ==============================================================================

    sineVoice.hpp
    Reference synth voice: one sine oscillator shaped by one ADSR envelope.

    The minimal legal SYNTH::dspVoice implementation from issue #152 — it proves
    the voice contract (intent-driven lifecycle, clone() prototype, atomic
    note inputs) and is the voice every downstream synth test builds on. See
    docs/design/synth_core.md §3.

  ==============================================================================
*/

#ifndef YSE_SYNTH_SINEVOICE_HPP
#define YSE_SYNTH_SINEVOICE_HPP

#include <memory>
#include "dspVoice.hpp"
#include "../dsp/oscillators.hpp"
#include "../dsp/ADSRenvelope.hpp"

namespace YSE {
  namespace SYNTH {

    /**
     *  @brief Reference voice — a sine oscillator gated by an ADSR envelope.
     *
     *  Pitched from ``getFrequency()``, scaled by ``getVelocity()`` and shaped
     *  by a classic attack / decay / sustain / release envelope keyed off the
     *  ``SOUND_STATUS`` intent:
     *
     *  - ``SS_WANTSTOPLAY`` restarts the oscillator phase and the envelope
     *    attack, then settles the intent to ``SS_PLAYING``.
     *  - ``SS_PLAYING`` holds the sustain level.
     *  - ``SS_WANTSTOSTOP`` enters the release tail; when the tail reaches
     *    zero the voice settles the intent to ``SS_STOPPED``.
     *
     *  Everything is allocated up front (in the constructor and the envelope
     *  setters, both off the audio thread) so ``process()`` and ``clone()``'s
     *  copy stay allocation-clean on their respective threads.
     */
    class API sineVoice : public dspVoice {
    public:
      /** @brief Construct a mono (or ``outputChannels``-wide) sine voice with a default ADSR. */
      sineVoice(int outputChannels = 1);

      // ---- chainable envelope setters (times in seconds; sustain in [0,1]) ----

      /** @brief Set the attack time in seconds. */
      sineVoice& attack(Flt seconds);
      /** @brief Set the decay time in seconds. */
      sineVoice& decay(Flt seconds);
      /** @brief Set the sustain level in [0, 1]. */
      sineVoice& sustain(Flt level);
      /** @brief Set the release time in seconds. */
      sineVoice& release(Flt seconds);

      /** @brief Current attack time in seconds. */
      Flt attack() const {
        return _attack;
      }
      /** @brief Current decay time in seconds. */
      Flt decay() const {
        return _decay;
      }
      /** @brief Current sustain level in [0, 1]. */
      Flt sustain() const {
        return _sustainLevel;
      }
      /** @brief Current release time in seconds. */
      Flt release() const {
        return _release;
      }

      // ---- dspVoice contract -------------------------------------------------

      /** @brief Render one block, honouring and settling ``intent``. Audio-thread only. */
      void process(SOUND_STATUS& intent) override;

      /** @brief Return a new, independently-allocated copy of this voice. Setup-thread only. */
      dspVoice* clone() override;

    protected:
      /** @brief Copy-construct an independent voice (rebuilds its own envelope). */
      sineVoice(const sineVoice& other);

    private:
      // Where the voice is in its own attack/sustain/release arc. Separate from
      // the SOUND_STATUS intent so a note-off knows whether the release
      // transition has already been taken.
      enum Phase { IDLE, PLAYING, RELEASING };

      // (Re)build the ADSR envelope from the current scalar parameters. Called
      // from the constructors and every setter — never from process().
      void buildEnvelope();

      Flt _attack;
      Flt _decay;
      Flt _sustainLevel;
      Flt _release;

      DSP::sine osc;
      std::unique_ptr<DSP::ADSRenvelope> env;
      Phase phase;
    };

  } // namespace SYNTH
} // namespace YSE

#endif // YSE_SYNTH_SINEVOICE_HPP
