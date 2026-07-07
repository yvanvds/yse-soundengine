/*
  ==============================================================================

    samplerVoice.cpp
    SFZ sampler voice — see samplerVoice.hpp and
    docs/design/sfz_opcode_subset.md.

  ==============================================================================
*/

#include "samplerVoice.hpp"

#include <algorithm>
#include <cmath>

#include "../headers/constants.hpp"

namespace YSE {
  namespace SYNTH {

    // Pitch-wheel bend range, semitones per unit deflection (§6 recommends ±2).
    static const double kBendRangeSemitones = 2.0;
    // Amp-EG release floor (~5 ms) so a released note never hard-cuts (§8).
    static const Flt kReleaseFloor = 0.005f;
    // Fast-choke declick, seconds — the voice-side equivalent of the engine
    // steal-fade for off_mode=fast (§4).
    static const Flt kChokeFadeSec = 0.005f;
    // Smallest legal envelope segment, seconds (avoids divide-by-zero slopes).
    static const Flt kMinSegment = 1e-5f;

    static inline Flt clampf(Flt v, Flt lo, Flt hi) {
      return v < lo ? lo : (v > hi ? hi : v);
    }
    static inline long clampl(long v, long lo, long hi) {
      return v < lo ? lo : (v > hi ? hi : v);
    }

    // ---- sfzADSR (allocation-free DAHDSR) ---------------------------------

    void sfzADSR::configure(Flt delay, Flt attack, Flt hold, Flt decay, Flt sustain, Flt release,
                            Flt sr_) {
      sr = sr_ > 0.0f ? sr_ : 44100.0f;
      sus = clampf(sustain, 0.0f, 1.0f);
      delaySamps = static_cast<long>(std::max(0.0f, delay) * sr);
      holdSamps = static_cast<long>(std::max(0.0f, hold) * sr);
      const Flt aT = std::max(attack, kMinSegment);
      const Flt dT = std::max(decay, kMinSegment);
      rSecs = std::max(release, kMinSegment);
      aInc = 1.0f / (aT * sr);
      dInc = (1.0f - sus) / (dT * sr);
      rInc = 1.0f / (rSecs * sr);
    }

    void sfzADSR::gateOn() {
      lvl = 0.0f;
      delayCnt = delaySamps;
      holdCnt = holdSamps;
      stage = delaySamps > 0 ? DELAY : ATTACK;
    }

    void sfzADSR::gateOff() {
      if (stage == IDLE || stage == DONE) return;
      rInc = (rSecs > 0.0f) ? (lvl / (rSecs * sr)) : lvl;
      if (rInc <= 0.0f) rInc = 1.0f; // already silent -> finish next tick
      stage = RELEASE;
    }

    Flt sfzADSR::tick() {
      switch (stage) {
      case IDLE:
        return 0.0f;
      case DELAY:
        if (delayCnt-- > 0) return 0.0f;
        stage = ATTACK;
        [[fallthrough]];
      case ATTACK:
        lvl += aInc;
        if (lvl >= 1.0f) {
          lvl = 1.0f;
          holdCnt = holdSamps;
          stage = holdSamps > 0 ? HOLD : DECAY;
        }
        return lvl;
      case HOLD:
        if (holdCnt-- > 0) return lvl;
        stage = DECAY;
        return lvl;
      case DECAY:
        lvl -= dInc;
        if (lvl <= sus) {
          lvl = sus;
          stage = SUSTAIN;
        }
        return lvl;
      case SUSTAIN:
        return lvl;
      case RELEASE:
        lvl -= rInc;
        if (lvl <= 0.0f) {
          lvl = 0.0f;
          stage = DONE;
        }
        return lvl;
      case DONE:
      default:
        return 0.0f;
      }
    }

    // ---- samplerInstrument ------------------------------------------------

    bool samplerInstrument::load() {
      if (!model.valid) return false;

      samples.clear();
      samples.resize(model.samples.size());
      bool anyLoaded = false;
      for (size_t i = 0; i < model.samples.size(); ++i) {
        residentSample& rs = samples[i];
        const DSP::sfzSample& src = model.samples[i];
        if (src.silence) {
          rs.silence = true;
          rs.loaded = true;
          rs.frames = 0;
          anyLoaded = true;
          continue;
        }
        DSP::fileBuffer ch0;
        if (!ch0.load(src.path.c_str(), 0)) {
          rs.loaded = false; // regions naming it will render silence
          continue;
        }
        rs.frames = static_cast<long>(ch0.getLength());
        rs.sampleRateAdjustment = ch0.getSampleRateAdjustment();
        rs.channels.push_back(ch0);
        DSP::fileBuffer ch1;
        if (ch1.load(src.path.c_str(), 1)) rs.channels.push_back(ch1);
        rs.loaded = true;
        anyLoaded = true;
      }
      (void)anyLoaded;
      return valid();
    }

