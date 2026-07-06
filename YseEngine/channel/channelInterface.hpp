/*
  ==============================================================================

  channel.h
  Created: 30 Jan 2014 4:20:50pm
  Author:  yvan

  ==============================================================================
*/

#ifndef CHANNELINTERFACE_H_INCLUDED
#define CHANNELINTERFACE_H_INCLUDED

#include <cstdint>
#include <string>
#include "../classes.hpp"
#include "../headers/defines.hpp"
#include "../headers/types.hpp"
#include "channel.hpp"

namespace YSE {
  class system;

  /// @cond INTERNAL
  namespace SOUND {
    class managerObject;
    class implementationObject;
  } // namespace SOUND
  /// @endcond

  /**
   *  @brief A node in the channel tree — a group of sounds that mix together.
   *
   *  Channels work like the channel groups on a mixing console: every sound is
   *  attached to a channel, and channels can themselves be attached to a parent
   *  channel, forming a tree rooted at ``MainMix``. Each channel is rendered on
   *  its own DSP thread, so spreading sounds across multiple channels can help
   *  scale across cores.
   *
   *  Several pre-built channels are created for you and exposed through free
   *  functions:
   *
   *  - ``ChannelMaster()``  — the root of the tree.
   *  - ``ChannelFX()``      — short sound effects.
   *  - ``ChannelMusic()``   — playlists and music.
   *  - ``ChannelAmbient()`` — environmental loops.
   *  - ``ChannelVoice()``   — dialogue.
   *  - ``ChannelGui()``     — UI feedback.
   *
   *  Use them as-is or as roots for your own subtrees.
   *
   *  @see YSE::sound
   */
  class API channel {
  public:
    /**
     *  @brief Create the channel and attach it to the tree.
     *
     *  Must be called before any other method. The pre-built channels
     *  (``ChannelFX`` etc.) call this internally.
     *
     *  @param name   Channel name, used for log output.
     *  @param parent Existing channel to attach to. Use ``ChannelMaster()`` for a
     *                top-level channel.
     */
    channel& create(const char* name, channel& parent);

    /** @brief Set the channel volume in the range [0.0, 1.0]. */
    channel& setVolume(float value);

    /**
     *  @brief Assign a bus-addressable name to this channel.
     *
     *  Names the channel on the global named bus (issue #123, epic #119) so
     *  live coders can drive it by string address. Once named ``music``, the
     *  channel subscribes to ``channel.music.volume`` → ``float`` (also accepts
     *  ``int``), calling ``setVolume()``.
     *
     *  This is independent of the log name passed to ``create``. Anonymous
     *  channels (the default) are not addressable; passing ``""`` clears the
     *  name and removes the subscription. Two channels cannot share a name —
     *  the second ``name()`` is rejected and logged, the first wins. The bus is
     *  only live between ``System::init()`` and ``System::close()``.
     *
     *  @param n The name, or ``""`` to make the channel anonymous again.
     *  @return ``*this`` for fluent chaining.
     */
    channel& name(const std::string& n);

    /** @brief Current channel volume. */
    float getVolume();

    /**
     *  @brief Re-parent this channel.
     *
     *  Detaches from the current parent and links to ``parent``. All sounds and
     *  subchannels move along.
     */
    channel& moveTo(channel& parent);

    /**
     *  @brief Move the global reverb effect onto this channel.
     *
     *  libYSE runs a single reverb instance for performance reasons. By default
     *  it sits on ``MainMix`` and affects every channel; call this to restrict
     *  reverb to a subtree.
     */
    channel& attachReverb();

    /**
     *  @brief Attach a pre-fader insert DSP effect to this channel.
     *
     *  Mirrors ``YSE::sound::setDSP`` but at the channel level: the effect
     *  processes this channel's *summed* output (all its sounds and
     *  subchannels mixed together) in place, **pre-fader** — before reverb and
     *  before the channel volume is applied. This is the DAW "insert" slot;
     *  put a delay on ``ChannelMusic()`` or a compressor on ``ChannelVoice()``.
     *
     *  The effect must honour the N-channel ``dspObject::process`` contract
     *  (process every channel of the buffer). Chain several effects with
     *  ``dspObject::link``. Pass ``nullptr`` to detach. The engine takes no
     *  ownership — the ``dspObject`` must outlive the channel or be detached
     *  first.
     *
     *  @param value The effect chain head, or ``nullptr`` to detach.
     *  @return ``*this`` for fluent chaining.
     */
    channel& setDSP(DSP::dspObject* value);

