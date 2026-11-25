/*
  ==============================================================================

    sfzSampler.hpp
    A polyphonic SFZ instrument played by MIDI-style notes (issue #1159).

  ==============================================================================
*/

#ifndef YSE_DSP_SAMPLING_SFZSAMPLER_HPP
#define YSE_DSP_SAMPLING_SFZSAMPLER_HPP

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include "../../headers/constants.hpp"
#include "../../headers/defines.hpp"
#include "../../headers/types.hpp"
#include "../buffer.hpp"

namespace YSE {
  namespace SYNTH {
    class samplerInstrument;
  } // namespace SYNTH

  namespace DSP {

    /**
     *  @brief A polyphonic SFZ instrument played by MIDI-style notes, out of
     *         a stereo pair: what the patcher's ``~sfizz`` does.
     *
     *  The instrument is the engine's own SFZ model: ``loadSFZ`` parses the
     *  file into the region table of ``dsp/sfzModel.hpp`` and
     *  ``SYNTH::samplerInstrument`` decodes every sample it names into
     *  memory. Each note is a ``SYNTH::samplerVoice`` (key and velocity
     *  select the regions; the regions' pitch, loops, round robin, choke
     *  groups, crossfades and amplitude envelopes apply as in a
     *  ``YSE::synth``). This class owns a pool of those voices and plays
     *  them without a ``YSE::synth`` or a ``YSE::sound``, so it works in a
     *  patcher or in any C++ render loop.
     *
     *  **Threads.** The control side (``load``, ``instrument``, ``reload``,
     *  ``voices(UInt)``, ``collect``) reads files and allocates: call it off
     *  the audio thread. It builds the instrument and a pool of voices for
     *  it under one lock and hands the pool to the audio side through one
     *  atomic slot, which the next block takes; the pool it replaces is
     *  freed by a later ``collect``, never by the audio thread. Notes
     *  sounding from the replaced pool stop with it. The other getters of the
     *  control side may be called from any thread. Everything else is the
     *  audio side: one thread, the one that calls ``operator()``. It
     *  allocates nothing, takes no lock and reads no file.
     *
     *  **Notes.** ``noteOn`` starts a voice: a free one, else the oldest one
     *  released, else the oldest, which is taken over at once. A note that
     *  starts and stops before a block is still heard (its release starts
     *  after its first block). ``noteOff`` releases every voice holding the
     *  key, unless the sustain pedal is down: then they sound on until it
     *  comes up. A note with no matching region is silent. ``midi`` reads
     *  raw channel messages on any channel: note on and off, controllers 64
     *  (sustain), 120 (all sound off), 121 (reset: pedal up, bend 0) and 123
     *  (all notes off), and the pitch wheel (two semitones either way, the
     *  sampler voice's bend range).
     *
     *  **Output.** ``operator()`` renders a block into ``left()`` and
     *  returns it; ``right()`` is filled by the same call. The voice mixes
     *  its regions to one channel (a stereo sample is summed); the pan of
     *  the first region the note plays places it, with the balance law
     *  ``left = min(1, 1 - pan / 100)``, ``right = min(1, 1 + pan / 100)``,
     *  so a centred region is at full level in both. The output is then
     *  scaled by ``volume()`` (dB), moved toward a new value by a one-pole
     *  smoother with a 10 ms time constant at the live rate.
     *
     *  **Tuning.** Key ``n`` sounds at ``pitch(n)`` Hz: ``base()`` Hz at key
     *  ``reference()``, and the keys around it by ``scale()``, a list of the
     *  degrees above ``middle()`` in cents whose last value is the period
     *  (Scala's convention). Degree ``d`` of period ``p`` above the middle
     *  key lies ``k p + c(d)`` cents above it, ``k`` periods up. The default
     *  (an empty scale, 440 Hz at key 69) is equal temperament at A440, the
     *  sampler voice's own. The key still selects the regions; the tuning
     *  moves the pitch each voice plays them at (``samplerVoice::tune``).
     *
     *  **Sample rate.** The voices take the live ``SAMPLERATE`` at every
     *  note, and the volume smoother's coefficient is rebuilt when it
     *  changes.
     */
    class API sfzSampler {
    public:
      /** @brief The voices a new sampler holds. */
      static constexpr UInt DEFAULT_VOICES = 64;
      /** @brief The most voices a sampler holds. */
      static constexpr UInt MAX_VOICES = 1024;
      /** @brief MIDI keys, 0 to 127. */
      static constexpr UInt KEYS = 128;
      /** @brief The most degrees a scale has, its period included. */
      static constexpr UInt MAX_DEGREES = 128;

