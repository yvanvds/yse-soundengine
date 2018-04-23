using System;
using System.Collections.Generic;
using System.Text;

namespace IYse
{
  public interface ISystem
  {
    bool Init();
    void Update();
    void Close();

    //IReverb GlobalReverb { get; }

    uint NumDevices { get; }
    //IDevice GetDevice(int nr);

    int MaxSounds { get; set; }

    bool AudioTest { get; set; }

    float CpuLoad { get; }

    IReverb GetReverb();
  }
}
