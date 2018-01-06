using System;
using System.Collections.Generic;
using System.Text;
using YSE;

namespace YSENET
{
  class Patcher : Yse.patcher, YSE.IPatcher
  {
    public void Connect(IHandle from, int pinOut, IHandle to, int pinIn)
    {
      base.Connect(((Handle)from).GetSource(), pinOut, ((Handle)to).GetSource(), pinIn);
    }

    public void Create(int mainOutputs)
    {
      create(mainOutputs);
    }

    public void Disconnect(IHandle to, int pinIn)
    {
      base.Disconnect(((Handle)to).GetSource(), pinIn);
    }

    public new IHandle GetOutputHandle(uint output)
    {
      return new Handle(base.GetOutputHandle(output));
    }

    public new IHandle AddObject(string type)
    {
      return new Handle(base.AddObject(type));
    }
  }
}
