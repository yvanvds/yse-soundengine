/*
  ==============================================================================

    sound.h
    Created: 28 Jan 2014 11:50:15am
    Author:  yvan

  ==============================================================================
*/

#ifndef SOUNDINTERFACE_H_INCLUDED
#define SOUNDINTERFACE_H_INCLUDED

#include <cstdint>
#include <string>

#include "../classes.hpp"
#include "../headers/defines.hpp"
#include "../patcher/patcher.hpp"

#if defined PUBLIC_JUCE
#include "JuceHeader.h"
#endif

namespace YSE {

  namespace SOUND {
    // Control-thread occlusion driver (issue #209). Invoked once per
    // System().update() on the application thread; runs every registered
    // sound's user occlusion callback off the audio callback thread and
    // delivers the result to the implementation via the message queue.
    // Declared here so the sound interface can befriend it; defined in
    // soundInterface.cpp.
    void updateOcclusion();
  } // namespace SOUND

  /**
   *  @brief A playable instance of an audio source.
   *
   *  Create one ``sound`` per voice you want in the scene. The audio source can be
   *  a file on disk (wav / ogg / flac and other formats depending on the platform),
   *  an in-memory buffer, a custom DSP source, or a patcher graph. Sounds can be
   *  mono, stereo, or multichannel.
   *
   *  Streaming sounds keep a dedicated buffer; non-streaming sounds share a buffer
   *  per file name — load the same file into multiple sounds and the underlying
   *  data is reused. Buffers without any remaining sound reference are flagged for
   *  deletion automatically.
   *
   *  @see YSE::channel For grouping sounds.
   *  @see YSE::DSP::dspSourceObject For procedural audio sources.
   *  @see YSE::patcher For modular-graph sources.
   */
  class API sound {
  public:
    sound();
    ~sound();

    /** @brief Sounds are non-copyable.
     *
     *  The implementation holds a back-pointer to this interface object, so its
     *  address must not change. Move it through ``std::unique_ptr`` if you need
     *  transferable ownership.
     */
    sound(const sound&) = delete;

    /**
     *  @brief Create a file-based sound.
     *
     *  Must be called before any other method on the sound. Calling twice on the
     *  same instance is a programming error.
     *
     *  @param fileName  Absolute path, or relative to the working directory.
     *  @param ch        Channel to attach the sound to. ``nullptr`` routes through
     *                   the global ``MainMix`` channel. Sounds can be moved later
     *                   with ``moveTo``.
     *  @param loop      Loop continuously when ``true``.
     *  @param volume    Initial volume in the range [0.0, 1.0].
     *  @param streaming Stream from disk instead of loading the whole file. Use
     *                   for large one-off assets; avoid for sounds played
     *                   repeatedly.
     */
    void create(const char* fileName, channel* ch = nullptr, bool loop = false, float volume = 1.0f,
                bool streaming = false);

    /**
     *  @brief Create a sound backed by an in-memory DSP buffer.
     *
     *  Useful when the audio data is generated or decoded by the application and
     *  also needs to be played back. The same buffer can back multiple sounds.
     *
     *  @param buffer The audio data. The buffer must outlive the sound.
     *  @param ch     Channel to attach to. ``nullptr`` uses ``MainMix``.
     *  @param loop   Loop continuously when ``true``.
     *  @param volume Initial volume in the range [0.0, 1.0].
     */
    void create(YSE::DSP::buffer& buffer, channel* ch = nullptr, bool loop = false,
                float volume = 1.f);

    /** @brief Multichannel variant of the buffer-based ``create``.
     *
     *  Identical to the single-buffer overload except that the source carries
     *  multiple channels (stereo or surround).
     */
    void create(MULTICHANNELBUFFER& buffer, channel* ch = nullptr, bool loop = false,
                float volume = 1.0f);

    /**
     *  @brief Create a sound driven by a custom DSP source.
     *
     *  Lets the application supply procedurally generated audio. Subclass
     *  ``YSE::DSP::dspSourceObject`` and pass the instance.
     *
     *  @param dsp     The audio source. Must outlive the sound (see lifetime note).
     *  @param ch      Channel to attach to. ``nullptr`` uses ``MainMix``.
     *  @param volume  Initial volume in the range [0.0, 1.0].
     *
     *  @warning **Lifetime contract.** The caller owns ``dsp`` and must keep it
     *           alive until *after* the sound's destructor has run AND the
     *           engine's slow-pool delete tick has fired. The audio thread calls
     *           ``dsp.process(...)`` every callback while the sound is live. The
     *           implementation defensively nulls its pointer at the release
     *           transition, so destroying ``dsp`` slightly past release degrades
     *           to a silent nullptr read — but stack-local sources whose
     *           lifetime ends well before release can still cause a
     *           use-after-free. For tests or short-lived sources, allocate at
     *           file scope, hold via ``std::shared_ptr``, or stop the sound and
     *           drain the manager before ``dsp`` goes out of scope.
     */
    void create(YSE::DSP::dspSourceObject& dsp, channel* ch = nullptr, float volume = 1.0f);

