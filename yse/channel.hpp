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
  
  class API channel {
  public:
    channel&  create(const char * name, channel& parent); // link to parent channel
    channel&  volume(Flt value); // changes the channel volume
    Flt       volume();

    channel& moveTo(channel& parent); // detach channel from current parent and link it to new parent

    channel& set(Int count); // use this for custom speaker positions, in combination with the pos function below
    channel& pos(Int nr, Flt angle); // set speaker to angle in degrees (-180 -> 180)

    Bool valid(); // returns false if channel could not be created or if it was released
    channel& release();

    channel& attachReverb(); // attach reverb processing to this channel

    

    channel();
    ~channel();

  private:
    void createGlobal();
    INTERNAL::channelImplementation *pimpl;
    friend class sound;
    friend class soundImplementation;
    friend class system;
    friend class soundManager;
  };

  channel& ChannelMainMix();
  channel& ChannelFX     ();
  channel& ChannelMusic  ();
  channel& ChannelAmbient();
  channel& ChannelVoice  ();
  channel& ChannelGui    ();
}




#endif  // CHANNEL_H_INCLUDED
