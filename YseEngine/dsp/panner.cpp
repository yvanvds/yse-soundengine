/*
  ==============================================================================

    panner.cpp
    Reusable per-source spatialization component — see panner.hpp and
    docs/design/per_note_positioning.md §6 (issue #169).

    The static helpers below are lifted VERBATIM from
    SOUND::implementationObject so the sound path (which now forwards to them)
    and the per-voice synth panner share one implementation and stay
    bit-identical. Do not "clean up" the arithmetic here without re-checking the
    #202/#204/#207/#208/#210/#212/#213 bit-exactness tests.

    NB: the math calls are qualified to the GLOBAL scope (::pow / ::sqrt / ::cos
    / ::atan2). Unqualified, they would resolve to the YSE::DSP::pow / ::sqrt /
    ::cos math-object CLASSES that live in this namespace. The global functions
    are exactly the ones the sound path (namespace YSE::SOUND, which has no such
    classes) resolves to, so the arithmetic stays bit-identical.

  ==============================================================================
*/

#include "panner.hpp"

#include "../internalHeaders.h"

#include <cmath>

namespace YSE {
  namespace DSP {

    namespace {
      // Default rolloff-start radius for a per-voice panner. Matches the sound's
      // default `size` (soundImplementation.cpp ctor) so a voice at a given
      // distance rolls off exactly like a sound would.
      const Flt kDefaultSize = 1.0f;
    } // namespace

    panner::panner()
      : numOutputs(0),
        realSpeakers(0),
        srcChannels(0),
        lastPosition(0.f),
        haveLastPosition(false),
        lastRelative(false),
        distance(0.f),
        angle(0.f),
        horizFraction(1.f),
        size(kDefaultSize),
        gainDirty(true) {}

    // ---- pure spatialization math (verbatim from SOUND::implementationObject) --

    Flt panner::computePanRatio(Flt initGain, Flt power, UInt speakerCount) {
      constexpr Flt minPower = 1e-9f;
      if (speakerCount == 0) return 0.f;
      if (!(power > minPower)) return 1.f / static_cast<Flt>(speakerCount);
      return static_cast<Flt>(::pow(initGain, 2) / power);
    }

    Flt panner::computeSourceAngle(bool relative, const Pos& dir, const Pos& listenerForward) {
      Flt a = static_cast<Flt>(::atan2(dir.x, dir.z));
      if (!relative) {
        a -= static_cast<Flt>(::atan2(listenerForward.x, listenerForward.z));
      }
      while (a > Pi)
        a -= Pi2;
      while (a < -Pi)
        a += Pi2;
      return a;
    }

    Flt panner::computeHorizontalFraction(const Pos& dir) {
      Flt horiz = static_cast<Flt>(::sqrt(dir.x * dir.x + dir.z * dir.z));
      Flt total = static_cast<Flt>(::sqrt(dir.x * dir.x + dir.y * dir.y + dir.z * dir.z));
      constexpr Flt minTotal = 1e-9f;
      if (total < minTotal) return 1.f;
      return horiz / total;
    }

    Flt panner::computeSpeakerOverlap(Flt angleA, Flt angleB) {
      return (1 + ::cos(angleA - angleB)) * 0.5f;
    }

    Flt panner::computeDopplerRatio(const Pos& sourceVel, const Pos& listenerVel, const Pos& dist,
                                    Flt dopplerScale) {
      constexpr Flt speedOfSound = 344.0f; // m/s, dry air ~20C
      constexpr Flt minRatio = 0.25f;
      constexpr Flt maxRatio = 4.0f;

      Pos d = dist; // local copy: Pos::length() is non-const
      Flt len = d.length();
      if (len == 0.f) return 1.0f;

      Flt rSource = Dot(sourceVel, d) / len;
      Flt rList = Dot(listenerVel, d) / len;

      Flt denom = speedOfSound + rSource;
      if (denom <= 0.f) return maxRatio;
      Flt ratio = (speedOfSound + rList) / denom;

      ratio = 1.0f + (ratio - 1.0f) * dopplerScale;

      Clamp(ratio, minRatio, maxRatio);
      return ratio;
    }

