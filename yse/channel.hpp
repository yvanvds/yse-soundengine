/*
  ==============================================================================

    channel.h
    Created: 30 Jan 2014 4:20:50pm
    Author:  yvan

  ==============================================================================
*/

#ifndef CHANNEL_H_INCLUDED
#define CHANNEL_H_INCLUDED

#include "headers/defines.hpp"
#include "headers/types.hpp"
#include "classes.hpp"

namespace YSE {
  
  /**
    Channels are used to control groups of sounds simultaniously. (Quite comparable to 
    channel groups on a mixing console.) Every sound has to be linked to a channel at 
    creation time. Channel can also be linked to another channel, thus creating a tree-like
    structure with subchannels. During DSP callback, all channels will render their
    audio in a separate thread. For this reason it might be a good idea to distribute
    your sounds over several channels.
   
    For convenience, several typical channels are already created by the system. There's the 
    MainMix, which is the root of the channel tree. Other channels (all linked to MainMix) are:
   
    - FX     : Intended for short audio effects
    - Music  : Intended for playlists and other music
    - Ambient: Intended for environmental sounds
    - Voice  : Intended for dialogs
    - Gui    : Intended for interface sounds
   
   Of course you can use these channels for anything you like.
  */
  class API channel {
  public:
    
    /**
        Creates the channel object. You do have to call this before using a custom channel.
        Premade channels (FX and such) call this function internally.
     
        @param name     The name of the channel. This can be used in logfiles.
        @param parent   The parent channel. All channels must be linked to an existing channel.
    */
    channel&  create(const char * name, channel& parent);
    
    /**
        Changes the volume of a channel. Range is 0-1.
    */
    channel&  setVolume(Flt value);
      
    /**
        Get the volume of a channel.
    */
    Flt getVolume();

    /**
        Move the channel to another branch in the channel tree. This detaches the channel from its
        current parent and links it to another channel. All sounds and subchannels move along.
     
        @param parent The new parent channel to link this channel to.
    */
    channel& moveTo(channel& parent);

    /**
        Set the number of speakers for this channel. This function should be moved to System and use the channel manager internally
        because all channels are supposed to have the same nr of channels.
    */
    channel& setNumberOfSpeakers(Int count); // use this for custom speaker positions, in combination with the pos function below
    channel& pos(Int nr, Flt angle); // set speaker to angle in degrees (-180 -> 180)

    /**
        Checks if the channel is valid.
     
        @return Returns false if the channel has been released or when the system could not create it.
    */
    Bool valid();
      
    /**
        Releases the channel. This will move all sounds and subchannels to the parent channel. Don't use this
        on the main channel! 
        \TODO make it impossible to do so.
    */
    channel& release();

    /**
        Because reverb needs a lot of processing power, there's only one actual reverb object. By default this is attached to the mainMix,
        thereby affecting all channels. If you want to use the reverb on only a subset of channels, call this function on
        the intended channel. The reverb will be moved to this channel.
    */
    channel& attachReverb();

    /**
        The real initialisation of a channel is not done in the constructor. Be sure to call create() first, before
        doing anything else.
    */
    channel();
    ~channel();

  private:
      
    /**
        A special version of create. It is used internally to create the global channel. This is not meant to be used anywhere else.
    */
    void createGlobal();
      
    // channel implementation and friend classes
    INTERNAL::channelImplementation *pimpl;
    friend class sound;
    friend class soundImplementation;
    friend class system;
    friend class soundManager;
  };
  
  /**
   Use these functors to get access to the premade channels.
  */
  channel& ChannelMainMix();
  channel& ChannelFX     ();
  channel& ChannelMusic  ();
  channel& ChannelAmbient();
  channel& ChannelVoice  ();
  channel& ChannelGui    ();
}




#endif  // CHANNEL_H_INCLUDED
