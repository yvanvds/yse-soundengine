/*
  ==============================================================================

    source.h
    Created: 31 Jan 2014 2:53:05pm
    Author:  yvan

  ==============================================================================
*/

#ifndef SOURCE_H_INCLUDED
#define SOURCE_H_INCLUDED

#include <vector>
#include <memory>
#include "buffer.hpp"
#include "../headers/enums.hpp"
#include "lfo.hpp"
#include "math.hpp"

namespace YSE {
  /// @cond INTERNAL
  namespace SOUND {
    class implementationObject;
  }
  /// @endcond

  namespace DSP {

    /**
     *  @brief Base class for chainable DSP effects.
     *
     *  Subclass and implement ``create`` and ``process`` to define a custom
     *  effect that can be linked into a sound's processing chain via
     *  ``YSE::sound::setDSP``. The base class provides bypass control, a
     *  wet/dry impact slider, and a built-in LFO that can modulate the
     *  effect's parameters.
     *
     *  Chain instances together with ``link``: ``a.link(b); b.link(c);``.
     */
    class API dspObject {
    public:
      dspObject();
      virtual ~dspObject();

      /** @brief One-time setup before processing starts.
       *
       *  Allocate buffers and any other heavyweight state here — never inside
       *  ``process``. The engine calls ``create`` from a setup thread before
       *  the first ``process`` call.
       */
      virtual void create() = 0;

      /** @brief Process one audio block in place.
       *
       *  Runs on the audio thread. Must be allocation-free and reasonably
       *  bounded in time. Subclasses typically call ``createIfNeeded()`` at
       *  the start, do their work, then call ``calculateImpact()`` at the end
       *  to apply the wet/dry mix.
       *
       *  ### N-channel processing contract
       *
       *  ``buffer`` is a ``MULTICHANNELBUFFER`` (a ``std::vector`` of
       *  ``DSP::buffer``, one per audio channel). Implementations MUST:
       *
       *  - **Process every channel** ``buffer[0] .. buffer[buffer.size()-1]``,
       *    not just ``buffer[0]``. Each channel is an independent signal; state
       *    that remembers per-sample history (filter memory, delay lines,
       *    oscillator phase driven by the audio) must be kept *per channel* so
       *    it does not bleed between them. The ``DSP::perChannel<State>`` helper
       *    exists for this fan-out; ``reverbDSP`` (per-channel ``reverbChannel``
       *    structs) is the reference example.
       *  - **Tolerate a changing channel count between calls.** The device can
       *    restart with a different output-channel count, so ``buffer.size()``
       *    may differ from the previous call. Re-sizing per-channel state to
       *    match is allowed to allocate — that is the device-restart path, not
       *    the steady state — but this is the *only* place allocation is
       *    permitted. Do it via ``perChannel::ensure(buffer.size())`` (or an
       *    equivalent size check) so a steady-state call with an unchanged
       *    count allocates nothing.
       *  - **Allocate only in** ``create()`` **or on that resize path.** The
       *    common per-block scratch (block-length ``result`` buffers, etc.) is
       *    (re)sized only when the block length changes, exactly as the input
       *    channels are.
       *
       *  A single-channel buffer is the degenerate case: modules upgraded to
       *  this contract are bit-identical to their former mono-only behaviour
       *  when ``buffer.size() == 1``.
       */
      virtual void process(MULTICHANNELBUFFER& buffer) = 0;

      /** @brief Insert ``next`` after this object in the processing chain.
       *
       *  If a downstream object is already linked, ``next`` is spliced between
       *  this object and the existing next.
       */
      void link(dspObject& next);

      /** @brief Next object in the processing chain, or ``nullptr`` if this is the tail. */
      dspObject* link();