    Flt panner::computeVirtualDist(Flt distance, Flt size, Flt volume) {
      Flt effectiveDist = distance - size;
      if (effectiveDist < 0) effectiveDist = 0;

      constexpr Flt minVolume = 0.0001f;
      if (volume < minVolume) volume = minVolume;
      return effectiveDist / volume;
    }

    void panner::gainAccumulate(const Flt* src, const Flt* fader, Flt* dest, UInt length,
                                Flt& lastGain, Flt finalGain) {
      if (lastGain == finalGain) {
        for (UInt i = 0; i < length; i++) {
          Flt v = src[i] * finalGain;
          v *= fader[i];
          dest[i] += v;
        }
        return;
      }

      Flt rampLen = 50;
      Clamp(rampLen, 1.f, static_cast<Flt>(length));
      Flt step = (finalGain - lastGain) / rampLen;
      Flt multiplier = lastGain;
      UInt i = 0;
      UInt ramp = static_cast<UInt>(rampLen);
      for (; i < ramp; i++) {
        Flt v = src[i] * multiplier;
        v *= fader[i];
        dest[i] += v;
        multiplier += step;
      }
      for (; i < length; i++) {
        Flt v = src[i] * finalGain;
        v *= fader[i];
        dest[i] += v;
      }
      lastGain = finalGain;
    }

    // ---- per-instance per-voice panner --------------------------------------

    void panner::resize(UInt sourceChannels) {
      // Slow-pool / device-restart only. Snapshot the device speaker layout and
      // size every gain vector. After this returns, update()/spread() never
      // allocate.
      numOutputs = CHANNEL::Manager().getNumberOfOutputs();
      srcChannels = sourceChannels;

      // No device geometry (headless / pre-init): fall back to a single centred
      // speaker so the panner degenerates to a mono passthrough (finalGain 1 at
      // the origin) instead of producing an empty, silent bed.
      const bool noDevice = (numOutputs == 0);
      if (noDevice) numOutputs = 1;

      spkAngle.resize(numOutputs);
      spkEffective.assign(numOutputs, 1.f);
      spkIsLFE.resize(numOutputs);
      for (UInt i = 0; i < numOutputs; i++) {
        spkAngle[i] = noDevice ? 0.f : CHANNEL::Manager().getOutputAngle(i);
        spkIsLFE[i] = noDevice ? false : CHANNEL::Manager().getOutputIsLFE(i);
      }

      // effective[i] = sum over non-LFE j of overlap(angle_i, angle_j) — the
      // density-compensation term, identical to
      // CHANNEL::implementationObject::computeEffectiveSpeakerWeights (#211).
      realSpeakers = 0;
      for (UInt i = 0; i < numOutputs; i++) {
        if (!spkIsLFE[i]) realSpeakers++;
      }
      for (UInt i = 0; i < numOutputs; i++) {
        if (spkIsLFE[i]) continue;
        Flt sum = 0;
        for (UInt j = 0; j < numOutputs; j++) {
          if (spkIsLFE[j]) continue;
          sum += computeSpeakerOverlap(spkAngle[i], spkAngle[j]);
        }
        spkEffective[i] = sum;
      }

      finalGainCache.assign(numOutputs, std::vector<Flt>(srcChannels, 0.f));
      lastGain.assign(numOutputs, std::vector<Flt>(srcChannels, 0.f));
      initGainScratch.assign(numOutputs, 0.f);

      gainDirty = true;
      haveLastPosition = false;
    }

    void panner::reset() {
      // Re-arm the smoothing so a re-used slot's new note fades cleanly from the
      // gain state left behind, and force a fresh gain recompute.
      gainDirty = true;
      haveLastPosition = false;
    }

