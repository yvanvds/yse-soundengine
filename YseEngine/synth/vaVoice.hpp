/*
  ==============================================================================

    vaVoice.hpp
    Virtual-analog + wavetable synthesiser voice for YSE::synth (issue #175).

    A SYNTH::dspVoice subclass covering the classic subtractive-synth space:
    two-to-three oscillators (saw / pulse-with-width / triangle / sine / noise
    / wavetable-morph) through a Moog-style resonant ladder low-pass, shaped by
    an amplitude ADSR and an independent filter ADSR, with one LFO routable to
    pitch, cutoff and wavetable position, plus velocity routing to amplitude
    and cutoff.

    See docs/design/synth_core.md §3 for the voice contract and issue #175 for
    the feature scope.

  ==============================================================================
*/

#ifndef YSE_SYNTH_VAVOICE_HPP
#define YSE_SYNTH_VAVOICE_HPP

#include <memory>
#include <vector>

#include "dspVoice.hpp"
#include "../dsp/ladderFilter.hpp"
#include "../dsp/lfo.hpp"
#include "../dsp/wavetable.hpp"

namespace YSE {
  namespace SYNTH {

    /** @brief Oscillator waveform modes for ``vaVoice``. */
    enum VA_WAVEFORM {
      VA_SAW, ///< Band-limited sawtooth.
      VA_PULSE, ///< Band-limited pulse with variable width (PWM).
      VA_TRIANGLE, ///< Band-limited triangle.
      VA_SINE, ///< Sine.
      VA_NOISE, ///< White noise.
      VA_WAVETABLE, ///< Morph across the wavetable bank (see ``vaParams``).
    };

    /**
     *  @brief Live-settable patch shared by every voice of one synth.
     *
     *  A ``vaParams`` is the "sound" — all continuous parameters are atomics
     *  read on the audio thread, so they can be tweaked from a control thread
     *  while voices play, glitch-free (no allocation, no locks). All voices of
     *  a synth share **one** ``vaParams`` (the prototype's, carried across
     *  ``clone()`` by shared pointer), while each voice keeps its own
     *  independent oscillator phase, filter state and envelope state.
     *
     *  Retain the shared pointer (``vaVoice::patch()``) if you want to keep
     *  editing the patch after the prototype has been handed to
     *  ``synth::addVoices`` and destroyed.
     *
     *  The wavetable bank (used by ``VA_WAVETABLE`` mode) is built once and is
     *  intended to be populated **before** playback via ``loadWavetable`` —
     *  like voice allocation, table (re)building is a setup-thread operation,
     *  not an audio-thread one. The morph *position* is the live control.
     */
    class API vaParams {
    public:
      /** @brief Number of oscillators per voice. */
      static const int kNumOsc = 3;

      /** @brief Construct a sensible default patch (osc 1 saw, gentle filter). */
      vaParams();

      vaParams(const vaParams&) = delete;
      vaParams& operator=(const vaParams&) = delete;

      // ---- oscillator section (per oscillator) -------------------------------
      aInt oscWave[kNumOsc]; ///< VA_WAVEFORM per oscillator.
      aFlt oscDetune[kNumOsc]; ///< Detune in semitones.
      aFlt oscLevel[kNumOsc]; ///< Mix level in [0, 1].
      aFlt oscPulseWidth[kNumOsc]; ///< Pulse width in (0, 1) for VA_PULSE.
      aFlt wavetablePosition; ///< Morph position in [0, 1] for VA_WAVETABLE.

      // ---- ladder filter -----------------------------------------------------
      aFlt cutoff; ///< Base cutoff in Hz.
      aFlt resonance; ///< Resonance in [0, 1].
      aFlt keyTracking; ///< Key-follow in [0, 1] (0 = fixed, 1 = full tracking).
      aFlt filterEnvAmount; ///< Filter-envelope depth in octaves (may be negative).
      aFlt filterVelAmount; ///< Velocity → cutoff depth in octaves at full velocity.

      // ---- amplitude envelope (seconds; sustain in [0, 1]) -------------------
      aFlt ampAttack, ampDecay, ampSustain, ampRelease;
      aFlt ampVelAmount; ///< Velocity → amplitude amount in [0, 1].

      // ---- filter envelope (seconds; sustain in [0, 1]) ----------------------
      aFlt filterAttack, filterDecay, filterSustain, filterRelease;

      // ---- LFO ---------------------------------------------------------------
      aInt lfoType; ///< DSP::LFO_TYPE shape.
      aFlt lfoRate; ///< LFO rate in Hz.
      aFlt lfoToPitch; ///< LFO → pitch depth in semitones.
      aFlt lfoToCutoff; ///< LFO → cutoff depth in octaves.
      aFlt lfoToWavetable; ///< LFO → wavetable-position depth in [0, 1].

      // ---- output ------------------------------------------------------------
      aFlt gain; ///< Master voice gain in [0, 1].

      // ---- shared oscillator source tables -----------------------------------
      // Band-limited single-cycle tables shared (read-only on the audio thread)
      // by every voice. Built once in the constructor.
      DSP::wavetable sawTable;
      DSP::wavetable triTable;
      DSP::wavetable sineTable;

