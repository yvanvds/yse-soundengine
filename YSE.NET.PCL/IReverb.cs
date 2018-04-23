using System;
using System.Collections.Generic;
using System.Text;

namespace IYse
{
  public enum REVERB_PRESET
  {
    REVERB_OFF,
    REVERB_GENERIC,
    REVERB_PADDED,
    REVERB_ROOM,
    REVERB_BATHROOM,
    REVERB_STONEROOM,
    REVERB_LARGEROOM,
    REVERB_HALL,
    REVERB_CAVE,
    REVERB_SEWERPIPE,
    REVERB_UNDERWATER,
  }

  public interface IReverb
  {
    void Create();
    bool IsValid();
    bool Active { set; get; }

    void SetPos(Pos value);
    Pos GetPos();

    float Size { set; get; }
    float RollOff { set; get; }
    float RoomSize { set; get; }
    float Damping { set; get; }

    void SetDryWetBalance(float dry, float wet);
    float Wet { get; }
    float Dry { get; }

    void SetModulation(float frequency, float width);
    float ModulationFrequency { get; }
    float ModulationWidth { get; }

    void SetReflection(int reflection, int time, float gain);
    int ReflectionTime(int reflection);
    float ReflectionGain(int reflection);

    void SetPreset(REVERB_PRESET rp);
  }
}