    void panner::update(const Pos& position, bool relative) {
      if (numOutputs == 0) return; // not sized yet

      // Only recompute when the pan inputs actually changed — a stationary voice
      // recomputes once and then reuses the cache (issue #212 amortization).
      if (haveLastPosition && position == lastPosition && relative == lastRelative) return;
      lastPosition = position;
      lastRelative = relative;
      haveLastPosition = true;

      Pos newPos = position * INTERNAL::Settings().distanceFactor;

      if (relative) {
        distance = Dist(Pos(0), newPos);
      } else {
        distance = Dist(newPos, INTERNAL::ListenerImpl().newPos);
      }

      Pos dir = relative ? newPos : newPos - INTERNAL::ListenerImpl().newPos;
      Pos listenerForward = relative ? Pos(0) : INTERNAL::ListenerImpl().forward.load();
      angle = computeSourceAngle(relative, dir, listenerForward);
      horizFraction = computeHorizontalFraction(dir);

      gainDirty = true;
    }

    void panner::computeFinalGains() {
#ifdef _MSC_VER
#pragma warning(disable : 4258)
#endif
      // Rolloff attenuation depends only on distance/size/rolloffScale; hoisted
      // out of the source-channel loop exactly like the sound path.
      Flt dist = distance - size;
      if (dist < 0) dist = 0;
      Flt correctPower = 1 / ::pow(dist, (2 * INTERNAL::Settings().rolloffScale));
      if (correctPower > 1) correctPower = 1;

      for (UInt x = 0; x < srcChannels; x++) {
        // Voices are mono in this epic, so there is no multichannel spread term
        // (spread == 0 -> spreadAdjust == 0). Kept explicit to mirror the sound
        // path's structure.
        const Flt spreadAdjust = 0.f;

        for (UInt i = 0; i < numOutputs; i++) {
          if (spkIsLFE[i]) continue; // LFE is not azimuth-panned
          Flt initPan = (1 + horizFraction * ::cos(spkAngle[i] - (angle + spreadAdjust))) * 0.5f;
          initGainScratch[i] = initPan / spkEffective[i];
        }
        Flt power = 0;
        for (UInt i = 0; i < numOutputs; i++) {
          if (spkIsLFE[i]) continue;
          power += static_cast<Flt>(::pow(initGainScratch[i], 2));
        }

        for (UInt j = 0; j < numOutputs; ++j) {
          if (spkIsLFE[j]) continue; // leave the LFE buffer silent
          Flt ratio = computePanRatio(initGainScratch[j], power, realSpeakers);
          Flt finalGain = static_cast<Flt>(::sqrt(correctPower * ratio));
          finalGainCache[j][x] = finalGain;
        }
      }
    }

    void panner::spread(MULTICHANNELBUFFER& src, const Flt* fader, MULTICHANNELBUFFER& dest) {
      if (numOutputs == 0) return;
      if (gainDirty) {
        computeFinalGains();
        gainDirty = false;
      }

      const UInt srcN = (static_cast<UInt>(src.size()) < srcChannels)
                            ? static_cast<UInt>(src.size())
                            : srcChannels;
      const UInt destN = (static_cast<UInt>(dest.size()) < numOutputs)
                             ? static_cast<UInt>(dest.size())
                             : numOutputs;

      for (UInt x = 0; x < srcN; x++) {
        Flt* s = src[x].getPtr();
        UInt length = src[x].getLength();
        for (UInt j = 0; j < destN; ++j) {
          if (spkIsLFE[j]) continue; // leave the LFE buffer silent
          UInt n = (length < dest[j].getLength()) ? length : dest[j].getLength();
          gainAccumulate(s, fader, dest[j].getPtr(), n, lastGain[j][x], finalGainCache[j][x]);
        }
      }
    }

  } // namespace DSP
} // namespace YSE
