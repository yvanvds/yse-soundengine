/*
  ==============================================================================

    vaVoice.cpp
    Virtual-analog + wavetable voice — see vaVoice.hpp and issue #175.

  ==============================================================================
*/

#include "vaVoice.hpp"

#include <algorithm>
#include <cmath>

#include "../dsp/math.hpp"
#include "../headers/constants.hpp"

namespace YSE {
  namespace SYNTH {

    // Table resolution for the built-in band-limited shapes and the default
    // morph bank. A power of two keeps the phase→index math friendly.
    static const int kTableLen = 2048;
    // Number of summed partials in the band-limited shapes — bright enough for
    // a classic saw/square without obvious aliasing at musical pitches.
    static const int kHarmonics = 64;

    static inline Flt clampf(Flt v, Flt lo, Flt hi) {
      return v < lo ? lo : (v > hi ? hi : v);
    }

    // ─── vaADSR ────────────────────────────────────────────────────────────────

    void vaADSR::configure(Flt attack, Flt decay, Flt sustain, Flt release, Flt sampleRate) {
      sr = sampleRate > 1.f ? sampleRate : 44100.f;
      sus = clampf(sustain, 0.f, 1.f);
      const Flt eps = 1e-4f;
      const Flt a = attack > eps ? attack : eps;
      const Flt d = decay > eps ? decay : eps;
      aInc = 1.f / (a * sr);
      dInc = (1.f - sus) / (d * sr);
      rSec = release > eps ? release : eps;
    }

    void vaADSR::gateOn() {
      stage = ATTACK;
    }

    void vaADSR::gateOff() {
      rInc = lvl / (rSec * sr);
      if (rInc < 1e-9f) rInc = 1e-9f;
      stage = RELEASE;
    }

    Flt vaADSR::tick() {
      switch (stage) {
      case ATTACK:
        lvl += aInc;
        if (lvl >= 1.f) {
          lvl = 1.f;
          stage = DECAY;
        }
        break;
      case DECAY:
        lvl -= dInc;
        if (lvl <= sus) {
          lvl = sus;
          stage = SUSTAIN;
        }
        break;
      case SUSTAIN:
        lvl = sus;
        break;
      case RELEASE:
        lvl -= rInc;
        if (lvl <= 0.f) {
          lvl = 0.f;
          stage = IDLE;
        }
        break;
      case IDLE:
      default:
        lvl = 0.f;
        break;
      }
      return lvl;
    }

    void vaADSR::advance(int n) {
      for (int i = 0; i < n; i++)
        tick();
    }

    // ─── vaParams ───────────────────────────────────────────────────────────────

    vaParams::vaParams()
      : wavetablePosition(0.f),
        cutoff(2000.f),
        resonance(0.1f),
        keyTracking(0.5f),
        filterEnvAmount(2.f),
        filterVelAmount(0.f),
        ampAttack(0.005f),
        ampDecay(0.1f),
        ampSustain(0.8f),
        ampRelease(0.2f),
        ampVelAmount(1.f),
        filterAttack(0.005f),
        filterDecay(0.2f),
        filterSustain(0.3f),
        filterRelease(0.2f),
        lfoType(DSP::LFO_SINE),
        lfoRate(5.f),
        lfoToPitch(0.f),
        lfoToCutoff(0.f),
        lfoToWavetable(0.f),
        gain(0.8f),
        sawTable(kTableLen),
        triTable(kTableLen),
        sineTable(kTableLen) {
      // Per-oscillator defaults: a single saw voice out of the box.
      for (int i = 0; i < kNumOsc; i++) {
        oscWave[i].store(VA_SAW, std::memory_order_relaxed);
        oscDetune[i].store(0.f, std::memory_order_relaxed);
        oscLevel[i].store(i == 0 ? 0.8f : 0.f, std::memory_order_relaxed);
        oscPulseWidth[i].store(0.5f, std::memory_order_relaxed);
      }

      // Built-in band-limited shape tables.
      sawTable.createSaw(kHarmonics, kTableLen);
      triTable.createTriangle(kHarmonics, kTableLen);
      sineTable.createFourierTable(std::vector<Flt>{1.f}, kTableLen, 0.f);

      // Default morph bank: sine → triangle → saw → square. Continuous in the
      // morph position, so a wavetable sweep is click-free by construction.
      wtBank.reserve(4);
      wtBank.emplace_back(kTableLen);
      wtBank.back().createFourierTable(std::vector<Flt>{1.f}, kTableLen, 0.f);
      wtBank.emplace_back(kTableLen);
      wtBank.back().createTriangle(kHarmonics, kTableLen);
      wtBank.emplace_back(kTableLen);
      wtBank.back().createSaw(kHarmonics, kTableLen);
      wtBank.emplace_back(kTableLen);
      wtBank.back().createSquare(kHarmonics, kTableLen);
    }