      /** @brief A sampler with @p voices voices (1 to ``MAX_VOICES``) and no
       *         instrument. Control thread. */
      explicit sfzSampler(UInt voices = DEFAULT_VOICES);
      ~sfzSampler();
      sfzSampler(const sfzSampler&) = delete;
      sfzSampler& operator=(const sfzSampler&) = delete;

      // ---- the control side: off the audio thread ------------------------

      /** @brief Read the .sfz file @p path and decode its samples, then hand
       *         the instrument to the next block. False, and the instrument
       *         that plays kept, when the file cannot be read or has no
       *         playable region. Sample paths are relative to the file. */
      bool load(const std::string& path);

      /** @brief Play an instrument built in code (or loaded elsewhere):
       *         @p playable must be valid. @p name is what ``file()`` says.
       *         False, and nothing changed, for a null or invalid one. */
      bool instrument(std::shared_ptr<SYNTH::samplerInstrument> playable,
                      const std::string& name = std::string());

      /** @brief Read the last file ``load`` read again. False without one,
       *         or when it fails. */
      bool reload();

      /** @brief Hold @p count voices (clamped to 1 .. ``MAX_VOICES``): a new
       *         pool for the instrument that plays, handed to the next block
       *         (its sounding notes stop). Returns false when nothing changed
       *         (the same count). */
      bool voices(UInt count);

      /** @brief The voices the sampler holds. Any thread. */
      UInt voices() const {
        return voiceCount.load(std::memory_order_acquire);
      }

      /** @brief The file of the instrument the control side last handed
       *         over (empty for none). Any thread, takes the lock. */
      std::string file() const;

      /** @brief Whether an instrument has been handed over. Any thread. */
      bool loaded() const {
        return regionCount.load(std::memory_order_acquire) > 0;
      }

      /** @brief The regions of the instrument handed over last, 0 for none.
       *         Any thread. */
      UInt regions() const {
        return regionCount.load(std::memory_order_acquire);
      }

      /** @brief Counts the pools handed over: it changes with each load,
       *         instrument or voice count. Any thread. */
      std::uint64_t version() const {
        return changes.load(std::memory_order_acquire);
      }

      /** @brief Free the pool the audio side let go. Control thread. */
      void collect();

      /** @brief Whether a pool waits to be freed by ``collect``. Any thread. */
      bool collectPending() const {
        return retired.load(std::memory_order_acquire) != nullptr;
      }

      // ---- the audio side: one thread --------------------------------------

      /** @brief Start key @p note (0 to 127) at @p velocity (0 to 1; 0 is a
       *         note off). */
      void noteOn(Int note, Flt velocity);

      /** @brief Release key @p note (held on while the pedal is down). */
      void noteOff(Int note);

      /** @brief The sustain pedal down or up; up releases the notes it held. */
      void sustain(bool down);

      /** @brief Whether the sustain pedal is down. */
      bool sustain() const {
        return pedal;
      }

      /** @brief The pitch wheel, -1 to 1 (clamped): two semitones either way. */
      void pitchBend(Flt position);

      /** @brief The pitch wheel, -1 to 1. */
      Flt pitchBend() const {
        return bend;
      }

      /** @brief Release every note, the pedal's too. */
      void allNotesOff();

      /** @brief Silence every voice at once, without a release. */
      void allSoundOff();

      /** @brief One raw MIDI channel message: @p status with its channel
       *         (ignored) and data bytes 0 to 127 (see the class notes). */
      void midi(Int status, Int data1, Int data2);

      /** @brief The output volume in dB (0 by default), smoothed. */
      sfzSampler& volume(Flt decibels);
      /** @brief The output volume in dB. */
      Flt volume() const {
        return volumeDb;
      }

