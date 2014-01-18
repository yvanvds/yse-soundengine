#pragma once
#include "headers/types.hpp"
#include "headers/defines.hpp"

namespace YSE {
  class sound;
  class system;
  class channelimpl; // internal use only

  class API channel {
  public:
    channel&  create(               ); // link to global channel
    channel&  create(channel& parent); // link to parent channel
    channel&  volume(Flt value); // changes the channel volume
    Flt       volume(         );

    channel& moveTo(channel& parent); // detach channel from current parent and link it to new parent

		channel& set(Int count); // use this for custom speaker positions, in combination with the pos function below
		channel& pos(Int nr, Flt angle); // set speaker to angle in degrees (-180 -> 180)

    Bool valid(); // returns false if channel could not be created or if it was released
    channel& release();
    
    channel& attachReverb(Bool value = true); // attach the reverb processing to this channel
    channel& underWater(Flt depth); // set underwater FX. Best to do this only for one channel (and group all other channels you want the FX applied to below this one)
    Flt underWater();

    channel();
   ~channel();

  private:
    void createGlobal();
    channelimpl *pimpl;
    friend class sound;
    friend class system;
  };

  extern API channel ChannelFX;
	extern API channel ChannelMusic;
	extern API channel ChannelAmbient;
	extern API channel ChannelVoice;
	extern API channel ChannelGui;
	extern API channel ChannelGlobal;

}