    uint32_t samplerInstrument::bumpChoke(int g) {
      if (g <= 0) return 0;
      int idx = g >= kMaxChokeGroups ? kMaxChokeGroups - 1 : g;
      return ++chokeGenerations[static_cast<size_t>(idx)];
    }

    uint32_t samplerInstrument::chokeGen(int g) const {
      if (g <= 0) return 0;
      int idx = g >= kMaxChokeGroups ? kMaxChokeGroups - 1 : g;
      return chokeGenerations[static_cast<size_t>(idx)];
    }

    // ---- samplerConfig facade ---------------------------------------------

    bool samplerConfig::build(samplerInstrument& inst) const {
      inst.model = DSP::sfzInstrument{};
      if (file_.empty()) return false;

      DSP::sfzSample s;
      s.path = file_;
      s.silence = false;
      inst.model.samples.push_back(s);

      DSP::sfzRegion r; // defaults from the model
      r.sampleIndex = 0;
      r.pitchKeycenter = root_;
      r.lokey = low_;
      r.hikey = high_;
      r.egAttack = attack_;
      r.egRelease = release_;
      r.ampVeltrack = 100.0f;
      r.loopMode = DSP::SFZ_NO_LOOP; // the facade never exposed looping (§11)
      inst.model.regions.push_back(r);
      inst.model.valid = true;
      inst.model.sourceFile = name_;

      if (!inst.load()) return false;

      // maxLength caps the one-shot length (§11): clamp `end` to maxLength
      // seconds of source audio. Not a native SFZ opcode.
      if (!inst.samples.empty() && inst.samples[0].loaded && inst.samples[0].frames > 0) {
        const long frames = inst.samples[0].frames;
        const Flt sra = inst.samples[0].sampleRateAdjustment;
        long end = frames - 1;
        long cap = static_cast<long>(maxLength_ * static_cast<Flt>(SAMPLERATE) * sra);
        if (cap > 0 && cap < end) end = cap;
        inst.model.regions[0].endFrame = end;
      }
      return true;
    }

    // ---- samplerVoice -----------------------------------------------------

    samplerVoice::samplerVoice(int outputChannels) : dspVoice(outputChannels) {
      chokeFadeSamps = std::max(1, static_cast<int>(static_cast<Flt>(SAMPLERATE) * kChokeFadeSec));
    }

    samplerVoice::samplerVoice(const samplerVoice& other)
      : dspVoice(other), inst(other.inst) { // share the immutable instrument (spec §10)
      note_.store(other.note_.load(std::memory_order_relaxed), std::memory_order_relaxed);
      chokeFadeSamps = other.chokeFadeSamps;
      // per-voice playback state stays at rest (fresh, independent of the prototype)
    }

    dspVoice* samplerVoice::clone() {
      return new samplerVoice(*this);
    }

    void samplerVoice::frequency(Flt midiNote) {
      dspVoice::frequency(midiNote); // base stores Hz for the position/pan path
      note_.store(static_cast<int>(std::lround(midiNote)), std::memory_order_relaxed);
    }

    bool samplerVoice::loadSFZ(const std::string& path) {
      inst = std::make_shared<samplerInstrument>();
      inst->model = DSP::loadSFZ(path);
      return inst->load();
    }

    bool samplerVoice::configure(const samplerConfig& cfg) {
      inst = std::make_shared<samplerInstrument>();
      return cfg.build(*inst);
    }

    // Same cubic kernel as DSP::interpolate4, with loop-region-aware tap fetch so
    // the 4 taps stay valid across a seamless loop seam (spec §7).
    Flt samplerVoice::cubic(const Flt* d, long n, double pos, long loopStart, long loopEnd,
                            bool looping) {
      const long loopLen = loopEnd - loopStart + 1;
      auto tap = [&](long t) -> Flt {
        if (looping && loopLen > 0) {
          if (t > loopEnd)
            t = loopStart + ((t - loopStart) % loopLen);
          else if (t < loopStart)
            t = loopStart; // pre-loop guard; positions only wrap forward
        }
        if (t < 0) t = 0;
        if (t >= n) t = n - 1;
        return d[t];
      };
      long i = static_cast<long>(std::floor(pos));
      Flt frac = static_cast<Flt>(pos - static_cast<double>(i));
      Flt a = tap(i - 1), b = tap(i), c = tap(i + 1), dd = tap(i + 2);
      Flt cmb = c - b;
      return b + frac * (cmb - 0.1666667f * (1.0f - frac) *
                                   ((dd - a - 3.0f * cmb) * frac + (dd + 2.0f * a - 3.0f * b)));
    }