      /** @brief The frequency of key ``reference()``, in Hz (440 by
       *         default); 0 or less, or not finite, is ignored. */
      sfzSampler& base(Flt hz);
      /** @brief The frequency of key ``reference()``, in Hz. */
      Flt base() const {
        return baseHz;
      }

      /** @brief The key that sounds at ``base()`` Hz, 0 to 127 (69). */
      sfzSampler& reference(Int note);
      /** @brief The key that sounds at ``base()`` Hz. */
      Int reference() const {
        return referenceKey;
      }

      /** @brief The key a scale starts on, 0 to 127 (60). */
      sfzSampler& middle(Int note);
      /** @brief The key a scale starts on. */
      Int middle() const {
        return middleKey;
      }

      /** @brief The scale: @p count degrees above ``middle()`` in cents, the
       *         last of them the period (at most ``MAX_DEGREES``; the rest
       *         is ignored). 0 degrees, or a period that is not above 0, is
       *         equal temperament. */
      sfzSampler& scale(const Flt* cents, UInt count);
      /** @brief How many degrees the scale has; 0 for equal temperament. */
      UInt scaleSize() const {
        return degreeCount;
      }

      /** @brief One pitch of a Scala scale, in cents: a number with a
       *         decimal point is cents, an integer or ``a/b`` a ratio
       *         (Scala's rule). False for anything else, and for a ratio
       *         that is not above 0. */
      static bool scalaPitch(const char* text, std::size_t length, Flt& cents);

      /** @brief The degrees of the text of a Scala ``.scl`` file, in cents,
       *         into @p cents (at most @p capacity): lines starting with
       *         ``!`` are comments, the first other line is the description,
       *         the next the number of degrees, then one pitch per line (its
       *         first word). Returns the degrees read, 0 when the text is no
       *         scale. */
      static UInt scalaDegrees(const std::string& text, Flt* cents, UInt capacity);

      /** @brief The frequency key @p note sounds at under the tuning, in Hz,
       *         before the regions' own tuning and the pitch wheel. */
      Flt pitch(Int note) const;

      /** @brief Render one block of @p length samples (at most
       *         ``STANDARD_BUFFERSIZE``; more is cut to it) and return the
       *         left channel. */
      buffer& operator()(UInt length = STANDARD_BUFFERSIZE);

      /** @brief The left channel of the last block. */
      buffer& left() {
        return outLeft;
      }
      /** @brief The right channel of the last block. */
      buffer& right() {
        return outRight;
      }

      /** @brief The voices sounding after the last block. */
      UInt active() const {
        return sounding;
      }

    private:
      struct pool;
      struct slot;

      // Control side.
      bool Publish(std::shared_ptr<SYNTH::samplerInstrument> playable, const std::string& name,
                   UInt count);
      // Audio side.
      void Take();
      void Retune();
      slot* Claim(UInt& index);

      mutable std::mutex edit; // the control side
      std::shared_ptr<SYNTH::samplerInstrument> playing; // what the last pool plays
      std::string playingFile;
      std::string lastPath; // the last file load() read
      std::atomic<UInt> voiceCount{DEFAULT_VOICES};
      std::atomic<UInt> regionCount{0};
      std::atomic<std::uint64_t> changes{0};

      std::atomic<pool*> incoming{nullptr};
      std::atomic<pool*> retired{nullptr};
      pool* current = nullptr; // the audio side's

      // The audio side.
      buffer outLeft;
      buffer outRight;
      std::uint64_t clock = 0; // note-on order, for stealing
      bool pedal = false;
      Flt bend = 0.f;
      UInt sounding = 0;
      Flt volumeDb = 0.f;
      Flt gainTarget = 1.f;
      Flt gain = 1.f;
      bool gainStarted = false;
      Flt gainCoef = 1.f;
      UInt gainRate = 0;

      Flt baseHz = 440.f;
      Int referenceKey = 69;
      Int middleKey = 60;
      std::array<Flt, MAX_DEGREES> degrees{};
      UInt degreeCount = 0;
      std::array<Flt, KEYS> keyCents{}; // each key's offset from A440 equal temperament
    };

  } // namespace DSP
} // namespace YSE

#endif // YSE_DSP_SAMPLING_SFZSAMPLER_HPP
