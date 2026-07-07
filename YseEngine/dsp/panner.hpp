/*
  ==============================================================================

    panner.hpp
    Reusable per-source spatialization component (Route 2 of
    docs/design/per_note_positioning.md, issue #169).

    The pan/distance derivation used to live as private static helpers plus
    per-sound smoothing state on SOUND::implementationObject. This component
    lifts that math out so there is exactly ONE copy of it: the sound subsystem
    calls the static helpers here (see soundImplementation.cpp forwarders) and
    the synth runs one `panner` instance per voice to spread each voice into a
    device-width aggregate bed. Two users, one implementation — they cannot
    drift (design §6).

    RT-safety: every gain vector is sized in resize(), which runs OFF the audio
    thread (slow-pool setup, or the accepted device-restart path). update() and
    spread() allocate nothing, lock nothing, and touch no I/O — the same
    discipline as a voice process().

  ==============================================================================
*/

#ifndef YSE_DSP_PANNER_HPP
#define YSE_DSP_PANNER_HPP

#include <vector>

#include "../headers/defines.hpp" // MULTICHANNELBUFFER
#include "../headers/types.hpp"
#include "../utils/vector.hpp" // Pos
#include "buffer.hpp" // DSP::buffer (MULTICHANNELBUFFER element type)

namespace YSE {
  namespace DSP {

    /**
        Per-source directional panner. Distributes a mono (or multichannel)
        source across the current device speaker layout, applying cardioid pan,
        distance rolloff and a 50-sample smoothing ramp — the same derivation
        the sound path uses, kept here as the single shared copy.
    */
    class panner {
    public:
      panner();

      // ---- pure spatialization math (moved from SOUND::implementationObject) --
      // All static, pure (state passed by argument), allocation-free and
      // unit-testable in isolation. The sound path forwards to these so the two
      // panners share one implementation and stay bit-identical.

      /** Per-speaker share of the source's power, in [0..1], with the
          antipodal-NaN guard (issue #202). */
      static Flt computePanRatio(Flt initGain, Flt power, UInt speakerCount);

      /** Pan angle of a source relative to the listener, wrapped to (-pi, pi]
          (issues #204). `relative` takes `dir` directly (listener frame);
          otherwise it is measured against `listenerForward`. */
      static Flt computeSourceAngle(bool relative, const Pos& dir, const Pos& listenerForward);

      /** Fraction of the source direction lying in the horizontal plane, in
          [0..1] — tames the zenith flyover artifact (issue #210). */
      static Flt computeHorizontalFraction(const Pos& dir);

      /** Cardioid overlap weight between two speaker angles, in [0..1]
          (issue #207). */
      static Flt computeSpeakerOverlap(Flt angleA, Flt angleB);

      /** Multiplicative doppler playback-rate ratio for a moving source/listener
          pair, clamped to a sane band (issue #208). */
      static Flt computeDopplerRatio(const Pos& sourceVel, const Pos& listenerVel, const Pos& dist,
                                     Flt dopplerScale);

      /** Virtualization priority metric — importance rises with volume and falls
          with distance (issue #205). Kept here as the shared helper even though
          virtualization stays per-aggregate on the sound side (design §5). */
      static Flt computeVirtualDist(Flt distance, Flt size, Flt volume);

      /** Fused scaled multiply-accumulate with the 50-sample smoothing ramp
          (issue #213): dest[i] += (src[i] * g[i]) * fader[i], g slewing from
          lastGain toward finalGain. Bit-identical to the old sound mix. */
      static void gainAccumulate(const Flt* src, const Flt* fader, Flt* dest, UInt length,
                                 Flt& lastGain, Flt finalGain);

      // ---- per-instance per-voice panner --------------------------------------

      /** Snapshot the current device speaker layout and size the gain vectors to
          (device outputs) x sourceChannels. Runs ONLY off the audio thread (the
          slow-pool setup, or the engine's accepted device-restart path). Calling
          it again re-snapshots the layout and re-sizes — this is how a voice's
          panner tracks a device restart. This is the ONLY method that allocates. */
      void resize(UInt sourceChannels);

      /** True once resize() has captured a speaker layout. */
      bool ready() const {
        return numOutputs > 0;
      }

      /** Device output-channel count this panner is currently sized for. */
      UInt outputs() const {
        return numOutputs;
      }

      /** Control-rate: recompute cached per-speaker gains from a new position.
          Reads the global listener. Only marks the gain cache dirty when the
          position actually changed since the last update(), so a stationary
          voice recomputes once and then reuses the cache (issue #212 amortization).
          Pure math, allocation-free — safe to call on the audio thread. */
      void update(const Pos& position, bool relative = false);

      /** Force a gain recompute on the next spread(), regardless of position.
          Used when a voice slot is re-armed for a new note so the smoothing ramp
          starts clean. */
      void reset();

      /** Audio-rate: distribute `src` (mono or multichannel) across the
          device-width `dest` bed with the 50-sample smoothing ramp, scaled by
          the per-sample `fader` block. `dest` must have at least outputs()
          channels; extra source channels beyond the sizing are ignored.
          Allocation-free, lock-free — RT-safe. */
      void spread(MULTICHANNELBUFFER& src, const Flt* fader, MULTICHANNELBUFFER& dest);

    private:
      void computeFinalGains();

      // device speaker layout snapshot, filled in resize()
      std::vector<Flt> spkAngle;
      std::vector<Flt> spkEffective;
      std::vector<Bool> spkIsLFE;
      UInt numOutputs;
      UInt realSpeakers;
      UInt srcChannels;

      // per-instance pan state; written by update(), read by spread()
      Pos lastPosition;
      bool haveLastPosition;
      bool lastRelative;
      Flt distance;
      Flt angle;
      Flt horizFraction;
      Flt size; // distance before rolloff begins
      Bool gainDirty;

      // cached gains + smoothing state, indexed [output channel][source channel]
      std::vector<std::vector<Flt>> finalGainCache;
      std::vector<std::vector<Flt>> lastGain;
      std::vector<Flt> initGainScratch; // [output], transient scratch
    };

  } // namespace DSP
} // namespace YSE

#endif // YSE_DSP_PANNER_HPP
