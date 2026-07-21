/*
  ==============================================================================

    system.hpp
    Created: 27 Jan 2014 7:14:31pm
    Author:  yvan

  ==============================================================================
*/

#ifndef SYSTEM_H_INCLUDED
#define SYSTEM_H_INCLUDED

#include "headers/types.hpp"
#include "headers/enums.hpp"
#include "utils/vector.hpp"
#include "classes.hpp"
#include <atomic>
#include <string>

namespace YSE {
  const std::string VERSION = "2.4.0";

  /** @brief Signature of a user-supplied sound-occlusion callback.
   *
   *  Given the source and listener positions, return the occlusion amount in
   *  the range [0.0, 1.0]: 0 means no obstruction (full volume), 1 means fully
   *  occluded (silent). The engine applies it as a gain duck
   *  (``finalGain *= 1 - occlusion``). Typical implementations raycast through
   *  game-world geometry. See ``system::occlusionCallback`` for the threading
   *  contract — this runs on the control thread, not the audio callback.
   */
  typedef float (*occlusionFunc)(const Pos& source, const Pos& listener);

  /**
   *  @brief Engine lifecycle, audio device control, and global effect settings.
   *
   *  ``system`` is the top-level entry point for libYSE. Construct nothing
   *  directly — access the singleton through the ``System()`` free function.
   *  The typical lifecycle is:
   *
   *  1. ``System().init()`` once at startup.
   *  2. ``System().update()`` every frame.
   *  3. ``System().close()`` once at shutdown.
   *
   *  Between init and close, the engine manages an audio device, runs the
   *  DSP graph, and dispatches messages to playing sounds.
   *
   *  @see YSE::System
   *  @see YSE::listener
   */
  class API system {
  public:
    system();

    /** @brief Initialise the engine and open the default audio device.
     *
     *  @note **Threading.** This call runs on and *defines* the engine's
     *        control thread. Call ``update()`` and ``close()`` from the same
     *        thread. The control-thread identity is captured here and never
     *        re-derived, so driving ``init()`` and ``update()`` from different
     *        threads silently loses the named-bus inline fast path — see the
     *        ``update()`` threading note (issue #290).
     *  @return ``true`` on success, ``false`` if no device could be opened.
     */
    bool init();

    /** @brief Initialise the engine without opening an audio device.
     *
     *  For benchmarks, automated tests, and headless tooling that need a
     *  fully-configured engine but no PortAudio stream — same channel
     *  tree, same DSP graph, no audio thread. Once initialised this way,
     *  drive the engine via ``System().renderOffline(blocks)`` rather
     *  than the audio callback.
     *
     *  @return ``true`` on success.
     */
    bool initOffline();

    /** @brief Render N audio blocks synchronously on the calling thread.
     *
     *  Runs the same callback body the audio thread executes in
     *  production — manager update, channel-tree DSP, channel mixing,
     *  reverb — for ``blocks`` × ``STANDARD_BUFFERSIZE`` samples worth of
     *  output, which is discarded. Use only after ``initOffline()``;
     *  driving this concurrently with a live audio thread would race the
     *  manager-update path.
     */
    void renderOffline(int blocks);

    /** @brief Pump engine state.
     *
     *  Call once per frame from the main thread. Drives message delivery,
     *  sound state transitions, virtualisation decisions, and listener
     *  velocity calculations.
     *
     *  @note **Threading.** ``update()`` must be called from the same thread
     *        that called ``init()`` — the engine's single control thread. That
     *        identity is frozen when the engine initialises (not re-derived per
     *        tick) and decides who may dispatch control-rate (``T_GUI``)
     *        named-bus publishes inline; a publish from any other thread is
     *        instead deferred to the next ``update()`` tick to keep the
     *        per-object message queues single-producer (issue #193). Driving
     *        ``init()`` and ``update()`` from *different* threads is
     *        unsupported: it stays functionally correct — every publish simply
     *        takes the deferred path and is dispatched on whichever thread runs
     *        ``update()`` — but permanently forfeits the inline fast path,
     *        because the control thread is captured at ``init()`` rather than
     *        followed from ``update()`` (issue #290).
     */
    void update();

    /** @brief Shut down the engine and release the audio device. */
    void close();

