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
#include <vector>
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
     *  @param sendSlots Number of aux-send slots to allocate for this channel
     *                (issue #165). Sized once off the audio thread and never
     *                resized. Default 4; raise it for a channel that fans out to
     *                many return buses.
     */
    channel& create(const char* name, channel& parent, int sendSlots = 4);

    /**
     *  @brief Create this channel as a send/return bus.
     *
     *  A return bus is an ordinary channel (it keeps ``setDSP`` inserts,
     *  ``attachReverb``, ``setVolume``, and metering) with two differences: it is
     *  excluded from the normal mix tree, and other channels route scaled copies
     *  of their signal into it via ``send``. Its output folds into ``MainMix``
     *  after the source tree, so a single reverb or delay on a return can serve
     *  many channels (the classic aux-send topology). A return may itself
     *  ``send`` into another return (an acyclic delay→reverb chain, for example);
     *  cycles are rejected at wiring time.
     *
     *  Call this instead of ``create`` — not after it. The engine takes no
     *  ownership of any effect attached with ``setDSP``.
     *
     *  @param name      Channel name, used for log output.
     *  @param sendSlots Number of aux-send slots on the return itself (for
     *                   return→return routing). Default 4.
     *  @return ``*this`` for fluent chaining (e.g. ``r.makeReturn("verb").setDSP(&plate)``).
     */
    channel& makeReturn(const char* name = "return", int sendSlots = 4);

    /** @brief Whether this channel is a send/return bus (issue #165). */
    bool isReturn() const;

    /**
     *  @brief Route a scaled copy of this channel into a return bus.
     *
     *  Wires send slot @p slot (in ``[0, sendSlots)``) to @p returnBus at the
     *  given @p level. The send is **post-fader** by default (it follows this
     *  channel's own volume); pass ``preFader = true`` for a cue-style send
     *  independent of the fader. Re-calling ``send`` on the same slot re-points
     *  it. The level ramps in, so wiring a live send never clicks.
     *
     *  Illegal wirings are rejected on the calling (control) thread and logged,
     *  never reaching the audio thread: a target that is not a return, a
     *  self-send, or a return→return edge that would close a cycle.
     *
     *  @param slot      Send-slot index in ``[0, sendSlots)``.
     *  @param returnBus A channel created with ``makeReturn``.
     *  @param level     Send gain (typically ``[0, 1]``).
     *  @param preFader  Tap before this channel's fader when true (default false).
     *  @return ``*this`` for fluent chaining.
     */
    channel& send(int slot, channel& returnBus, float level, bool preFader = false);

    /**
     *  @brief Set a send slot's level, ramped and click-free.
     *
     *  Safe to call every control tick — send levels are designed as modulation
     *  targets (a patcher outlet, a live-coded expression, a proximity rule), so
     *  continuous writes fuse into the per-block ramp without zippering.
     *
     *  @param slot  Send-slot index in ``[0, sendSlots)``.
     *  @param level New send gain.
     *  @return ``*this`` for fluent chaining.
     */
    channel& setSendLevel(int slot, float level);

    /** @brief Detach send slot @p slot (fully disconnects it from its return). */
    channel& clearSend(int slot);

    /** @brief Current target level of send slot @p slot, or 0 if unset/invalid. */
    float getSendLevel(int slot) const;

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

    // ─── Send/return state (issue #165) ───
    // Control-thread mirror of this channel's send wiring. Lets the interface
    // validate slots, report getSendLevel(), and maintain the control-thread
    // return→return graph (remove the old edge when a slot is re-pointed). None
    // of this is touched on the audio thread — the impl carries its own copy,
    // updated via messages.
    bool _isReturn{false};
    int _sendSlots{0};
    struct SendMirror {
      channel* target{nullptr}; // interface identity of the target return
      CHANNEL::implementationObject* targetImpl{nullptr}; // graph key (value only; never dereferenced)
      float level{0.f};
      bool preFader{false};
      bool graphEdge{false}; // this slot contributed a return→return graph edge
    };
    std::vector<SendMirror> _sends;

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