    void vaParams::loadWavetable(int slot, const std::vector<Flt>& cycle) {
      if (slot < 0 || cycle.empty()) return;
      while (static_cast<int>(wtBank.size()) <= slot) {
        wtBank.emplace_back(static_cast<UInt>(cycle.size()));
      }
      // Build a fresh single-cycle table (with the wrap-around overflow tail)
      // and copy-assign it into the slot. Setup-thread only.
      DSP::wavetable fresh(static_cast<UInt>(cycle.size()));
      Flt* p = fresh.getPtr();
      for (UInt i = 0; i < cycle.size(); i++)
        p[i] = cycle[i];
      fresh.copyOverflow();
      wtBank[slot] = fresh;
    }

    // ─── vaVoice ────────────────────────────────────────────────────────────────

    vaVoice::vaVoice(int outputChannels)
      : dspVoice(outputChannels),
        params(std::make_shared<vaParams>()),
        noiseState(0x1234567u),
        phase(IDLE) {
      for (int i = 0; i < vaParams::kNumOsc; i++)
        oscPhase[i] = 0.0;
    }

    vaVoice::vaVoice(const vaVoice& other)
      : dspVoice(other),
        params(other.params), // share the patch — all voices track one sound
        noiseState(0x1234567u),
        phase(IDLE) {
      // Fresh, independent DSP state (phases, filter, envelopes) — a clone
      // shares only the patch, never the running state.
      for (int i = 0; i < vaParams::kNumOsc; i++)
        oscPhase[i] = 0.0;
      filter.reset();
      ampEnv.reset();
      filEnv.reset();
    }

    dspVoice* vaVoice::clone() {
      return new vaVoice(*this);
    }

    Flt vaVoice::readTable(DSP::wavetable& t, Dbl phase) {
      const UInt len = t.getLength();
      if (len == 0) return 0.f;
      Dbl fp = phase - std::floor(phase); // wrap to [0,1)
      Dbl fidx = fp * len;
      UInt i0 = static_cast<UInt>(fidx);
      if (i0 >= len) i0 = len - 1; // guard rounding at the top edge
      Flt frac = static_cast<Flt>(fidx - i0);
      Flt* p = t.getPtr(); // storage has a 1-sample overflow tail: p[len] == p[0]
      return p[i0] + frac * (p[i0 + 1] - p[i0]);
    }

