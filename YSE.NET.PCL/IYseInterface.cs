using System;
using System.Collections.Generic;
using System.Text;

namespace IYse
{
  public interface IYseInterface
  {
    ISystem System { get; }

    IChannel ChannelMaster { get; }
    IChannel ChannelFX { get; }
    IChannel ChannelMusic { get; }
    IChannel ChannelAmbient { get; }
    IChannel ChannelVoice { get; }
    IChannel ChannelGui { get; }
    IBufferIO BufferIO { get; }
    IListener Listener { get; }
    ILog Log { get; }

    ISound NewSound();
    IChannel NewChannel();
    IReverb NewReverb();
    IPatcher NewPatcher();
  }
}