    /** @brief The currently attached insert effect, or ``nullptr`` if none. */
    DSP::dspObject* getDSP();

    /**
     *  @brief Allow or disallow sounds on this channel to be virtualised.
     *
     *  Virtualised sounds keep their playback state but stop consuming DSP
     *  budget — the engine uses this to stay within ``System().maxSounds(...)``.
     */
    channel& setVirtual(bool value);

    /** @brief Whether virtualisation is permitted on this channel. */
    bool getVirtual();

    /** @brief Whether this channel has a live implementation. */
    bool isValid();

    /** @brief Channel name (the value passed to ``create``). */
    const char* getName() {
      return logName.c_str();
    }

    /// @name Output peak metering
    /// Lock-free getters returning the latest per-block peak of this channel's
    /// output buffers. Refresh granularity is one audio block (so polling
    /// faster than the audio block rate yields no new data). ``Pre`` reads
    /// the peak measured at the end of ``dsp()`` (after reverb/underwater FX,
    /// before the channel volume is applied); ``Post`` reads the peak
    /// measured immediately after ``adjustVolume()`` — what listeners hear.
    /// Per-output overloads take an index in ``[0, getNumOutputs())``;
    /// out-of-range indices return 0. dB getters convert the linear value on
    /// the fly with a -120 dB floor for silence. Returns 0 on an invalid
    /// channel.
    /// @{

    /** @brief Number of output channels currently allocated (matches the open device's layout). */
    int getNumOutputs();

    /** @brief Combined (max-over-outputs) pre-volume peak as a linear sample value. */
    float getPeakLinearPre();
    /** @brief Combined (max-over-outputs) post-volume peak as a linear sample value. */
    float getPeakLinearPost();
    /** @brief Combined pre-volume peak in dBFS (``-120`` floor for silence). */
    float getPeakDbPre();
    /** @brief Combined post-volume peak in dBFS (``-120`` floor for silence). */
    float getPeakDbPost();

    /** @brief Per-output pre-volume peak as a linear sample value. */
    float getPeakLinearPre(int outputIdx);
    /** @brief Per-output post-volume peak as a linear sample value. */
    float getPeakLinearPost(int outputIdx);
    /** @brief Per-output pre-volume peak in dBFS (``-120`` floor for silence). */
    float getPeakDbPre(int outputIdx);
    /** @brief Per-output post-volume peak in dBFS (``-120`` floor for silence). */
    float getPeakDbPost(int outputIdx);

    /// @}

    /** @brief Construct an empty channel.
     *
     *  Channels are only usable after ``create`` has been called.
     */
    channel();
    ~channel();

  private:
    void createGlobal();

    // Bus addressing (issue #123). Subscribe/unsubscribe the named-bus
    // address for this channel. No-ops while the engine bus is down.
    void registerOnBus();
    void unregisterFromBus();

    Flt volume;
    Bool allowVirtual;
    std::string logName; // label passed to create(); used for log output
    CHANNEL::implementationObject* pimpl;

    // Cached head of the attached insert chain (interface-side mirror of the
    // impl's insert_dsp). Used by getDSP() and to skip redundant messages when
    // setDSP() is called with an unchanged pointer. Not owned.
    DSP::dspObject* _dsp{nullptr};

    // Bus addressing state. Empty busName = anonymous = not on the bus.
    // busOwner is true only when this channel won the name and holds a live
    // subscription.
    std::string busName;
    std::uint64_t busVolumeHandle{0};
    bool busOwner{false};

    friend class CHANNEL::implementationObject;
    friend class SOUND::implementationObject;
    friend class YSE::system;
    friend class SOUND::managerObject;
  };

  /** @brief Root of the channel tree. Every channel ultimately routes here. */
  API channel& ChannelMaster();

  /** @brief Pre-built channel for short sound effects. */
  API channel& ChannelFX();

  /** @brief Pre-built channel for playlists and music tracks. */
  API channel& ChannelMusic();

  /** @brief Pre-built channel for environmental and ambient sounds. */
  API channel& ChannelAmbient();

  /** @brief Pre-built channel for dialogue and voice-over. */
  API channel& ChannelVoice();

  /** @brief Pre-built channel for user-interface sounds. */
  API channel& ChannelGui();
} // namespace YSE

#endif // CHANNEL_H_INCLUDED