      /** @brief Detach the forward edge of the chain at this object.
       *
       *  Clears this object's ``next`` pointer and the detached neighbour's
       *  ``previous`` back-pointer, so the neighbour becomes a standalone
       *  (potential) chain head. A no-op when nothing is linked. It does NOT
       *  touch ``calledfrom`` — the sound/channel attachment back-reference is
       *  owned by ``setDSP`` / engine teardown and only meaningful on a chain
       *  head.
       *
       *  RT-safety: the audio thread walks ``next`` pointers lock-free every
       *  block. To restructure a chain that is attached to a live sound or
       *  channel, detach the chain from its owner first (``setDSP(nullptr)``
       *  — a pointer swap applied on the audio thread), ``unlink()`` and
       *  re-``link()`` the objects into the new order, then re-attach the new
       *  head. That keeps the audio thread from ever observing a half-rewired
       *  chain; effect DSP state survives because the objects themselves are
       *  untouched.
       */
      void unlink();

      /** @brief Bypass this effect when ``true``. Bypassed effects still run but pass input through
       * unchanged. */
      dspObject& bypass(Bool value) {
        _bypass = value;
        return *this;
      }

      /** @brief Whether this effect is bypassed. */
      Bool bypass() {
        return _bypass;
      }

      /** @brief Wet/dry mix in [0.0, 1.0]. 0 is fully dry, 1 is fully processed. */
      dspObject& impact(Flt value) {
        _impact = value;
        return *this;
      }

      /** @brief Current wet/dry mix value. */
      Flt impact() {
        return _impact;
      }

      /** @brief Set the modulation LFO shape. ``LFO_NONE`` disables modulation. */
      dspObject& lfoType(LFO_TYPE type) {
        _lfoType = type;
        return *this;
      }

      /** @brief Current LFO shape. */
      LFO_TYPE lfoType() {
        return _lfoType;
      }

      /** @brief Set the LFO frequency in Hz. */
      dspObject& lfoFrequency(Flt value) {
        _lfoFrequency = value;
        return *this;
      }

      /** @brief Current LFO frequency. */
      Flt lfoFrequency() {
        return _lfoFrequency;
      }

      /** @internal Engine-managed back-pointer. Treat as private. */
      dspObject** calledfrom;

    protected:
      /** @brief Subclass helper: run ``create`` lazily on first ``process`` call. */
      void createIfNeeded();

      /** @brief Subclass helper: access the current LFO buffer. */
      buffer& getLFO();

      /** @brief Subclass helper: apply the wet/dry mix between ``in`` and ``filtered``. */
      void calculateImpact(buffer& in, buffer& filtered);

    private:
      dspObject* next;
      dspObject* previous;
      Bool _bypass;
      Bool _needsCreate;
      aFlt _impact;

      std::shared_ptr<lfo> lfoOsc;
      std::shared_ptr<inverter> invertedImpact;
      LFO_TYPE _lfoType;
      aFlt _lfoFrequency;
    };

    /**
     *  @brief Base class for DSP-driven sound sources.
     *
     *  Where ``dspObject`` is an effect, ``dspSourceObject`` is a generator —
     *  it produces audio rather than transforming it. Subclass and implement
     *  ``process`` and ``frequency`` to drive a ``YSE::sound`` from procedural
     *  audio (see ``YSE::sound::create`` for the lifetime contract).
     */
    class API dspSourceObject {
    public:
      /** @brief Output buffers, one per audio channel. */
      std::vector<buffer> samples;

      /** @brief Construct with ``buffers`` audio channels (1 = mono, 2 = stereo, ...). */
      dspSourceObject(Int buffers = 1);

      /** @brief Fill ``samples`` with the next audio block.
       *
       *  @param intent The current playback intent (start, stop, pause, etc.)
       *                — subclasses use this to drive amplitude envelopes and
       *                trigger transitions.
       */
      virtual void process(SOUND_STATUS& intent) = 0;

      /** @brief Set the fundamental frequency of the generator. */
      virtual void frequency(Flt value) = 0;
    };

  } // namespace DSP
} // namespace YSE

#endif // SOURCE_H_INCLUDED
