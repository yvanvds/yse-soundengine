/*
  ==============================================================================

    fmVoice.cpp
    DX7-class 6-operator FM voice — see fmVoice.hpp and issue #176.

  ==============================================================================
*/

#include "fmVoice.hpp"

#include <cmath>
#include <cstring>
#include <mutex>

#include "../math.hpp"
#include "../../headers/constants.hpp"

#include "msfa/synth.h"
#include "msfa/sin.h"
#include "msfa/exp2.h"
#include "msfa/freqlut.h"
#include "msfa/pitchenv.h"
#include "msfa/lfo.h"
#include "msfa/controllers.h"
#include "msfa/dx7note.h"

namespace YSE {
  namespace SYNTH {

    namespace msfa = ::YSE::DSP::msfa;

    // Convert the FM core's fixed-point output to float. MSFA maps a note's
    // int32 sample to a signed 16-bit value by `(sample >> 4) >> 9` with full
    // scale at 1<<24, i.e. 1<<28 in the raw domain maps to unity.
    static const Flt kOutputScale = 1.0f / static_cast<Flt>(1 << 28);

    // Raw-domain peak below which a releasing voice is considered silent. The
    // envelopes decay toward zero after key-up, so a couple of consecutive
    // quiet blocks means the release tail has finished.
    static const int32_t kSilenceThreshold = 512;
    static const int kQuietBlocksToStop = 2;

    // ─── one-time, rate-dependent global table init ──────────────────────────
    // The MSFA sine / exp2 tables are rate-independent; the freqlut, LFO and
    // pitch-envelope tables depend on the sample rate. Built once per rate on
    // the setup thread (never the audio thread) — voices are cloned there after
    // the device has locked SAMPLERATE. Guarded by a mutex because the prototype
    // may be constructed on a different (non-audio) thread than the clones.
    static void ensureTablesInited() {
      static std::mutex mutex;
      static double initedRate = 0.0;
      std::lock_guard<std::mutex> lock(mutex);
      const double sr = static_cast<double>(SAMPLERATE);
      if (initedRate == sr) return;
      msfa::Sin::init();
      msfa::Exp2::init();
      msfa::Freqlut::init(sr);
      msfa::Lfo::init(sr);
      msfa::PitchEnv::init(sr);
      initedRate = sr;
    }

    // ─── PIMPL audio-thread state ────────────────────────────────────────────
    struct fmVoiceState {
      msfa::Dx7Note note;
      msfa::Lfo lfo;
      msfa::Controllers ctrls;
      int32_t scratch[64];
      int scratchPos = 64; // >= 64 forces a refill on the first sample
      enum Phase { IDLE, PLAYING, RELEASING } phase = IDLE;
      int quietBlocks = 0;
      SOUND_STATUS settleStatus = SS_STOPPED;

      fmVoiceState() {
        for (int i = 0; i < 129; i++)
          ctrls.values_[i] = 0;
        ctrls.values_[msfa::kControllerPitch] = 0x2000; // pitch-wheel centre
        for (int i = 0; i < 64; i++)
          scratch[i] = 0;
      }
    };

    // ─── fmVoice ─────────────────────────────────────────────────────────────

    fmVoice::fmVoice(int outputChannels)
      : dspVoice(outputChannels),
        params(std::make_shared<fmPatch>(fmPatch::sine())),
        state(std::make_unique<fmVoiceState>()) {
      ensureTablesInited();
    }

    fmVoice::fmVoice(const fmVoice& other)
      : dspVoice(other),
        params(other.params), // share the patch — all voices track one sound
        state(std::make_unique<fmVoiceState>()) {
      ensureTablesInited();
    }

    fmVoice::~fmVoice() = default;

    dspVoice* fmVoice::clone() {
      return new fmVoice(*this);
    }

