/*
  ==============================================================================

    sineVoice.cpp
    Reference sine + ADSR voice — see sineVoice.hpp and
    docs/design/synth_core.md §3.

  ==============================================================================
*/

#include "sineVoice.hpp"

namespace YSE {
  namespace SYNTH {

    // Length of the flat sustain plateau built into the envelope, in seconds.
    // The ADSRenvelope holds sustain by looping this constant-value region
    // (ADSRenvelope::RESUME), so it only needs to be long enough to contain a
    // handful of samples; the exact value is inaudible.
    static const Flt kSustainHold = 0.05f;

    // Smallest legal segment duration. Two envelope breakpoints sharing the
    // same time would make ADSRenvelope::generate() divide by a zero-length
    // segment, so every duration is clamped to this floor.
    static const Flt kMinSegment = 1e-4f;

    static inline Flt clampMin(Flt v, Flt lo) {
      return v < lo ? lo : v;
    }

    sineVoice::sineVoice(int outputChannels)
      : dspVoice(outputChannels),
        _attack(0.01f),
        _decay(0.05f),
        _sustainLevel(0.7f),
        _release(0.1f),
        phase(IDLE) {
      buildEnvelope();
    }

    sineVoice::sineVoice(const sineVoice& other)
      : dspVoice(other),
        _attack(other._attack),
        _decay(other._decay),
        _sustainLevel(other._sustainLevel),
        _release(other._release),
        phase(IDLE) {
      // Build a fresh envelope rather than copy the prototype's: ADSRenvelope
      // holds raw pointers into its own storage, so a copy would alias the
      // original. Rebuilding keeps a clone's state fully independent.
      buildEnvelope();
    }

    void sineVoice::buildEnvelope() {
      const Flt a = clampMin(_attack, kMinSegment);
      const Flt d = clampMin(_decay, kMinSegment);
      const Flt r = clampMin(_release, kMinSegment);
      Flt s = _sustainLevel;
      if (s < 0.f) s = 0.f;
      if (s > 1.f) s = 1.f;

      // Fresh envelope on the heap: each rebuild replaces the previous one, so
      // breakpoints never accumulate and the raw pointers ADSRenvelope keeps
      // stay valid for exactly this instance's lifetime. Off the audio thread.
      auto fresh = std::make_unique<DSP::ADSRenvelope>();
      fresh->addPoint({0.f, 0.f, 1.f}); // start silent
      fresh->addPoint({a, 1.f, 1.f}); // attack -> peak
      fresh->addPoint({a + d, s, 1.f, /*loopStart*/ true}); // decay -> sustain
      fresh->addPoint({a + d + kSustainHold, s, 1.f, // sustain plateau
                       /*loopStart*/ false, /*loopEnd*/ true});
      fresh->addPoint({a + d + kSustainHold + r, 0.f, 1.f}); // release -> silent
      fresh->generate();
      env = std::move(fresh);
    }

    sineVoice& sineVoice::attack(Flt seconds) {
      _attack = seconds;
      buildEnvelope();
      return *this;
    }

    sineVoice& sineVoice::decay(Flt seconds) {
      _decay = seconds;
      buildEnvelope();
      return *this;
    }

    sineVoice& sineVoice::sustain(Flt level) {
      _sustainLevel = level;
      buildEnvelope();
      return *this;
    }

    sineVoice& sineVoice::release(Flt seconds) {
      _release = seconds;
      buildEnvelope();
      return *this;
    }

    dspVoice* sineVoice::clone() {
      return new sineVoice(*this);
    }

    void sineVoice::process(SOUND_STATUS& intent) {
      DSP::ADSRenvelope::STATE estate;

      if (intent == SS_WANTSTOPLAY || intent == SS_WANTSTORESTART) {
        // Note start (or retrigger): restart the oscillator and the envelope.
        osc.reset();
        estate = DSP::ADSRenvelope::ATTACK;
        phase = PLAYING;
        intent = SS_PLAYING;
      } else if (intent == SS_WANTSTOSTOP || intent == SS_WANTSTOPAUSE) {
        // Note off: take the release transition once, then let it tail out.
        if (phase != RELEASING) {
          estate = DSP::ADSRenvelope::RELEASE;
          phase = RELEASING;
        } else {
          estate = DSP::ADSRenvelope::RESUME;
        }
      } else if (intent == SS_PLAYING || intent == SS_PLAYING_FULL_VOLUME) {
        // Sustaining: RESUME loops the envelope's sustain plateau.
        estate = DSP::ADSRenvelope::RESUME;
      } else {
        // SS_STOPPED / SS_PAUSED: emit silence.
        for (UInt i = 0; i < samples.size(); i++)
          samples[i] = 0.f;
        return;
      }

      const Flt freq = getFrequency();
      const Flt vel = getVelocity();

      DSP::buffer& sig = osc(freq);
      DSP::buffer& amp = (*env)(estate);

      for (UInt i = 0; i < samples.size(); i++) {
        samples[i] = sig;
        samples[i] *= amp;
        samples[i] *= vel;
      }

      // The release tail has fully decayed — hand the slot back to the engine.
      if (phase == RELEASING && env->isAtEnd()) {
        intent = SS_STOPPED;
        phase = IDLE;
      }
    }

  } // namespace SYNTH
} // namespace YSE