    void samplerVoice::armLayer(Layer& L, int regionIndex, int note, int velocity) {
      const DSP::sfzRegion& r = inst->model.regions[static_cast<size_t>(regionIndex)];
      // Non-const to take a raw Flt* for reading; the PCM itself is never written.
      residentSample& rs = inst->samples[static_cast<size_t>(r.sampleIndex)];

      L.active = true;
      L.finished = false;
      L.releasing = false;
      L.sampleDone = false;
      L.regionIndex = regionIndex;

      const long frames = rs.frames;
      if (rs.silence || !rs.loaded || frames <= 0) {
        L.numCh = 0;
        L.frames = 0;
        L.ch[0] = L.ch[1] = nullptr;
      } else {
        L.numCh = std::min<int>(2, static_cast<int>(rs.channels.size()));
        L.ch[0] = rs.channels[0].getPtr();
        L.ch[1] = L.numCh > 1 ? rs.channels[1].getPtr() : nullptr;
        L.frames = frames;
      }

      // pitch / tuning (spec §6)
      const double semis =
          (note - r.pitchKeycenter) * (static_cast<double>(r.pitchKeytrack) / 100.0) +
          r.transposeSemis;
      const double cents = r.tuneCents;
      const double ratio = std::pow(2.0, (semis + cents / 100.0) / 12.0);
      L.baseSpeed = ratio * static_cast<double>(rs.sampleRateAdjustment);

      // playback bounds (spec §7)
      const long lastFrame = frames > 0 ? frames - 1 : 0;
      L.offset = clampl(r.offset, 0, lastFrame);
      L.endFrame = (r.endFrame == DSP::SFZ_UNSET) ? lastFrame : clampl(r.endFrame, 0, lastFrame);
      L.pos = static_cast<double>(L.offset);
      L.loopMode = r.loopMode;
      long ls = (r.loopStart == DSP::SFZ_UNSET) ? 0 : r.loopStart;
      long le = (r.loopEnd == DSP::SFZ_UNSET) ? L.endFrame : r.loopEnd;
      ls = clampl(ls, 0, lastFrame);
      le = clampl(le, 0, lastFrame);
      L.loopStart = ls;
      L.loopEnd = le;
      L.looping = (r.loopMode == DSP::SFZ_LOOP_CONTINUOUS || r.loopMode == DSP::SFZ_LOOP_SUSTAIN) &&
                  ls < le && frames > 0;

      // amplitude: volume * velocity-track * crossfade (constant NOTE_ON gain, §8)
      const Flt volGain = std::pow(10.0f, r.volumeDb / 20.0f);
      const Flt v = static_cast<Flt>(velocity) / 127.0f;
      const Flt track = r.ampVeltrack / 100.0f;
      Flt velGain = 1.0f - track * (1.0f - v * v);
      if (velGain < 0.0f) velGain = 0.0f;

      auto curve = [](Flt p, int c) -> Flt {
        if (p < 0.0f) p = 0.0f;
        if (p > 1.0f) p = 1.0f;
        return c == DSP::SFZ_CURVE_POWER ? std::sqrt(p) : p;
      };
      auto xfadeAxis = [&](int x, int inLo, int inHi, int outLo, int outHi, int c) -> Flt {
        Flt gin = 1.0f, gout = 1.0f;
        if (inLo > 0 || inHi > 0) {
          if (x <= inLo)
            gin = 0.0f;
          else if (x >= inHi)
            gin = 1.0f;
          else
            gin = static_cast<Flt>(x - inLo) / static_cast<Flt>(inHi - inLo);
        }
        if (outLo > 0 || outHi > 0) {
          if (x <= outLo)
            gout = 1.0f;
          else if (x >= outHi)
            gout = 0.0f;
          else
            gout = 1.0f - static_cast<Flt>(x - outLo) / static_cast<Flt>(outHi - outLo);
        }
        return curve(gin, c) * curve(gout, c);
      };
      const Flt xfVel =
          xfadeAxis(velocity, r.xfinLovel, r.xfinHivel, r.xfoutLovel, r.xfoutHivel, r.xfVelcurve);
      const Flt xfKey =
          xfadeAxis(note, r.xfinLokey, r.xfinHikey, r.xfoutLokey, r.xfoutHikey, r.xfKeycurve);
      L.gain = volGain * velGain * xfVel * xfKey;

      // amp EG (§8) — DAHDSR with a ~5 ms release floor.
      const Flt rel = std::max(r.egRelease, kReleaseFloor);
      L.env.configure(r.egDelay, r.egAttack, r.egHold, r.egDecay, r.egSustain, rel,
                      static_cast<Flt>(SAMPLERATE));
      L.env.gateOn();

      // choke bookkeeping (§4): record the baseline generation this layer will
      // watch, so a later fire of its off_by group is detected.
      L.chokeGroup = r.chokeGroup;
      L.offBy = r.offBy;
      L.offMode = r.offMode;
      L.offByBaseline = inst->chokeGen(r.offBy);
    }