    Flt vaVoice::renderOsc(int index, Dbl phase, Flt pulseWidth, Flt wtPos) {
      vaParams& p = *params;
      switch (static_cast<VA_WAVEFORM>(p.oscWave[index].load(std::memory_order_relaxed))) {
      case VA_SINE:
        return readTable(p.sineTable, phase);
      case VA_TRIANGLE:
        return readTable(p.triTable, phase);
      case VA_SAW:
        return readTable(p.sawTable, phase);
      case VA_PULSE: {
        // Band-limited variable-width pulse = saw(φ) − saw(φ + width), both
        // read from the band-limited saw table. Mean-free for any width.
        Dbl p2 = phase + pulseWidth;
        return 0.5f * (readTable(p.sawTable, phase) - readTable(p.sawTable, p2));
      }
      case VA_NOISE: {
        noiseState = noiseState * 1664525u + 1013904223u;
        return (static_cast<Flt>(noiseState >> 8) / 8388608.f) - 1.f;
      }
      case VA_WAVETABLE: {
        const int cnt = static_cast<int>(p.wtBank.size());
        if (cnt == 0) return 0.f;
        if (cnt == 1) return readTable(p.wtBank[0], phase);
        Flt fpos = clampf(wtPos, 0.f, 1.f) * static_cast<Flt>(cnt - 1);
        int i0 = static_cast<int>(fpos);
        if (i0 > cnt - 2) i0 = cnt - 2;
        if (i0 < 0) i0 = 0;
        Flt fr = fpos - static_cast<Flt>(i0);
        return readTable(p.wtBank[i0], phase) * (1.f - fr) +
               readTable(p.wtBank[i0 + 1], phase) * fr;
      }
      default:
        return 0.f;
      }
    }

