using System;
using System.Collections.Generic;
using System.Text;
using YSE;

namespace YSE
{
  public class Reverb : Yse.reverb, IYse.IReverb
  {
    Yse.reverb source;

    public Reverb(Yse.reverb source = null)
    {
      if(source != null)
      {
        this.source = source;
      } else
      {
        source = this;
      }
    }

    public bool Active { get => source.getActive(); set => source.setActive(value); }
    public float Size { get => source.getSize(); set => source.setSize(value); }
    public float RollOff { get => source.getRollOff(); set => source.setRollOff(value); }
    public float RoomSize { get => source.getRoomSize(); set => source.setRoomSize(value); }
    public float Damping { get => source.getDamping(); set => source.setDamping(value); }

    public float Wet => source.getWet();

    public float Dry => source.getDry();

    public float ModulationFrequency => source.getModulationFrequency();

    public float ModulationWidth => source.getModulationWidth();

    public void Create()
    {
      source.create();
    }

    public IYse.Pos GetPos()
    {
      Yse.Pos p = source.getPosition();
      return new IYse.Pos(p.x, p.y, p.z);
    }

    public bool IsValid()
    {
      return source.isValid();
    }

    public float ReflectionGain(int reflection)
    {
      return source.getReflectionGain(reflection);
    }

    public int ReflectionTime(int reflection)
    {
      return source.getReflectionTime(reflection);
    }

    public void SetDryWetBalance(float dry, float wet)
    {
      source.setDryWetBalance(dry, wet);
    }

    public void SetModulation(float frequency, float width)
    {
      source.setModulation(frequency, width);
    }

    public void SetPos(IYse.Pos value)
    {
      source.setPosition(new Yse.Pos(value.X, value.Y, value.Z));
    }

    public void SetPreset(IYse.REVERB_PRESET rp)
    {
      Yse.REVERB_PRESET p = new Yse.REVERB_PRESET();
      switch(rp)
      {
        case IYse.REVERB_PRESET.REVERB_BATHROOM: p = Yse.REVERB_PRESET.REVERB_BATHROOM; break;
        case IYse.REVERB_PRESET.REVERB_CAVE: p = Yse.REVERB_PRESET.REVERB_CAVE; break;
        case IYse.REVERB_PRESET.REVERB_GENERIC: p = Yse.REVERB_PRESET.REVERB_GENERIC; break;
        case IYse.REVERB_PRESET.REVERB_HALL: p = Yse.REVERB_PRESET.REVERB_HALL; break;
        case IYse.REVERB_PRESET.REVERB_LARGEROOM: p = Yse.REVERB_PRESET.REVERB_LARGEROOM; break;
        case IYse.REVERB_PRESET.REVERB_OFF: p = Yse.REVERB_PRESET.REVERB_OFF; break;
        case IYse.REVERB_PRESET.REVERB_PADDED: p = Yse.REVERB_PRESET.REVERB_PADDED; break;
        case IYse.REVERB_PRESET.REVERB_ROOM: p = Yse.REVERB_PRESET.REVERB_ROOM; break;
        case IYse.REVERB_PRESET.REVERB_SEWERPIPE: p = Yse.REVERB_PRESET.REVERB_SEWERPIPE; break;
        case IYse.REVERB_PRESET.REVERB_STONEROOM: p = Yse.REVERB_PRESET.REVERB_STONEROOM; break;
        case IYse.REVERB_PRESET.REVERB_UNDERWATER: p = Yse.REVERB_PRESET.REVERB_UNDERWATER; break;
      }
      source.setPreset(p);
    }

    public void SetReflection(int reflection, int time, float gain)
    {
      source.setReflection(reflection, time, gain);
    }
  }
}