    /**
     *  @brief Create a sound driven by a patcher graph.
     *
     *  @param patcher The patcher to run as an audio source. Must outlive the sound.
     *  @param ch      Channel to attach to. ``nullptr`` uses ``MainMix``.
     *  @param volume  Initial volume in the range [0.0, 1.0].
     */
    void create(YSE::patcher& patcher, channel* ch = nullptr, float volume = 1.0f);

    /**
     *  @brief Create a sound driven by a polyphonic ``YSE::synth``.
     *
     *  Attaches the synth's aggregate voice pool behind this sound, which
     *  supplies the single 3D position, channel routing, and master play/stop
     *  intent. Calls ``synth.create()`` for you if you have not. Build the
     *  synth's voices with ``synth.addVoices(...)`` *before* this call.
     *
     *  @param synth  The synth to render. Must outlive the sound (see warning).
     *  @param ch     Channel to attach to. ``nullptr`` uses ``MainMix``.
     *  @param volume Initial volume in the range [0.0, 1.0].
     *
     *  @warning **Lifetime contract.** The ``synth`` must outlive this sound —
     *           until after the sound's destructor AND the engine's slow-pool
     *           delete tick. Same shape as the raw ``dspSourceObject`` overload.
     */
    void create(YSE::synth& synth, channel* ch = nullptr, float volume = 1.0f);

    /**
     *  @brief Whether this interface has a live implementation.
     *
     *  Returns ``true`` for the entire lifetime of a successfully ``create``-d
     *  sound. Primarily useful for debugging — callers don't need to gate other
     *  methods on this.
     */
    bool isValid();

    /**
     *  @brief Assign a bus-addressable name to this sound.
     *
     *  Names the sound on the global named bus (issue #123, epic #119) so live
     *  coders can drive it by string address rather than by C++ handle. Once
     *  named ``foo``, the sound subscribes to:
     *
     *  - ``sound.foo.volume``   → ``float`` (also accepts ``int``), calls ``volume()``
     *  - ``sound.foo.speed``    → ``float`` (also accepts ``int``), calls ``speed()``
     *  - ``sound.foo.position`` → ``list[float]`` of length 3, becomes a ``Pos``
     *
     *  Anonymous sounds (the default) are not addressable. Passing an empty
     *  string clears the name and removes the bus subscriptions. Renaming
     *  re-subscribes under the new name.
     *
     *  Two sounds cannot share a name: the second ``name()`` is rejected and
     *  logged via the engine's error path; the first registration wins. The
     *  bus is only live between ``System::init()`` and ``System::close()`` —
     *  naming a sound while the engine is down is a no-op.
     *
     *  @param n The name, or ``""`` to make the sound anonymous again.
     *  @return ``*this`` for fluent chaining.
     */
    sound& name(const std::string& n);

    /** @brief Set the position of this sound in the virtual scene.
     *
     *  Accepts a full 3D ``Pos``, but the panner is **horizontal-only**: the
     *  azimuth is taken from the ``x`` / ``z`` components (``atan2(x, z)``)
     *  while the ``y`` (height) component only affects distance attenuation and
     *  doppler, never which speaker a sound pans to. Two sources at the same
     *  ``x`` / ``z`` but different heights pan identically. As a source
     *  approaches straight overhead the azimuth is deliberately faded toward an
     *  equal-power omni spread so a flyover degrades gracefully rather than
     *  sweeping the full circle (issue #210). True height reproduction
     *  (7.1.4 / dome layouts) is not currently supported.
     */
    void pos(const Pos& v);

    /** @brief Current position of this sound. */
    Pos pos();

    /**
     *  @brief Spread the channels of a multichannel sound across the stereo or surround field.
     *
     *  Has no audible effect on mono sources. ``value`` is the distance between
     *  individual channels in the listener's space.
     */
    void spread(float value);

    /** @brief Current channel spread of a multichannel sound. */
    float spread();

    /**
     *  @brief Set the playback speed.
     *
     *  Alters pitch — playback at 2.0 is one octave up, 0.5 is one octave down.
     *  Doppler shift from listener/sound velocity is layered on top unless
     *  ``doppler(false)`` is set. Negative values play the sound backwards
     *  (not supported for streaming sounds, since that would thrash the disk).
     *
     *  @note The internal value never reaches exactly zero — perfectly silent
     *        playback at static DC can damage speakers.
     */
    void speed(float value);

    /** @brief Current playback speed. */
    float speed();

    /**
     *  @brief Set the audible radius of the sound.
     *
     *  ``value`` controls the rolloff distance: beyond this radius the sound
     *  fades out.
     */
    void size(float value);

    /** @brief Current audible radius. */
    float size();

    /** @brief Set whether the sound loops continuously. */
    void looping(bool value);

    /** @brief Whether the sound is currently set to loop. */
    bool looping();

    /**
     *  @brief Set the volume of this sound, optionally with a fade.
     *
     *  @param value Target volume in the range [0.0, 1.0].
     *  @param time  Fade duration in milliseconds. 0 (default) sets the volume
     *               immediately.
     */
    void volume(float value, unsigned int time = 0);

