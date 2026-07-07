/*
  ==============================================================================

    samplerVoice.hpp
    SFZ sampler synthesiser voice for YSE::synth (issue #174).

    A SYNTH::dspVoice subclass that renders the #173 RT-ready region table: it
    resolves the layer set for a note at NOTE_ON (allocation-free), pitch-tracks
    each layer through the engine's 4-point cubic kernel, loops seamlessly,
    shapes each layer with its own amplitude EG, and sums up to
    YSE_MAX_REGION_LAYERS layers into one voice. Round-robin and choke groups
    are coordinated through a shared, audio-thread-only state block carried
    across clone() by shared pointer — so the generic synth allocator stays
    untouched and the sample data is shared, never duplicated.

    Contract: docs/design/sfz_opcode_subset.md (§5 selection, §6 pitch, §7 loop,
    §8 amp EG, §10 buffer sharing, §11 samplerConfig facade).

    Real-time discipline: every per-voice buffer and every per-layer state field
    is sized in the constructor / clone() on the setup pool. process() and the
    render loop allocate nothing, take no lock, and never touch the disk — the
    whole PCM set is resident (spec §9), loaded off the audio thread before the
    prototype is handed to addVoices().

  ==============================================================================
*/

#ifndef YSE_SYNTH_SAMPLERVOICE_HPP
#define YSE_SYNTH_SAMPLERVOICE_HPP

#include <array>
#include <memory>
#include <string>
#include <vector>

#include "dspVoice.hpp"
#include "../dsp/fileBuffer.hpp"
#include "../dsp/sfzModel.hpp"
#include "../headers/types.hpp"

namespace YSE {
  namespace SYNTH {

    /**
     *  @brief One fully-resident, de-duplicated sample (all channels in RAM).
     *
     *  Built once on the setup / slow-pool thread by ``samplerInstrument::load``
     *  and immutable thereafter, so any number of voices read it concurrently on
     *  the audio thread without synchronisation (spec §10). Parallel to
     *  ``sfzInstrument::samples`` (index == sampleIndex).
     */
    struct residentSample {
      std::vector<DSP::fileBuffer> channels; ///< One buffer per source channel.
      long frames = 0; ///< Frame count (per channel).
      Flt sampleRateAdjustment = 1.0f; ///< fileRate / deviceRate (spec §6).
      bool silence = false; ///< sample=*silence — produces silence, no PCM.
      bool loaded = false; ///< PCM decoded (or silence), ready to render.
    };

    /**
     *  @brief The shared, playable SFZ instrument: region table + resident PCM.
     *
     *  All voices cloned from one prototype share **one** ``samplerInstrument``
     *  (like ``vaParams`` for ``vaVoice``): the immutable region table and PCM
     *  are read-only on the audio thread, while the small cross-note round-robin
     *  and choke coordination state is touched only inside the audio-thread
     *  render pass (single-threaded — plain ints, no atomics, no locks).
     */
    class API samplerInstrument {
    public:
      DSP::sfzInstrument model; ///< Flattened region table (immutable after load).
      std::vector<residentSample> samples; ///< Resident PCM, parallel to model.samples.

      /** @brief Decode every unique sample into RAM. Setup / slow-pool thread
       *  only — never the audio thread. Returns true if the instrument is
       *  playable (valid region table + at least one loaded sample). */
      bool load();

      /** @brief Whether this instrument is playable (valid region table + at
       *  least one resident sample). Computed, so a caller may also build the
       *  model + samples in place without going through ``load``. */
      bool valid() const {
        return model.valid && !samples.empty();
      }

      // ---- audio-thread-only cross-note state (spec §4) --------------------

      /** @brief Per-key round-robin hit counter (spec §4). Read+incremented at
       *  the NOTE_ON edge inside process(); the region table stays immutable. */
      std::array<int, 128> seqCounter{};

      /** @brief Bump the choke generation for group ``g`` (a region fired it).
       *  Returns the new generation. Groups outside [1,127] clamp into range. */
      uint32_t bumpChoke(int g);

      /** @brief Current choke generation for group ``g`` (0 if never fired). */
      uint32_t chokeGen(int g) const;

    private:
      // group id -> generation, direct-indexed and clamped. 128 covers a full
      // drum kit's worth of choke groups; ids beyond clamp to the last slot.
      static constexpr int kMaxChokeGroups = 128;
      std::array<uint32_t, kMaxChokeGroups> chokeGenerations{};
    };

