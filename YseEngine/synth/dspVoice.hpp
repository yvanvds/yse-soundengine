/*
  ==============================================================================

    dspVoice.hpp
    Voice base class for the YSE::synth subsystem.

    Implements §3 ("The voice model") of docs/design/synth_core.md. A dspVoice
    is a DSP::dspSourceObject the user derives from: the engine owns polyphony,
    allocation and lifecycle; the subclass owns only what one voice sounds like.

  ==============================================================================
*/

#ifndef YSE_SYNTH_DSPVOICE_HPP
#define YSE_SYNTH_DSPVOICE_HPP

#include "../dsp/dspObject.hpp"
#include "../dsp/math.hpp"
#include "../headers/types.hpp"

namespace YSE {
  namespace SYNTH {

    /// @cond INTERNAL
    // Forward declaration only — the audio-thread implementation object that
    // owns and drives voices arrives with the synth subsystem (#153). It is
    // named here purely so voices can grant it friendship (it writes
    // _aftertouch and drives the per-voice intent). Declaring a friend that is
    // never defined in this translation unit is well-formed C++.
    class implementationObject;
    /// @endcond

    /**
     *  @brief Base class for user-subclassable synthesiser voices.
     *
     *  Derive from ``dspVoice`` to define what a single note sounds like. The
     *  base is a ``DSP::dspSourceObject`` — it already provides the
     *  ``samples`` output buffers and the ``process(SOUND_STATUS&)`` entry
     *  point the sound renderer needs — extended with the pieces the voice
     *  allocator relies on: a ``clone()`` prototype hook, atomic
     *  frequency / velocity / aftertouch inputs, and a default atomic
     *  ``frequency()`` that stores the note as Hz.
     *
     *  The reference implementation is ``SYNTH::sineVoice`` (a sine shaped by an
     *  ADSR envelope). See docs/design/synth_core.md §3 for the full contract.
     */
    class API dspVoice : public DSP::dspSourceObject {
    public:
      /** @brief Construct a voice with ``outputChannels`` output buffers. */
      dspVoice(int outputChannels = 1) : dspSourceObject(outputChannels) {}
      virtual ~dspVoice() {}

      /**
       *  @brief Fill ``samples`` for one block. **You must implement this.**
       *
       *  ``intent`` is this voice's own ``SOUND_STATUS``: ``SS_WANTSTOPLAY`` on
       *  note start, ``SS_WANTSTOSTOP`` on note release, etc. Honour it to
       *  drive your amplitude envelope, and settle it to ``SS_STOPPED`` once
       *  your release tail has finished so the allocator can free the slot.
       *
       *  Runs on the audio thread. Must be allocation-free, lock-free and
       *  non-blocking.
       */
      void process(SOUND_STATUS& intent) override = 0;

      /**
       *  @brief Return a new, fully-allocated heap instance of your derived
       *         type. **You must implement this.**
       *
       *  The typical body is ``return new MyVoice(*this);`` — a copy-construct.
       *  Every buffer, table and filter state the returned voice will touch in
       *  ``process()`` must already exist, so ``process()`` is thereafter
       *  allocation-free. Called only on the setup thread (never the audio
       *  thread); allocation here is fine.
       */
      virtual dspVoice* clone() = 0;

      // ---- delivered by the allocator (audio thread) ---------------------

      /**
       *  @brief Set the note frequency from a MIDI note number.
       *
       *  Stored internally as Hz. Called by the allocator on NOTE_ON; a voice
       *  reads the result with ``getFrequency()`` in ``process()``.
       */
      void frequency(Flt midiNote) override {
        _frequency.store(DSP::MidiToFreq(midiNote), std::memory_order_relaxed);
      }

      /** @brief Current note frequency in Hz. */
      Flt getFrequency() const {
        return _frequency.load(std::memory_order_relaxed);
      }

      /** @brief Set the note velocity, normalised to [0, 1]. */
      void velocity(Flt v) {
        _velocity.store(v, std::memory_order_relaxed);
      }

      /** @brief Current note velocity in [0, 1]. */
      Flt getVelocity() const {
        return _velocity.load(std::memory_order_relaxed);
      }

      /** @brief Current aftertouch pressure in [0, 1]. */
      Flt getAftertouch() const {
        return _aftertouch.load(std::memory_order_relaxed);
      }

    protected:
      /**
       *  @brief Copy the note atomics (and base output buffers) into a fresh,
       *         independently-owned voice.
       *
       *  Provided so a derived ``clone()`` can be a plain copy-construct
       *  (``new MyVoice(*this)``): ``std::atomic`` members are not implicitly
       *  copyable, so the base supplies this. Each instance keeps its own
       *  atomics, so a clone shares no mutable note state with its prototype.
       */
      dspVoice(const dspVoice& o) : dspSourceObject(o) {
        _frequency.store(o._frequency.load(std::memory_order_relaxed), std::memory_order_relaxed);
        _velocity.store(o._velocity.load(std::memory_order_relaxed), std::memory_order_relaxed);
        _aftertouch.store(o._aftertouch.load(std::memory_order_relaxed), std::memory_order_relaxed);
      }

    private:
      aFlt _frequency{440.f};
      aFlt _velocity{0.f};
      aFlt _aftertouch{0.f};
      friend class implementationObject; // sets _aftertouch, drives intent
    };

  } // namespace SYNTH
} // namespace YSE

#endif // YSE_SYNTH_DSPVOICE_HPP