    /** @brief Current volume.
     *
     *  May differ from the most recently requested target volume if a non-zero
     *  fade time was supplied and the fade is still in progress.
     */
    float volume();

    /** @brief Fade out over ``time`` milliseconds, then stop. */
    void fadeAndStop(unsigned int time);

    /** @brief Start playback. */
    void play();

    /** @brief Whether the sound is currently playing. */
    bool isPlaying();

    /** @brief Pause playback. ``play()`` resumes from the current position. */
    void pause();

    /** @brief Whether the sound is currently paused. */
    bool isPaused();

    /** @brief Stop playback and rewind to the start of the source. */
    void stop();

    /** @brief Whether the sound is currently stopped. */
    bool isStopped();

    /** @brief Cycle between playing / paused / stopped.
     *
     *  Playing → paused, paused → playing, stopped → playing.
     */
    void toggle();

    /** @brief Restart from the beginning regardless of current position. */
    void restart();

    /**
     *  @brief Seek to a position in the source file.
     *
     *  Distinct from ``pos`` — that controls position in the virtual scene.
     *
     *  @param value New playhead position in samples, in [0, ``length()``].
     */
    void time(float value);

    /** @brief Current playhead position in samples. */
    float time();

    /** @brief Length of the source in samples. */
    unsigned int length();

    /**
     *  @brief Make the sound relative to the listener.
     *
     *  Relative sounds move with the listener — libYSE's idiomatic replacement
     *  for "2D" sounds. Typical for GUI clicks and background music. Relative
     *  sounds can still move within the listener's frame.
     */
    void relative(bool value);

    /** @brief Whether the sound is relative to the listener. */
    bool relative();

    /**
     *  @brief Enable or disable doppler shift for this sound.
     *  @see YSE::System For the global doppler scale.
     */
    void doppler(bool value);

    /** @brief Whether doppler is enabled for this sound. */
    bool doppler();

    /**
     *  @brief Shorthand for relative + listener-origin position + no doppler.
     *
     *  Equivalent to ``relative(true); pos(Pos(0,0,0)); doppler(false);``.
     */
    void pan2D(bool value);

    /** @brief Whether the sound is in 2D-pan mode. */
    bool pan2D();

    /** @brief Whether the sound is streamed from disk or network (vs loaded into memory). */
    bool isStreaming();

    /**
     *  @brief Whether the sound is ready for playback.
     *
     *  Loading happens asynchronously so the audio callback isn't blocked, so
     *  there is a brief window after ``create`` where the sound is not yet
     *  ready. Callers do *not* need to wait for this — calling ``play()`` on a
     *  not-yet-ready sound simply queues the start.
     */
    bool isReady();

    /**
     *  @brief Enable sound occlusion for this sound.
     *
     *  Requires an occlusion callback installed via
     *  ``System().occlusionCallback(...)``.
     */
    void occlusion(bool value);

    /** @brief Whether sound occlusion is active for this sound. */
    bool occlusion();

    /** @brief Move this sound to a different channel.
     *
     *  @param target The new parent channel.
     */
    void moveTo(channel& target);

    /** @brief Attach a DSP effect chain to this sound. */
    void setDSP(YSE::DSP::dspObject* value);

    /** @brief Currently attached DSP effect chain, or ``nullptr`` if none. */
    YSE::DSP::dspObject* getDSP();

  private:
    // Bus addressing (issue #123). Subscribe/unsubscribe the named-bus
    // addresses for this sound. No-ops while the engine bus is down.
    void registerOnBus();
    void unregisterFromBus();

    SOUND::implementationObject* pimpl;

    // These values keep the last set value and are used by getters
    // so that we don't have to query the implementation
    Pos _pos;
    Flt _spread;
    Flt _volume;
    Flt _speed;
    Flt _size;
    Bool _loop;
    Bool _relative;
    Bool _doppler;
    Bool _pan2D;
    Bool _occlusion;
    // Last occlusion value delivered to the implementation. Cached so the
    // control-thread driver only enqueues an OCCLUSION_VALUE message when the
    // callback result actually changes (issue #209), matching the compare-then-
    // send pattern used by every other setter.
    Flt _occlusionValue{0.f};

    UInt _fadeAndStopTime;
    DSP::dspObject* _dsp;
    channel* _parent;

    // Bus addressing state. Empty name = anonymous = not on the bus.
    // Handles index 0 = volume, 1 = speed, 2 = position. _busOwner is true
    // only when this sound won the name and holds live subscriptions.
    std::string _name;
    std::uint64_t _busHandles[3]{0, 0, 0};
    bool _busOwner{false};

    friend class SOUND::implementationObject;
    // The control-thread occlusion driver reads _pos / pimpl / _occlusionValue
    // and enqueues OCCLUSION_VALUE messages directly (issue #209).
    friend void SOUND::updateOcclusion();
  };
} // namespace YSE

#endif // SOUND_H_INCLUDED
