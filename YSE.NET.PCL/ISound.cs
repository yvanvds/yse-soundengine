using System;
using System.Collections.Generic;
using System.Text;

namespace IYse
{
  public interface ISound
  {
    void Create(String Name, IChannel channel = null, bool loop = false, float volume = 1.0f, bool streaming = false);
    void Create(IPatcher patcher, IChannel channel = null, float volume = 1.0f);
    bool Valid { get; }

    Pos GetPos();
    void SetPos(Pos p);

    float Spread { get; set; }
    float Speed { get; set; }
    float Size { get; set; }
    bool Loop { get; set; }
    float Volume { get; set; }
    void SetVolume(float value, uint time);
    void FadeAndStop(uint time);

    void Play();
    bool Playing { get; }

    void Pause();
    bool Paused { get; }

    void Stop();
    bool Stopped { get; }

    void Toggle();
    void Restart();

    float Time { get; set; }
    float Length { get; }

    bool Relative { get; set; }

    void Dispose();
  }
}
