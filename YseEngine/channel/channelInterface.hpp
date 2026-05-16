/*
  ==============================================================================

    channel.h
    Created: 30 Jan 2014 4:20:50pm
    Author:  yvan

  ==============================================================================
*/

#ifndef CHANNELINTERFACE_H_INCLUDED
#define CHANNELINTERFACE_H_INCLUDED

#include <string>
#include "../headers/defines.hpp"
#include "../headers/types.hpp"
#include "channel.hpp"


namespace YSE {
  class system;

  /// @cond INTERNAL
  namespace SOUND {
    class managerObject;
    class implementationObject;
  }
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
    channel& create(const char * name, channel& parent);

    /** @brief Set the channel volume in the range [0.0, 1.0]. */
    channel& setVolume(float value);

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
    const char * getName() { return name.c_str(); }

    /** @brief Construct an empty channel.
     *
     *  Channels are only usable after ``create`` has been called.
     */
    channel();
    ~channel();
  private:

    void createGlobal();

    Flt volume;
    Bool allowVirtual;
    std::string name;
    CHANNEL::implementationObject * pimpl;

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
}




#endif  // CHANNEL_H_INCLUDED