    void samplerVoice::startNote() {
      // Reset every layer to rest first, so a dropped note leaves no stale state.
      for (auto& L : layers) {
        L.active = false;
        L.finished = false;
        L.regionIndex = -1;
        L.env.reset();
      }
      chokeFading = false;
      chokeFadePos = 0;
      phase = IDLE;

      if (!inst || !inst->valid()) return;

      const int n = note_.load(std::memory_order_relaxed) & 127;
      int vel = static_cast<int>(std::lround(getVelocity() * 127.0f));
      vel = std::min(127, std::max(1, vel));

      // Round-robin: read + increment the per-key counter in this single-threaded
      // audio pass; the region table stays immutable (spec §4).
      const int hit = inst->seqCounter[static_cast<size_t>(n)]++;
      lastHit_ = hit;

      int idx[DSP::YSE_MAX_REGION_LAYERS];
      const int count = DSP::resolveLayers(inst->model, n, vel, hit, idx);
      if (count == 0) return; // no region matched -> voice drops cleanly (§5)

      for (int k = 0; k < count; ++k) {
        armLayer(layers[static_cast<size_t>(k)], idx[k], n, vel);
      }
      phase = PLAYING;

      // Fire choke groups after arming all layers (baselines already recorded),
      // so this voice never chokes itself off its own fire (§4).
      for (int k = 0; k < count; ++k) {
        const int g = layers[static_cast<size_t>(k)].chokeGroup;
        if (g != 0) inst->bumpChoke(g);
      }
    }

    void samplerVoice::releaseNote() {
      phase = RELEASING;
      for (auto& L : layers) {
        if (!L.active || L.finished) continue;
        if (L.loopMode == DSP::SFZ_ONE_SHOT) continue; // one-shot ignores note-off (§7)
        if (!L.releasing) {
          L.releasing = true;
          L.env.gateOff();
          if (L.loopMode == DSP::SFZ_LOOP_SUSTAIN) L.looping = false; // play out on release
        }
      }
    }

    void samplerVoice::applyChoke() {
      if (phase == IDLE || chokeFading) return;
      bool fast = false, normal = false;
      for (auto& L : layers) {
        if (!L.active || L.finished || L.offBy == 0) continue;
        if (inst->chokeGen(L.offBy) != L.offByBaseline) {
          if (L.offMode == DSP::SFZ_OFF_FAST)
            fast = true;
          else
            normal = true;
        }
      }
      if (fast) {
        chokeFading = true;
        chokeFadePos = 0;
      } else if (normal) {
        releaseNote();
      }
    }

