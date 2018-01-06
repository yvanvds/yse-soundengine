using System;
using System.Collections.Generic;
using System.Text;

namespace YSE
{
  public interface IGlobal
  {
    ISystem System { get; }
    IBufferIO BufferIO { get; }
    IListener Listener { get; }

    IChannel ChannelMaster { get; }
    IChannel ChannelFX { get; }
    IChannel ChannelMusic { get; }
    IChannel ChannelAmbient { get; }
    IChannel ChannelVoice { get; }
    IChannel ChannelGui { get; }

    ISound CreateSound();
    IChannel CreateChannel();
    IReverb CreateReverb();
    IPatcher CreatePatcher();
  }
}