    void vaVoice::process(SOUND_STATUS& intent) {
      if (samples.empty()) return; // degenerate 0-channel voice — nothing to render
      const UInt n = samples[0].getLength();

      // ---- lifecycle / gates -------------------------------------------------
      if (intent == SS_WANTSTOPLAY || intent == SS_WANTSTORESTART) {
        // Restart oscillator phase and re-gate the envelopes. The ladder state
        // is deliberately not cleared: on a fresh voice it is already zero
        // (constructor / clone reset it), and on a retrigger keeping it avoids
        // a filter click — the amplitude envelope is at zero at this instant
        // anyway, so any residual is inaudible.
        for (int o = 0; o < vaParams::kNumOsc; o++)
          oscPhase[o] = 0.0;
        ampEnv.gateOn();
        filEnv.gateOn();
        phase = PLAYING;
        intent = SS_PLAYING;
      } else if (intent == SS_WANTSTOSTOP || intent == SS_WANTSTOPAUSE) {
        if (phase == IDLE) {
          // Released before it ever attacked — settle immediately (mirrors
          // sineVoice): nothing is sounding.
          intent = (intent == SS_WANTSTOPAUSE) ? SS_PAUSED : SS_STOPPED;
          for (UInt ch = 0; ch < samples.size(); ch++)
            samples[ch] = 0.f;
          return;
        }
        if (phase != RELEASING) {
          ampEnv.gateOff();
          filEnv.gateOff();
          phase = RELEASING;
        }
      } else if (intent == SS_PLAYING || intent == SS_PLAYING_FULL_VOLUME) {
        // keep rendering
      } else {
        // SS_STOPPED / SS_PAUSED: emit silence.
        for (UInt ch = 0; ch < samples.size(); ch++)
          samples[ch] = 0.f;
        return;
      }

      const Flt sr = static_cast<Flt>(SAMPLERATE);
      vaParams& p = *params;

      // Envelope slopes — recomputed each block so patch times are live-settable.
      ampEnv.configure(p.ampAttack.load(std::memory_order_relaxed),
                       p.ampDecay.load(std::memory_order_relaxed),
                       p.ampSustain.load(std::memory_order_relaxed),
                       p.ampRelease.load(std::memory_order_relaxed), sr);
      filEnv.configure(p.filterAttack.load(std::memory_order_relaxed),
                       p.filterDecay.load(std::memory_order_relaxed),
                       p.filterSustain.load(std::memory_order_relaxed),
                       p.filterRelease.load(std::memory_order_relaxed), sr);

      const Flt vel = getVelocity();
      const Flt wheelSemi = getPitchWheel() * 2.f; // ±2 semitones bend range
      const Flt baseHz = getFrequency();

      // LFO — one bipolar value per block (control-rate is plenty for an LFO).
      const DSP::LFO_TYPE lt =
          static_cast<DSP::LFO_TYPE>(p.lfoType.load(std::memory_order_relaxed));
      const Flt lfoRate = p.lfoRate.load(std::memory_order_relaxed);
      Flt lfoS = 0.f;
      {
        DSP::buffer& lb = lfoOsc(lt, lfoRate, n > 0 ? n : STANDARD_BUFFERSIZE);
        if (n > 0) lfoS = lb.getPtr()[n - 1] * 2.f - 1.f;
      }

      // Per-oscillator phase increments and mix levels (block-rate).
      const Flt lfoPitchSemi = lfoS * p.lfoToPitch.load(std::memory_order_relaxed);
      Dbl inc[vaParams::kNumOsc];
      Flt lvlOsc[vaParams::kNumOsc];
      Flt pw[vaParams::kNumOsc];
      for (int o = 0; o < vaParams::kNumOsc; o++) {
        lvlOsc[o] = p.oscLevel[o].load(std::memory_order_relaxed);
        pw[o] = clampf(p.oscPulseWidth[o].load(std::memory_order_relaxed), 0.01f, 0.99f);
        const Flt semi = p.oscDetune[o].load(std::memory_order_relaxed) + lfoPitchSemi + wheelSemi;
        const Flt hz = baseHz * std::exp2(semi / 12.f);
        inc[o] = static_cast<Dbl>(hz) / static_cast<Dbl>(sr);
      }
      const Flt wtPos = clampf(p.wavetablePosition.load(std::memory_order_relaxed) +
                                   lfoS * p.lfoToWavetable.load(std::memory_order_relaxed),
                               0.f, 1.f);

      // Filter cutoff (block-rate, glided inside the ladder for click-free sweeps).
      const Flt noteMidi = DSP::FreqToMidi(baseHz);
      const Flt keyOct = p.keyTracking.load(std::memory_order_relaxed) * (noteMidi - 60.f) / 12.f;
      const Flt envOct = p.filterEnvAmount.load(std::memory_order_relaxed) * filEnv.level();
      const Flt lfoOct = lfoS * p.lfoToCutoff.load(std::memory_order_relaxed);
      const Flt velOct = vel * p.filterVelAmount.load(std::memory_order_relaxed);
      const Flt cutoffHz =
          p.cutoff.load(std::memory_order_relaxed) * std::exp2(keyOct + envOct + lfoOct + velOct);
      filter.setResonance(p.resonance.load(std::memory_order_relaxed));
      filter.setCutoff(cutoffHz);
      filEnv.advance(static_cast<int>(n)); // move the filter env on for next block

      // Amplitude: velocity blend then master gain.
      const Flt ampVelAmt = p.ampVelAmount.load(std::memory_order_relaxed);
      const Flt g = p.gain.load(std::memory_order_relaxed) * ((1.f - ampVelAmt) + ampVelAmt * vel);

      // ---- per-sample render -------------------------------------------------
      Flt* out = samples[0].getPtr();
      for (UInt i = 0; i < n; i++) {
        Flt oscSum = 0.f;
        for (int o = 0; o < vaParams::kNumOsc; o++) {
          oscPhase[o] += inc[o];
          if (oscPhase[o] >= 1.0) oscPhase[o] -= std::floor(oscPhase[o]);
          if (lvlOsc[o] <= 0.f) continue; // muted osc: keep phase, skip render
          oscSum += renderOsc(o, oscPhase[o], pw[o], wtPos) * lvlOsc[o];
        }
        const Flt filt = filter.process(oscSum);
        const Flt a = ampEnv.tick();
        out[i] = filt * a * g;
      }

      // Mirror the mono voice signal to any extra output channels.
      for (UInt ch = 1; ch < samples.size(); ch++)
        samples[ch] = samples[0];

      // Release tail finished — hand the slot back to the engine.
      if (phase == RELEASING && ampEnv.idle()) {
        intent = SS_STOPPED;
        phase = IDLE;
      }
    }

  } // namespace SYNTH
} // namespace YSE