    bool samplerVoice::renderLayers(int blockLen) {
      Flt* out = samples[0].getPtr();
      for (int i = 0; i < blockLen; ++i)
        out[i] = 0.0f;

      const Flt wheel = getPitchWheel();
      const double wheelRatio =
          wheel != 0.0f ? std::pow(2.0, static_cast<double>(wheel) * kBendRangeSemitones / 12.0)
                        : 1.0;

      bool anySound = false;
      for (auto& L : layers) {
        if (!L.active || L.finished) continue;
        const double speed = L.baseSpeed * wheelRatio;
        const double loopLen = static_cast<double>(L.loopEnd - L.loopStart + 1);

        for (int s = 0; s < blockLen; ++s) {
          const Flt env = L.env.tick();
          Flt sampleVal = 0.0f;
          if (L.numCh > 0 && !L.sampleDone) {
            Flt v0 = cubic(L.ch[0], L.frames, L.pos, L.loopStart, L.loopEnd, L.looping);
            if (L.numCh > 1) {
              Flt v1 = cubic(L.ch[1], L.frames, L.pos, L.loopStart, L.loopEnd, L.looping);
              sampleVal = 0.5f * (v0 + v1);
            } else {
              sampleVal = v0;
            }
            L.pos += speed;
            if (L.looping) {
              while (L.pos >= static_cast<double>(L.loopStart) + loopLen)
                L.pos -= loopLen;
            } else if (L.pos > static_cast<double>(L.endFrame)) {
              L.sampleDone = true;
            }
          }
          out[s] += sampleVal * L.gain * env;
        }

        // Retire the layer once it can produce no more sound (spec §8): a
        // one-shot ends at its sample end; every other mode ends when its own
        // release EG has finished (so a short layer never cuts a longer one).
        if (L.loopMode == DSP::SFZ_ONE_SHOT) {
          if (L.sampleDone) L.finished = true;
        } else if (L.env.atEnd()) {
          L.finished = true;
        }
        if (!L.finished) anySound = true;
      }
      return anySound;
    }

    void samplerVoice::process(SOUND_STATUS& intent) {
      const int blockLen = static_cast<int>(samples[0].getLength());
      Flt* out = samples[0].getPtr();

      if (intent == SS_WANTSTOPLAY || intent == SS_WANTSTORESTART) {
        startNote();
        intent = SS_PLAYING;
        if (phase == IDLE) {
          // No region matched (or no instrument) — drop the note cleanly (§5).
          for (int i = 0; i < blockLen; ++i)
            out[i] = 0.0f;
          intent = SS_STOPPED;
          return;
        }
      } else if (intent == SS_WANTSTOSTOP || intent == SS_WANTSTOPAUSE) {
        if (phase == IDLE) {
          intent = SS_STOPPED;
          for (int i = 0; i < blockLen; ++i)
            out[i] = 0.0f;
          return;
        }
        if (phase != RELEASING) releaseNote(); // take the release edge once
      } else if (intent == SS_PLAYING || intent == SS_PLAYING_FULL_VOLUME) {
        // sustaining — fall through to render
      } else {
        // SS_STOPPED / SS_PAUSED — emit silence, keep state.
        for (int i = 0; i < blockLen; ++i)
          out[i] = 0.0f;
        return;
      }

      // Choke scan (§4): a fired choke group targeting one of our layers' off_by
      // either fast-fades us or triggers our release.
      applyChoke();

      renderLayers(blockLen);

      // Fast-choke declick overlay.
      if (chokeFading) {
        const Flt fade = static_cast<Flt>(chokeFadeSamps);
        for (int s = 0; s < blockLen; ++s) {
          Flt g = 1.0f - static_cast<Flt>(chokeFadePos + s) / fade;
          if (g < 0.0f) g = 0.0f;
          out[s] *= g;
        }
        chokeFadePos += blockLen;
        if (chokeFadePos >= chokeFadeSamps) {
          for (auto& L : layers)
            if (L.active) L.finished = true;
        }
      }

      // Settle STOPPED only when every sounding layer has finished — i.e. the
      // longest layer release governs the voice's end of life (spec §8).
      bool anyActive = false, allDone = true;
      for (auto& L : layers) {
        if (!L.active) continue;
        anyActive = true;
        if (!L.finished) allDone = false;
      }
      if (!anyActive || allDone) {
        intent = SS_STOPPED;
        phase = IDLE;
      }
    }

    // ---- introspection ----------------------------------------------------

    int samplerVoice::activeLayers() const {
      int n = 0;
      for (const auto& L : layers)
        if (L.active && !L.finished) ++n;
      return n;
    }

    int samplerVoice::layerRegion(int i) const {
      int n = 0;
      for (const auto& L : layers) {
        if (L.active && !L.finished) {
          if (n == i) return L.regionIndex;
          ++n;
        }
      }
      return -1;
    }

    Flt samplerVoice::layerGain(int i) const {
      int n = 0;
      for (const auto& L : layers) {
        if (L.active && !L.finished) {
          if (n == i) return L.gain;
          ++n;
        }
      }
      return 0.0f;
    }

  } // namespace SYNTH
} // namespace YSE