    /**
     *  @brief Chainable single-sample convenience facade (spec §11).
     *
     *  Sugar that builds a one-region instrument without an ``.sfz`` file. It
     *  emits the same flattened region model the parser produces, so a
     *  ``samplerConfig`` sampler behaves identically to the equivalent
     *  hand-written one-region file.
     */
    class API samplerConfig {
    public:
      /** @brief Instrument label (identification only; not an SFZ opcode). */
      samplerConfig& name(const char* n) {
        name_ = n;
        return *this;
      }
      /** @brief Absolute path to the sample file (``sample=``). */
      samplerConfig& file(const char* f) {
        file_ = f;
        return *this;
      }
      /** @brief MIDI channel for addVoices (0 = omni). Not an SFZ opcode. */
      samplerConfig& channel(U8 c) {
        channel_ = c;
        return *this;
      }
      /** @brief Root note — the key that plays the sample untransposed. */
      samplerConfig& root(U8 rootNote) {
        root_ = rootNote;
        return *this;
      }
      /** @brief Playable key range (also the addVoices window). */
      samplerConfig& range(U8 low, U8 high) {
        low_ = low;
        high_ = high;
        return *this;
      }
      /** @brief Amplitude envelope: attack / release (seconds) and a one-shot
       *  length cap (seconds; clamps ``end`` when the region does not loop). */
      samplerConfig& envelope(Flt attack, Flt release, Flt maxLength) {
        attack_ = attack;
        release_ = release;
        maxLength_ = maxLength;
        return *this;
      }

      const std::string& name() const {
        return name_;
      }
      U8 channel() const {
        return channel_;
      }
      U8 low() const {
        return low_;
      }
      U8 high() const {
        return high_;
      }

      /** @brief Build the one-region model this facade describes into ``inst``
       *  and decode its sample. Setup thread only. Returns true on success. */
      bool build(samplerInstrument& inst) const;

    private:
      std::string name_;
      std::string file_;
      U8 channel_ = 0;
      U8 root_ = 60;
      U8 low_ = 0;
      U8 high_ = 127;
      Flt attack_ = 0.0f;
      Flt release_ = 0.1f;
      Flt maxLength_ = 10.0f;
    };

    /**
     *  @brief Amplitude EG for one layer — allocation-free DAHDSR.
     *
     *  The spec (§8) maps ``ampeg_*`` onto the engine's ``DSP::ADSRenvelope``,
     *  but that envelope's ``generate()`` allocates, and a sampler voice is
     *  reused across notes that resolve to *different* regions with different
     *  ``ampeg_*`` values — so rebuilding it at NOTE_ON would allocate on the
     *  audio path. This lightweight envelope reconfigures its per-sample slopes
     *  with no allocation, exactly the reason ``vaVoice`` introduced ``vaADSR``.
     *  It adds the delay + hold stages the full DAHDSR set needs.
     */
    class sfzADSR {
    public:
      /** @brief (Re)configure from ``ampeg_*`` times (seconds), sustain [0,1]. */
      void configure(Flt delay, Flt attack, Flt hold, Flt decay, Flt sustain, Flt release, Flt sr);
      /** @brief Begin at the delay/attack stage from silence. */
      void gateOn();
      /** @brief Enter the release stage from the current level. */
      void gateOff();
      /** @brief Advance one sample; return the new level. */
      Flt tick();
      /** @brief Whether the release has finished (envelope at rest after gateOff). */
      bool atEnd() const {
        return stage == DONE;
      }
      /** @brief Whether the envelope is producing sound (past IDLE, before DONE). */
      bool active() const {
        return stage != IDLE && stage != DONE;
      }
      /** @brief Reset to rest. */
      void reset() {
        stage = IDLE;
        lvl = 0.0f;
      }

    private:
      enum Stage { IDLE, DELAY, ATTACK, HOLD, DECAY, SUSTAIN, RELEASE, DONE };
      Stage stage = IDLE;
      Flt lvl = 0.0f;
      Flt sus = 1.0f;
      Flt sr = 44100.0f;
      long delaySamps = 0, holdSamps = 0;
      long delayCnt = 0, holdCnt = 0;
      Flt aInc = 1.0f, dInc = 1.0f, rInc = 1.0f;
      Flt rSecs = 0.1f; // release time (seconds); slope resolved at gateOff
    };

    /**
     *  @brief SFZ sampler voice — renders the shared region table for one note.
     *
     *  Load an instrument (``loadSFZ`` / ``configure``) into the prototype, then
     *  hand it to ``synth::addVoices``; every clone shares the same immutable
     *  region table and resident PCM (``instrument()``) while keeping its own
     *  independent per-layer playback state.
     */
    class API samplerVoice : public dspVoice {
    public:
      /** @brief Construct an empty mono (or wider) sampler voice. */
      samplerVoice(int outputChannels = 1);

