using System;
using System.Collections.Generic;
using System.Text;
using YSE;

namespace YSENET
{
  class System : YSE.ISystem
  {
    public uint NumDevices
    {
      get => Yse.Yse.System().getNumDevices();
    }

    public int MaxSounds { get => Yse.Yse.System().maxSounds(); set => Yse.Yse.System().maxSounds(value); }

    public float CpuLoad
    {
      get
      {
        return Yse.Yse.System().cpuLoad();
      }
    }

    bool ISystem.AudioTest
    {
      get => AudioTestOn;
      set
      {
        Yse.Yse.System().AudioTest(value);
        AudioTestOn = value;
      }
    }

    public void Close()
    {
      Yse.Yse.System().close();
    }

    public bool Init()
    {
      return Yse.Yse.System().init();
    }

    public void Update()
    {
      Yse.Yse.System().update();
    }

    public IReverb GetReverb()
    {
      return new YSENET.Reverb(Yse.Yse.System().getGlobalReverb());
    }

    private bool AudioTestOn = false;
  }
}