    /** @brief Pause audio output. The engine keeps running but the device is silent.
     *
     *  ``pause()`` closes the audio stream, so the audio tick stops draining the
     *  lock-free control-to-audio inboxes (per-object message queues and the
     *  managers' ``toLoadInbox``es). Interface setters and object creation called
     *  while paused keep enqueuing onto those queues; the messages are held and
     *  delivered in order on ``resume()``, not dropped. This is intentional — the
     *  queues carry ordered discrete commands (play/stop, position, load-handoff
     *  pointers), so silently dropping them would corrupt state, unlike the
     *  latest-value-wins parameter queues (NamedBus, patcher) which do cap.
     *
     *  The consequence is unbounded memory growth if an app pushes a very large
     *  number of setter/creation calls while paused: ``lfQueue`` allocates
     *  doubling-size blocks on demand and never frees them, so the peak retained
     *  capacity persists for the queue's lifetime even after the backlog drains.
     *  All allocation happens producer-side on the control thread, so this never
     *  violates audio-thread real-time discipline. Applications that drive a heavy
     *  control-rate workload should keep the engine running (leave the device open)
     *  rather than pausing, or simply avoid issuing large batches of setters while
     *  paused. See issue #289.
     */
    void pause();

    /** @brief Resume audio output after ``pause()``. */
    void resume();

    /** @brief Number of audio callbacks that have failed to complete on time.
     *
     *  A non-zero value indicates the audio thread is starved or the device
     *  has disconnected. Useful as a watchdog signal for ``autoReconnect``.
     */
    int missedCallbacks();

    /** @brief Access the global reverb.
     *
     *  Disabled by default. When enabled, it acts as the fallback reverb at
     *  any position not covered by a positioned ``reverb`` zone. Partially
     *  rolled-off reverb zones are mixed against the global reverb.
     */
    reverb& getGlobalReverb();

    /** @brief All audio output devices visible to the engine.
     *
     *  @note Only available when libYSE is linked as a static library. When
     *        linked dynamically, use ``getNumDevices`` / ``getDevice`` instead
     *        to avoid leaking the standard-library ``std::vector`` across the
     *        ABI boundary.
     */
    const std::vector<device>& getDevices();

    /** @brief Number of audio output devices available. */
    unsigned int getNumDevices();

    /** @brief Audio device at index ``nr``. */
    const device& getDevice(unsigned int nr);

    /** @brief Open an audio device.
     *
     *  @param object Device + host + sample-rate configuration.
     *  @param conf   Speaker layout. ``CT_AUTO`` picks stereo when possible.
     */
    void openDevice(const deviceSetup& object, CHANNEL_TYPE conf = CT_AUTO);

    /** @brief Close the currently open audio device. */
    void closeCurrentDevice();

    /** @brief Name of the platform default audio device. */
    const std::string& getDefaultDevice();

    /** @brief Name of the platform default audio host (e.g. WASAPI, ALSA). */
    const std::string& getDefaultHost();

#if YSE_ENABLE_MIDI_DEVICE
    /** @brief Number of MIDI input devices available.
     *
     *  Present only when libyse was built with ``YSE_ENABLE_MIDI_DEVICE=ON``
     *  (default on Windows/Linux). When the option is OFF the function is
     *  not declared at all — gate consumer code on ``#if YSE_ENABLE_MIDI_DEVICE``.
     */
    unsigned int getNumMidiInDevices();

    /** @brief Number of MIDI output devices available. */
    unsigned int getNumMidiOutDevices();

    /** @brief Name of the MIDI input device with the given ID. */
    const std::string getMidiInDeviceName(unsigned int ID);

    /** @brief Name of the MIDI output device with the given ID. */
    const std::string getMidiOutDeviceName(unsigned int ID);
#endif

    /** @brief Install a sound-occlusion callback.
     *
     *  The engine calls the function for every occlusion-enabled sound to
     *  compute how much it should be attenuated by world geometry. The returned
     *  factor is applied as a gain duck (``finalGain *= 1 - occlusion``).
     *  Typical implementations issue a raycast through the game physics engine.
     *  Pass ``nullptr`` to disable.
     *
     *  @note **Threading.** The callback runs on the thread that calls
     *        ``System().update()`` (the application/control thread), once per
     *        occlusion-enabled sound per update tick — never on the audio
     *        callback thread. The result is delivered to the audio thread over
     *        the lock-free sound message queue. This means a raycast that takes
     *        locks or allocates cannot stall the audio callback (issue #209),
     *        but it also means the callback must not block ``update()`` for
     *        long.
     *
     *  @see occlusionFunc
     */
    system& occlusionCallback(float (*func)(const YSE::Pos&, const YSE::Pos&));

