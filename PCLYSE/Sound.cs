using System;
using System.Collections.Generic;
using System.Text;
using YSE;

namespace YSE
{
  public class Sound : Yse.sound, IYse.ISound
  {
    public bool Valid => isValid();

    public float Spread { get => spread(); set => spread(value); }
    public float Speed { get => speed(); set => speed(value); }
    public bool Loop { get => looping(); set => looping(value); }
    public float Volume { get => volume(); set => volume(value); }

    public bool Playing => isPlaying();

    public bool Paused => isPaused();

    public bool Stopped => isStopped();

    public float Time { get => time(); set => time(value); }

    public float Length => length();

    public bool Relative { get => relative(); set => relative(value); }
    float IYse.ISound.Size { get => size(); set => size(value); }

    public void Create(string Name, IYse.IChannel channel = null, bool loop = false, float volume = 1, bool streaming = false)
    {
      if(channel == null)
      {
        create(Name, null, loop, volume, streaming);
      }
      else if (channel.IsPremade())
      {
        create(Name, (channel as PremadeChannel).GetSource(), loop, volume, streaming);
      } else
      {
        create(Name, (channel as Channel).GetSource(), loop, volume, streaming);
      }
    }

    public void Create(IYse.IPatcher patcher, IYse.IChannel channel = null, float volume = 1)
    {
      if (channel == null)
      {
        create((patcher as Yse.patcher), null, volume);
      }
      else if (channel.IsPremade())
      {
        create((patcher as Yse.patcher), (channel as PremadeChannel).GetSource(), volume);
      }
      else
      {
        create((patcher as Yse.patcher), (channel as Channel).GetSource(), volume);
      }
    }

    public void FadeAndStop(uint time)
    {
      fadeAndStop(time);
    }

    public IYse.Pos GetPos()
    {
      Yse.Pos p = pos();
      return new IYse.Pos(p.x, p.y, p.z);
    }

    public void Pause()
    {
      pause();
    }

    public void Play()
    {
      play();
    }

    public void Restart()
    {
      restart();
    }

    public void SetPos(IYse.Pos p)
    {
      Yse.Pos pos = new Yse.Pos(p.X, p.Y, p.Z);
      base.pos(pos);
    }

    public void SetVolume(float value, uint time)
    {
      SetVolume(value, time);
    }

    public void Stop()
    {
      stop();
    }

    public void Toggle()
    {
      toggle();
    }
  }
}
