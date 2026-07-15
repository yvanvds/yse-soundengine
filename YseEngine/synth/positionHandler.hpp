/*
  ==============================================================================

    positionHandler.hpp
    Per-note 3D position behaviour for the YSE::synth subsystem.

    Implements §8 ("The positionHandler contract") of
    docs/design/per_note_positioning.md (issue #170). A positionHandler is the
    positional analogue of SYNTH::dspVoice: the user derives from it, the engine
    owns polyphony and lifetime, one instance is clone()-d per voice slot, and
    its hooks run on the audio thread under the same real-time discipline as a
    voice process(). A handler steers a voice's POSITION only — it never
    produces audio, never touches keyboard state, and never forwards
    controllers; the synth owns all of that and hands the live values to the
    handler through the read-only voiceContext for it to react to.

  ==============================================================================
*/

#ifndef YSE_SYNTH_POSITIONHANDLER_HPP
#define YSE_SYNTH_POSITIONHANDLER_HPP

#include "../headers/defines.hpp" // API
#include "../headers/types.hpp" // Flt, Int
#include "../utils/vector.hpp" // Pos

namespace YSE {
  namespace SYNTH {

    /// @cond INTERNAL
    // The audio-thread implementation object that owns and drives handlers. It
    // is friended so it can fill the private backing pointers of a voiceContext
    // it hands to a handler; the handler code itself only reads the public
    // scalars and the const accessors.
    class implementationObject;
    /// @endcond

    /**
     *  @brief Read-only view of one voice's live note state, handed to a
     *         positionHandler on every hook.
     *
     *  The synth fills a fresh voiceContext on the stack for each hook call —
     *  it is never a heap object and the handler never owns it. Everything on
     *  it is a value or a same-thread read; nothing here allocates or locks.
     *  Read it to modulate position (e.g. velocity -> orbit radius, aftertouch
     *  -> swarm width, a control-change -> height); you cannot write note state
     *  through it.
     */
    class voiceContext {
    public:
      Flt frequency = 0.f; ///< The sounding pitch, in Hz.
      Flt velocity = 0.f; ///< Note-on velocity, normalised to [0, 1].
      Flt aftertouch = 0.f; ///< Live aftertouch pressure for this voice, [0, 1].
      Flt pitchWheel = 0.f; ///< Live pitch-wheel position for the channel, [-1, 1].
      Int channel = 0; ///< MIDI channel that triggered the note (1..16).
      Int note = 0; ///< MIDI note number.

      /** @brief Live value of control-change ``number`` on this voice's channel,
       *  normalised to [0, 1]. Out-of-range numbers read 0. This is the synth
       *  forwarding a controller to the handler — same-thread read, no atomics. */
      Flt controller(int number) const {
        if (controllers_ == nullptr || number < 0 || number > 127) return 0.f;
        return controllers_[number];
      }

      /** @brief Shared handler parameter ``index``, written by
       *  ``synth::handlerParam()`` on the audio thread (§9). All of a synth's
       *  live handlers read the same block, so it is the natural home for a
       *  steerable swarm centre / radius. Out-of-range indices read 0. */
      Flt handlerParam(int index) const {
        if (handlerParams_ == nullptr || index < 0 || index >= numHandlerParams_) return 0.f;
        return handlerParams_[index];
      }

    private:
      // Filled by the engine immediately before a hook; handler code must not
      // touch these directly (use the accessors above).
      const Flt* controllers_ = nullptr; // -> channelState.controller[128]
      const Flt* handlerParams_ = nullptr; // -> the synth's shared param block
      int numHandlerParams_ = 0;
      friend class implementationObject;
    };

