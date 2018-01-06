using System;
using System.Collections.Generic;
using System.Text;
using YSE;

namespace YSENET
{
  public interface SourceChannel
  {
    Yse.channel GetSource();
  }

  class Channel : Yse.channel, YSE.IChannel, SourceChannel
  {
    public float Volume { get => getVolume(); set => setVolume(value); }
    public bool Virtual { get => getVirtual(); set => setVirtual(value); }

    public bool Valid => isValid();

    public string Name => getName();

    public void AttachReverb()
    {
      attachReverb();
    }

    public void Create(string name, IChannel parent)
    {
      create(name, parent as Channel);
    }

    public void MoveTo(IChannel parent)
    {
      moveTo(parent as Channel);
    }

    public Yse.channel GetSource()
    {
      return (Yse.channel)this;
    }
    
    public bool IsPremade()
    {
      return false;
    }
  }


  class PremadeChannel : YSE.IChannel, SourceChannel
  {
    Yse.channel source;

    public PremadeChannel(Yse.channel source)
    {
      this.source = source;
    }

    public float Volume { get => source.getVolume(); set => source.setVolume(value); }
    public bool Virtual { get => source.getVirtual(); set => source.setVirtual(value); }

    public bool Valid => source.isValid();

    public string Name => source.getName();

    public void AttachReverb()
    {
      source.attachReverb();
    }

    public void Create(string name, IChannel parent)
    {
      // not implemented
    }

    public void Dispose()
    {
      // not implemented
    }

    public void MoveTo(IChannel parent)
    {
      source.moveTo(parent as Channel);
    }

    public Yse.channel GetSource()
    {
      return source;
    }

    public bool IsPremade()
    {
      return true;
    }
  }
}