    void fmVoice::startNote() {
      const fmPatch& p = *params;

      // MIDI note number: the base stores frequency as Hz, so round-trip it
      // back to a note number and apply the patch transpose (24 = no shift).
      int midinote = static_cast<int>(std::lround(DSP::FreqToMidi(getFrequency())));
      midinote += static_cast<int>(p.transpose) - 24;
      if (midinote < 0) midinote = 0;
      if (midinote > 127) midinote = 127;

      int velocity = static_cast<int>(std::lround(getVelocity() * 127.f));
      if (velocity < 0) velocity = 0;
      if (velocity > 127) velocity = 127;

      char unpacked[156];
      p.toUnpacked(unpacked);
      state->note.init(unpacked, midinote, velocity);

      // LFO parameter block: {speed, delay, pmd, amd, sync, waveform}.
      char lfoParams[6];
      lfoParams[0] = static_cast<char>(p.lfoSpeed);
      lfoParams[1] = static_cast<char>(p.lfoDelay);
      lfoParams[2] = static_cast<char>(p.lfoPitchModDepth);
      lfoParams[3] = static_cast<char>(p.lfoAmpModDepth);
      lfoParams[4] = static_cast<char>(p.lfoSync);
      lfoParams[5] = static_cast<char>(p.lfoWaveform);
      state->lfo.reset(lfoParams);
      state->lfo.keydown();

      state->scratchPos = 64; // force a fresh core block
    }

    void fmVoice::process(SOUND_STATUS& intent) {
      if (samples.empty()) return; // degenerate 0-channel voice
      const UInt n = samples[0].getLength();
      fmVoiceState& s = *state;

      // ---- lifecycle / gates -------------------------------------------------
      if (intent == SS_WANTSTOPLAY || intent == SS_WANTSTORESTART) {
        startNote();
        s.phase = fmVoiceState::PLAYING;
        intent = SS_PLAYING;
      } else if (intent == SS_WANTSTOSTOP || intent == SS_WANTSTOPAUSE) {
        const SOUND_STATUS settled = (intent == SS_WANTSTOPAUSE) ? SS_PAUSED : SS_STOPPED;
        if (s.phase == fmVoiceState::IDLE) {
          // Released before it ever attacked — nothing is sounding.
          intent = settled;
          for (UInt ch = 0; ch < samples.size(); ch++)
            samples[ch] = 0.f;
          return;
        }
        if (s.phase != fmVoiceState::RELEASING) {
          s.note.keyup();
          s.phase = fmVoiceState::RELEASING;
          s.settleStatus = settled;
          s.quietBlocks = 0;
        }
      } else if (intent == SS_PLAYING || intent == SS_PLAYING_FULL_VOLUME) {
        // keep rendering
      } else {
        // SS_STOPPED / SS_PAUSED: emit silence.
        for (UInt ch = 0; ch < samples.size(); ch++)
          samples[ch] = 0.f;
        return;
      }

      // Deliver the channel pitch-wheel (±1 → ±8192 around the 0x2000 centre;
      // the core maps this to a hardcoded ±3-semitone bend).
      int pb = 0x2000 + static_cast<int>(std::lround(getPitchWheel() * 8192.f));
      if (pb < 0) pb = 0;
      if (pb > 16383) pb = 16383;
      s.ctrls.values_[msfa::kControllerPitch] = pb;

      // ---- per-sample render -------------------------------------------------
      Flt* out = samples[0].getPtr();
      int32_t blockMax = 0;
      for (UInt i = 0; i < n; i++) {
        if (s.scratchPos >= 64) {
          int32_t lfoVal = s.lfo.getsample();
          int32_t lfoDelay = s.lfo.getdelay();
          for (int k = 0; k < 64; k++)
            s.scratch[k] = 0;
          s.note.compute(s.scratch, lfoVal, lfoDelay, &s.ctrls);
          s.scratchPos = 0;
        }
        int32_t raw = s.scratch[s.scratchPos++];
        int32_t mag = raw < 0 ? -raw : raw;
        if (mag > blockMax) blockMax = mag;
        out[i] = static_cast<Flt>(raw) * kOutputScale;
      }

      // Mirror the mono voice signal to any extra output channels.
      for (UInt ch = 1; ch < samples.size(); ch++)
        samples[ch] = samples[0];

      // Release tail finished (envelopes decayed to silence) — free the slot.
      if (s.phase == fmVoiceState::RELEASING) {
        if (blockMax < kSilenceThreshold) {
          if (++s.quietBlocks >= kQuietBlocksToStop) {
            intent = s.settleStatus;
            s.phase = fmVoiceState::IDLE;
          }
        } else {
          s.quietBlocks = 0;
        }
      }
    }

  } // namespace SYNTH
} // namespace YSE