      /** @brief Load and preload an ``.sfz`` file into this prototype. Setup
       *  thread only (parses + decodes off the audio thread). Returns true when
       *  the instrument is playable. */
      bool loadSFZ(const std::string& path);

      /** @brief Build this prototype from a ``samplerConfig`` facade. Setup
       *  thread only. Returns true when the instrument is playable. */
      bool configure(const samplerConfig& cfg);

      /** @brief The shared instrument (region table + resident PCM). Retain to
       *  keep it alive alongside the prototype and its clones. */
      std::shared_ptr<samplerInstrument> instrument() const {
        return inst;
      }

      /** @brief Attach an already-built instrument. Clones made afterwards share
       *  it (the region table + PCM are never duplicated, spec §10). Setup
       *  thread only. */
      samplerVoice& setInstrument(std::shared_ptr<samplerInstrument> i) {
        inst = std::move(i);
        return *this;
      }

      // ---- dspVoice contract -------------------------------------------------

      /** @brief Render one block, honouring and settling ``intent``. Audio-thread only. */
      void process(SOUND_STATUS& intent) override;

      /** @brief Return a new voice sharing this voice's instrument, fresh state. Setup-thread only.
       */
      dspVoice* clone() override;

      /** @brief Store the note frequency (Hz, base) and the raw MIDI note the
       *  region matcher needs. Called by the allocator on NOTE_ON. */
      void frequency(Flt midiNote) override;

      /** @brief Current raw MIDI note number for region selection. */
      int getNote() const {
        return note_.load(std::memory_order_relaxed);
      }

      // ---- test / introspection hooks (audio-thread snapshot) ----------------

      /** @brief Number of layers currently sounding. */
      int activeLayers() const;
      /** @brief Region-table index of sounding layer ``i`` (-1 if none). */
      int layerRegion(int i) const;
      /** @brief Constant NOTE_ON amplitude gain of sounding layer ``i``. */
      Flt layerGain(int i) const;
      /** @brief The round-robin hit number chosen at the last NOTE_ON. */
      int lastHit() const {
        return lastHit_;
      }

    protected:
      /** @brief Copy-construct: share the instrument, reset per-voice state. */
      samplerVoice(const samplerVoice& other);

    private:
      // Per-layer playback state. All fields are plain values sized once (the
      // layer array is fixed at YSE_MAX_REGION_LAYERS), so arming a layer at
      // NOTE_ON allocates nothing.
      struct Layer {
        bool active = false;
        bool finished = false;
        int regionIndex = -1;
        // cached pointers into the shared resident sample (read-only)
        const Flt* ch[2] = {nullptr, nullptr};
        int numCh = 0;
        long frames = 0;
        // playback
        double pos = 0.0; // fractional read position, frames
        double baseSpeed = 1.0; // resample ratio at unity pitch-wheel
        long offset = 0;
        long endFrame = 0;
        int loopMode = DSP::SFZ_NO_LOOP;
        bool looping = false; // currently wrapping the loop region
        long loopStart = 0, loopEnd = 0;
        bool sampleDone = false; // reached endFrame (non-loop / one_shot)
        // amplitude
        Flt gain = 1.0f; // volume * veltrack * xfade (constant, NOTE_ON-time)
        sfzADSR env;
        bool releasing = false;
        // choke
        int chokeGroup = 0, offBy = 0, offMode = DSP::SFZ_OFF_FAST;
        uint32_t offByBaseline = 0;
      };

      // Where the voice is in its own note arc (separate from the intent gate).
      enum Phase { IDLE, PLAYING, RELEASING };

      void startNote(); // resolve layers + arm state at the NOTE_ON edge
      void releaseNote(); // enter release on the held layers (note-off)
      void armLayer(Layer& L, int regionIndex, int note, int velocity);
      bool
      renderLayers(int blockLen); // sum active layers into samples[0]; returns true if any sound
      void applyChoke(); // scan layers for a fired choke group
      static Flt cubic(const Flt* d, long n, double pos, long loopStart, long loopEnd,
                       bool looping);

      std::shared_ptr<samplerInstrument> inst;
      std::array<Layer, DSP::YSE_MAX_REGION_LAYERS> layers;
      Phase phase = IDLE;
      aInt note_{60};
      int lastHit_ = 0;

      // Fast-choke declick: a short linear fade to silence then settle STOPPED,
      // the voice-side equivalent of the engine steal-fade for off_mode=fast.
      bool chokeFading = false;
      int chokeFadePos = 0;
      int chokeFadeSamps = 1;
    };

  } // namespace SYNTH
} // namespace YSE

#endif // YSE_SYNTH_SAMPLERVOICE_HPP