    /**
     *  @brief Base class for user-subclassable per-note position behaviours.
     *
     *  Derive from ``positionHandler`` to decide where every note of a synth
     *  lives and how it moves — a static offset, a random scatter, a swarm
     *  orbiting a moving centre — without writing engine code per note. The
     *  returned ``Pos`` is the voice's position in the same coordinate frame as
     *  a ``YSE::sound`` position; the engine feeds it straight into that voice's
     *  panner (see docs/design/per_note_positioning.md §6/§7).
     *
     *  **Lifecycle (mirrors the voice slot, §8/§10/§11).** Attach a prototype
     *  with ``synth::positionHandler(proto)``. The engine clones it once per
     *  voice slot on the setup pool (via ``clone()``) and reuses that one
     *  instance for every note the slot ever plays. When the allocator lands a
     *  note on a slot it calls ``noteOn()``; every audio block until the voice's
     *  release tail ends it calls ``update()``; on the note-off edge it calls
     *  ``onRelease()`` once. The instance is never freed at note rate — it is
     *  re-seeded by the next ``noteOn()``.
     *
     *  **Stealing (§11).** Because one instance is permanently paired with a
     *  slot and reused, a stolen slot simply gets a fresh ``noteOn()`` for the
     *  new note. Therefore **``noteOn()`` must establish the note's COMPLETE
     *  initial state** (reset every phase / counter / RNG draw) — the instance
     *  may have just finished another note. Do not assume construction state.
     *
     *  **Real-time discipline.** ``noteOn()``, ``update()`` and ``onRelease()``
     *  run on the audio thread. They must not allocate, lock, block on I/O, or
     *  log — identical rules to a voice ``process()``. Allocate everything the
     *  hooks touch in the constructor / ``clone()`` (which run only on the setup
     *  pool). Any state shared with another thread must arrive through the
     *  handler-param block or be atomic.
     *
     *  The shipped reference implementations are ``SYNTH::staticHandler``,
     *  ``SYNTH::randomSpreadHandler`` and ``SYNTH::orbitHandler`` (the last is
     *  the swarm workhorse and the template for a custom handler).
     *
     *  @see YSE::SYNTH::orbitHandler  The swarm reference handler.
     *  @see YSE::synth::positionHandler  To attach a prototype.
     */
    class API positionHandler {
    public:
      virtual ~positionHandler() {}

      /**
       *  @brief Return a new, fully-allocated heap clone of your derived type.
       *         **You must implement this.**
       *
       *  The typical body is ``return new MyHandler(*this);``. Everything the
       *  hooks will touch must already exist in the returned object, so the
       *  hooks stay allocation-free. Called only on the setup pool (never the
       *  audio thread); allocation here is fine.
       */
      virtual positionHandler* clone() = 0;

      /**
       *  @brief A note begins on this handler's slot. Return its initial
       *         position. **You must implement this.**
       *
       *  Runs on the audio thread; allocation-free. Because the instance is
       *  reused across notes (and across voice steals), this hook **must fully
       *  reinitialise** every piece of per-note state it keeps.
       */
      virtual Pos noteOn(const voiceContext& ctx) = 0;

      /**
       *  @brief Control-rate steer, called once per audio block while the note
       *         sounds (through its release tail). Return the new position.
       *         **You must implement this.**
       *
       *  ``delta`` is the block's duration in seconds (buffer length /
       *  samplerate), so advancing state by ``rate * delta`` is frame-rate
       *  independent. Runs on the audio thread; allocation-free.
       */
      virtual Pos update(const voiceContext& ctx, Flt delta) = 0;

      /**
       *  @brief The note entered its release tail (key up / note-off edge).
       *         Optional — the default is a no-op.
       *
       *  Position control CONTINUES through the tail (``update()`` keeps being
       *  called until the voice stops), so a handler that ignores release keeps
       *  moving. Override to change behaviour for the fade (e.g. drift outward,
       *  slow an orbit). Called exactly once, on the edge. Audio thread;
       *  allocation-free.
       */
      virtual void onRelease(const voiceContext& /*ctx*/) {}
    };

  } // namespace SYNTH
} // namespace YSE

#endif // YSE_SYNTH_POSITIONHANDLER_HPP
