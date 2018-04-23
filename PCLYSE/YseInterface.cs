using System;
using System.Collections.Generic;
using System.Text;
using YSE;

namespace YSE
{
  public class YseInterface : IYse.IYseInterface
  {
    public YseInterface(IYse.OnMessageEventHandler logFunction)
    {
      log.OnMessage += logFunction;
    }
    
    YSE.System system = new YSE.System();
    public IYse.ISystem System => system;

    PremadeChannel master = new PremadeChannel(Yse.Yse.ChannelMaster());
    public IYse.IChannel ChannelMaster => master;

    PremadeChannel fx = new PremadeChannel(Yse.Yse.ChannelFX());
    public IYse.IChannel ChannelFX => fx;

    PremadeChannel music = new PremadeChannel(Yse.Yse.ChannelMusic());
    public IYse.IChannel ChannelMusic => music;

    PremadeChannel ambient = new PremadeChannel(Yse.Yse.ChannelAmbient());
    public IYse.IChannel ChannelAmbient => ambient;

    PremadeChannel voice = new PremadeChannel(Yse.Yse.ChannelVoice());
    public IYse.IChannel ChannelVoice => voice;

    PremadeChannel gui = new PremadeChannel(Yse.Yse.ChannelGui());
    public IYse.IChannel ChannelGui => gui;

    BufferIO IO = new BufferIO();
    public IYse.IBufferIO BufferIO => IO;

    Listener listener = new Listener();
    public IYse.IListener Listener => listener;

    Log log = new Log();
    public IYse.ILog Log => log;

    public IYse.ISound NewSound()
    {
      return new Sound();
    }

    public IYse.IChannel NewChannel()
    {
      return new Channel();
    }

    public IYse.IReverb NewReverb()
    {
      return new Reverb();
    }

    public IYse.IPatcher NewPatcher()
    {
      return new Patcher();
    }
  }
}