    /** @brief Current occlusion callback, or ``nullptr`` if none installed. */
    occlusionFunc occlusionCallback();

    /** @brief Route a channel through the built-in under-water effect.
     *
     *  Since issue #327 the underwater treatment is an ordinary insert module
     *  (``DSP::MODULES::underWater``); this call places the engine's default
     *  instance at the head of @p target's insert chain through the normal
     *  ``channel::setDSP`` message path. It therefore occupies the channel's
     *  insert slot: a later ``setDSP`` on the same channel replaces it, and
     *  vice versa. Only one channel carries the stock effect at a time —
     *  calling this again with a different channel moves it. To combine the
     *  effect with other inserts, or to drive it from your own control logic,
     *  instantiate your own ``DSP::MODULES::underWater`` instead.
     */
    system& underWaterFX(const channel& target);

    /** @brief Set the listener's depth below the water surface.
     *
     *  The default spatial driver of the underwater module: evaluate it at
     *  control rate on your update thread and the value is delivered to the
     *  audio thread as an ordinary wait-free parameter write.
     *
     *  @param value Depth below the surface in world distance units. Zero or
     *               less disables the effect entirely; the low-passed,
     *               position-neutral treatment fades in above 1 and is fully
     *               position-neutral from 5 down. Any positive depth also
     *               enables the built-in underwater reverb zone at the
     *               listener's position.
     */
    system& setUnderWaterDepth(float value);

    /** @brief Set the maximum number of concurrently audible sounds.
     *
     *  When this limit is exceeded, the engine virtualises the least
     *  significant sounds (typically furthest from the listener) instead of
     *  rendering them, freeing CPU for the audible ones.
     */
    system& maxSounds(int value);

    /** @brief Current ``maxSounds`` limit. */
    int maxSounds();

    /** @brief Enable or disable the built-in audio test signal.
     *
     *  Outputs a steady tone through the audio device for verifying the
     *  output chain.
     */
    system& AudioTest(bool on);

    /** @brief Configure automatic device reconnection.
     *
     *  @param on    When ``true``, the engine attempts to re-open the audio
     *               device after a disconnection (e.g. headphones unplugged).
     *  @param delay Milliseconds to wait between reconnection attempts.
     */
    system& autoReconnect(bool on, int delay);

    /** @brief Audio callback wall-clock load as a fraction of the buffer period.
     *
     *  Measured by YSE: timestamps taken at the entry/exit of each backend
     *  callback (PortAudio's ``paCallback`` or Oboe's ``onAudioReady``) and
     *  divided by the buffer's audio time. EMA-smoothed with a ~1 s time
     *  constant. Returns 0 when no device is open.
     *
     *  This is a **dropout-risk** indicator: 1.0 means the callback is taking
     *  as long as the buffer it produces, i.e. the next buffer will arrive
     *  late.
     *
     *  Distinct from the cost of ``update()`` on the main thread. Timed
     *  ourselves (rather than reading ``Pa_GetStreamCpuLoad``) so the Oboe /
     *  Android backend can report a comparable number — that API doesn't
     *  exist there.
     */
    float cpuLoad();

    /** @brief Engine session sample rate in Hz.
     *
     *  The rate the engine locked to when ``init()`` / ``initOffline()`` ran.
     *  Stays constant across the entire session, including pause / resume
     *  cycles where ``getActiveSampleRate()`` transiently drops to 0. Returns
     *  0 before the lock is established (pre-init).
     *
     *  Use this when scheduling sample-count-driven work that must outlive a
     *  pause; use ``getActiveSampleRate()`` for live device-state UI.
     */
    double getSampleRate();

    /// @name Active device readouts
    /// Live state of the currently open audio device. Each returns 0 when no
    /// device is open (pre-init, after ``close()``, or under ``initOffline()``).
    /// Host applications use these to render device-info UI without having to
    /// re-enumerate ``YSE::device`` descriptors, and to survive device
    /// reconnects cleanly.
    /// @{