      /**
       *  @brief Wavetable bank morphed across by VA_WAVETABLE mode.
       *
       *  Defaults to a small set of single-cycle shapes so morph works out of
       *  the box. Populate with AKWF-style single-cycle tables via
       *  ``loadWavetable`` before playback.
       */
      std::vector<DSP::wavetable> wtBank;

      /**
       *  @brief Install a single-cycle waveform into bank slot ``slot``.
       *
       *  ``cycle`` is one period of a normalised waveform (any length — AKWF
       *  tables are 600 samples). Slots beyond the current bank size are
       *  appended. **Setup-thread only** — this allocates/reshapes table
       *  storage and must not be called while the voice is rendering.
       */
      void loadWavetable(int slot, const std::vector<Flt>& cycle);

      /** @brief Number of tables currently in the morph bank. */
      int wavetableCount() const {
        return static_cast<int>(wtBank.size());
      }
    };

    /**
     *  @brief Lightweight real-time linear ADSR.
     *
     *  A minimal segment-based envelope used for both the amplitude and filter
     *  contours. Unlike ``DSP::ADSRenvelope`` (breakpoint list + an allocating
     *  ``generate()``), this recomputes its per-sample slopes from the current
     *  patch times every block, so envelope times are live-settable with no
     *  allocation — exactly what the shared ``vaParams`` requires. Internal to
     *  ``vaVoice``.
     */
    class vaADSR {
    public:
      /** @brief (Re)compute per-sample slopes from times (seconds) + sustain. */
      void configure(Flt attack, Flt decay, Flt sustain, Flt release, Flt sampleRate);

      /** @brief Begin the attack segment (from the current level — no click). */
      void gateOn();

      /** @brief Begin the release segment from the current level. */
      void gateOff();

      /** @brief Advance one sample and return the new level. */
      Flt tick();

      /** @brief Advance ``n`` samples (block-rate use). */
      void advance(int n);

      /** @brief Current level in [0, 1]. */
      Flt level() const {
        return lvl;
      }

      /** @brief Whether the envelope has returned to rest after a release. */
      bool idle() const {
        return stage == IDLE;
      }

      /** @brief Reset to rest (level 0). */
      void reset() {
        stage = IDLE;
        lvl = 0.f;
      }

    private:
      enum Stage { IDLE, ATTACK, DECAY, SUSTAIN, RELEASE };
      Stage stage = IDLE;
      Flt lvl = 0.f;
      Flt sus = 0.7f;
      Flt aInc = 1.f; // per-sample attack slope (toward 1)
      Flt dInc = 1.f; // per-sample decay slope (toward sustain)
      Flt rSec = 0.1f; // release time in seconds (rate resolved at gateOff)
      Flt rInc = 1.f; // per-sample release slope (toward 0)
      Flt sr = 44100.f;
    };

    /**
     *  @brief Virtual-analog + wavetable synthesiser voice.
     *
     *  Derives ``SYNTH::dspVoice`` and consumes the synth-core voice contract
     *  (intent-driven lifecycle, ``clone()`` prototype, atomic note inputs).
     *  All tone parameters live in a shared ``vaParams`` patch; construct one
     *  prototype, dial in the patch, then hand it to ``synth::addVoices`` — the
     *  clones share the patch and stay editable through ``patch()``.
     *
     *  Everything the audio thread needs is allocated up front (tables in the
     *  patch, per-voice buffers/filter/LFO in the constructor), so ``process``
     *  and ``clone``'s copy stay allocation-free on their respective threads.
     */
    class API vaVoice : public dspVoice {
    public:
      /** @brief Construct a voice with a fresh default patch. */
      vaVoice(int outputChannels = 1);

      /** @brief The shared, live-settable patch. Retain to keep editing after the prototype is
       * gone. */
      std::shared_ptr<vaParams> patch() const {
        return params;
      }

      /** @brief Convenience reference to the shared patch. */
      vaParams& parameters() {
        return *params;
      }

      // ---- dspVoice contract -------------------------------------------------

      /** @brief Render one block, honouring and settling ``intent``. Audio-thread only. */
      void process(SOUND_STATUS& intent) override;

      /** @brief Return a new voice sharing this voice's patch, with fresh DSP state. Setup-thread
       * only. */
      dspVoice* clone() override;

    protected:
      /** @brief Copy-construct: share the patch, rebuild independent DSP state. */
      vaVoice(const vaVoice& other);

    private:
      // One oscillator sample at normalised phase (0..1) for oscillator index.
      // Non-const because VA_NOISE advances the per-voice RNG state.
      Flt renderOsc(int index, Dbl phase, Flt pulseWidth, Flt wtPos);
      // Linear-interpolated read of a single-cycle table at normalised phase.
      static Flt readTable(DSP::wavetable& t, Dbl phase);

      std::shared_ptr<vaParams> params;

      // ---- per-voice state (independent per clone) ---------------------------
      Dbl oscPhase[vaParams::kNumOsc];
      UInt noiseState;
      DSP::lfo lfoOsc;
      vaADSR ampEnv;
      vaADSR filEnv;
      DSP::ladderFilter filter;

      enum Phase { IDLE, PLAYING, RELEASING };
      Phase phase;
    };

  } // namespace SYNTH
} // namespace YSE

#endif // YSE_SYNTH_VAVOICE_HPP
