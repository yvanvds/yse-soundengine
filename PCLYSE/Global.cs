using System;
using System.Collections.Generic;
using System.Text;
using YSE;

namespace YSENET
{
  public class Global : YSE.IGlobal
  {
    System system = new System();
    public ISystem System => system;

    PremadeChannel master = new PremadeChannel(Yse.Yse.ChannelMaster());
    public IChannel ChannelMaster => master;

    PremadeChannel fx = new PremadeChannel(Yse.Yse.ChannelFX());
    public IChannel ChannelFX => fx;

    PremadeChannel music = new PremadeChannel(Yse.Yse.ChannelMusic());
    public IChannel ChannelMusic => music;

    PremadeChannel ambient = new PremadeChannel(Yse.Yse.ChannelAmbient());
    public IChannel ChannelAmbient => ambient;

    PremadeChannel voice = new PremadeChannel(Yse.Yse.ChannelVoice());
    public IChannel ChannelVoice => voice;

    PremadeChannel gui = new PremadeChannel(Yse.Yse.ChannelGui());
    public IChannel ChannelGui => gui;

    BufferIO IO = new BufferIO();
    public IBufferIO BufferIO => IO;

    Listener listener = new Listener();
    public IListener Listener => listener;

    public ISound CreateSound()
    {
      return new Sound();
    }

    public IChannel CreateChannel()
    {
      return new Channel();
    }

    public IReverb CreateReverb()
    {
      return new Reverb();
    }

    public IPatcher CreatePatcher()
    {
      return new Patcher();
    }
  }
}