    /** @brief Currently negotiated sample rate in Hz. */
    double getActiveSampleRate();

    /** @brief Currently negotiated audio buffer size in frames.
     *
     *  This is the device's frames-per-callback (PortAudio's
     *  ``framesPerBuffer`` / Oboe's ``framesPerBurst``) — NOT the engine's
     *  internal ``STANDARD_BUFFERSIZE``, which may differ.
     */
    int getActiveBufferSize();

    /** @brief Currently negotiated output latency in samples.
     *
     *  Convert to milliseconds with ``(latency / sampleRate) * 1000``.
     */
    int getActiveOutputLatency();

    /// @}

    /** @brief Sleep the calling thread for ``ms`` milliseconds.
     *
     *  Convenience for console applications that don't otherwise yield
     *  between calls to ``update()``.
     */
    void sleep(unsigned int ms);

    /// @name Domain clocks (issue #249)
    /// A set of named musical (beat) clocks, each a beat accumulator derived
    /// from the audio callback. Every audio block a clock advances by
    /// ``blockSeconds × tempo / 60`` at its current tempo, so beat position is
    /// the running integral of tempo — no absolute-time schedule. Because all
    /// clocks derive from the single sample clock, polytemporal relationships
    /// stay exact and deterministic. Tempo is a playable, rampable control.
    ///
    /// **Threading.** ``createClock`` / ``destroyClock`` / ``setTempo`` are
    /// control-thread operations; ``beatPosition`` / ``currentTempo`` may be
    /// read from the UI thread at frame rate (e.g. for a playhead). None of
    /// them run on or block the audio callback.
    /// @{

    /** @brief Create a domain clock.
     *
     *  @param name         Unique domain name. Empty names are rejected.
     *  @param initialTempo Starting tempo in BPM (default 120).
     *  @return ``true`` on success; ``false`` if the name is empty or a live
     *          clock already owns it (first registration wins).
     */
    bool createClock(const std::string& name, float initialTempo = 120.f);

    /** @brief Destroy a domain clock. No-op for an unknown name.
     *
     *  The clock stops being visible to queries immediately; its beat/tempo
     *  advancement stops on the next audio callback.
     */
    void destroyClock(const std::string& name);

    /** @brief Whether a live domain clock with ``name`` exists. */
    bool clockExists(const std::string& name);

    /** @brief Ramp a domain clock's tempo toward ``bpm`` over ``rampSeconds``.
     *
     *  Instant when ``rampSeconds`` is 0; otherwise linear. Tempo is a played
     *  control signal — call it as often as you like. It is not clamped: 0
     *  pauses the clock and a negative tempo runs it backwards. No-op for an
     *  unknown name.
     */
    system& setTempo(const std::string& name, float bpm, float rampSeconds = 0.f);

    /** @brief Current beat position (running integral of tempo) of a clock, or
     *         0 for an unknown name. */
    double beatPosition(const std::string& name);

    /** @brief Current tempo in BPM of a clock, or 0 for an unknown name. */
    float currentTempo(const std::string& name);

    /// @}

    /** @brief libYSE version string. */
    std::string Version() const {
      return VERSION;
    }

  private:
    /// @cond INTERNAL
    // Shared body of init() / initOffline(); when openDevice is false,
    // skips the addCallback() that opens the PortAudio stream.
    bool initShared(bool openDevice);
    /// @endcond

    // Written by occlusionCallback(func) on the application thread, read by
    // SOUND::updateOcclusion() (also the control thread, but a different caller
    // may install the callback than the one driving update()). Atomic with
    // release/acquire ordering per the C API callback-bridge convention
    // (c_api/yse_c_internal.hpp §callback bridge rules) — a plain pointer here
    // is a data race and licenses the compiler to cache the load (issue #199).
    std::atomic<occlusionFunc> occlusionPtr;
    int currentlyMissedCallbacks;
    bool doAutoReconnect;
    int reconnectDelay;
  };

  /** @brief Access the singleton ``system`` object. */
  API system& System();
} // namespace YSE

#endif // SYSTEM_H_INCLUDED
