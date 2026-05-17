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
#include <string>

namespace YSE {
  const std::string VERSION = "2.0.2";

  /** @brief Signature of a user-supplied sound-occlusion callback.
   *
   *  Given the source and listener positions, return the attenuation factor
   *  in the range [0.0, 1.0]: 0 means fully occluded, 1 means audible without
   *  obstruction. Typical implementations raycast through game-world geometry.
   */
  typedef float(*occlusionFunc)(const Pos& source, const Pos& listener);

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
     */
    void update();

    /** @brief Shut down the engine and release the audio device. */
    void close();

    /** @brief Pause audio output. The engine keeps running but the device is silent. */
    void pause();

    /** @brief Resume audio output after ``pause()``. */
    void resume();

    /** @brief Number of audio callbacks that have failed to complete on time.
     *
     *  A non-zero value indicates the audio thread is starved or the device
     *  has disconnected. Useful as a watchdog signal for ``autoReconnect``.
     */
    int  missedCallbacks();

    /** @brief Access the global reverb.
     *
     *  Disabled by default. When enabled, it acts as the fallback reverb at
     *  any position not covered by a positioned ``reverb`` zone. Partially
     *  rolled-off reverb zones are mixed against the global reverb.
     */
    reverb & getGlobalReverb();

    /** @brief All audio output devices visible to the engine.
     *
     *  @note Only available when libYSE is linked as a static library. When
     *        linked dynamically, use ``getNumDevices`` / ``getDevice`` instead
     *        to avoid leaking the standard-library ``std::vector`` across the
     *        ABI boundary.
     */
    const std::vector<device> & getDevices();

    /** @brief Number of audio output devices available. */
    unsigned int getNumDevices();

    /** @brief Audio device at index ``nr``. */
    const device & getDevice(unsigned int nr);

    /** @brief Open an audio device.
     *
     *  @param object Device + host + sample-rate configuration.
     *  @param conf   Speaker layout. ``CT_AUTO`` picks stereo when possible.
     */
    void openDevice(const deviceSetup & object, CHANNEL_TYPE conf = CT_AUTO);

    /** @brief Close the currently open audio device. */
    void closeCurrentDevice();

    /** @brief Name of the platform default audio device. */
    const std::string & getDefaultDevice();

    /** @brief Name of the platform default audio host (e.g. WASAPI, ALSA). */
    const std::string & getDefaultHost();

#if YSE_WINDOWS || YSE_LINUX
    /** @brief Number of MIDI input devices available. (Windows / Linux only.) */
    unsigned int getNumMidiInDevices();

    /** @brief Number of MIDI output devices available. (Windows / Linux only.) */
    unsigned int getNumMidiOutDevices();

    /** @brief Name of the MIDI input device with the given ID. */
    const std::string getMidiInDeviceName(unsigned int ID);

    /** @brief Name of the MIDI output device with the given ID. */
    const std::string getMidiOutDeviceName(unsigned int ID);
#endif

    /** @brief Install a sound-occlusion callback.
     *
     *  The engine calls the function for every pair of (source, listener)
     *  positions to compute how much a sound should be attenuated by world
     *  geometry. Typical implementations issue a raycast through the game
     *  physics engine. Pass ``nullptr`` to disable.
     *
     *  @see occlusionFunc
     */
    system& occlusionCallback(float(*func)(const YSE::Pos&, const YSE::Pos&));

    /** @brief Current occlusion callback, or ``nullptr`` if none installed. */
    occlusionFunc occlusionCallback();

    /** @brief Route a channel through the under-water effect. */
    system & underWaterFX(const channel & target);

    /** @brief Configure the depth (intensity) of the under-water effect.
     *
     *  @param value Depth in the range [0.0, 1.0]. 0 is dry, 1 is the maximum
     *               low-pass / pitch-shift the effect applies.
     */
    system & setUnderWaterDepth(float value);

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

    /** @brief CPU load of the audio thread as a fraction of the callback budget.
     *
     *  Distinct from the cost of ``update()`` on the main thread.
     */
    float cpuLoad();

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

    /** @brief libYSE version string. */
    std::string Version() const { return VERSION; }

  private:
    /// @cond INTERNAL
    // Shared body of init() / initOffline(); when openDevice is false,
    // skips the addCallback() that opens the PortAudio stream.
    bool initShared(bool openDevice);
    /// @endcond

    float(*occlusionPtr)(const Pos& source, const Pos& listener);
    int currentlyMissedCallbacks;
    bool doAutoReconnect;
    int reconnectDelay;
  };

  /** @brief Access the singleton ``system`` object. */
  API system & System();
}



#endif  // SYSTEM_H_INCLUDED
