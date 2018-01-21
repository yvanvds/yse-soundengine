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

    public void Disconnect(IHandle from, int outlet, IHandle to, int inlet)
    {
      base.Disconnect(((Handle)from).GetSource(), outlet, ((Handle)to).GetSource(), inlet);
    }

    public new IHandle CreateObject(string type)
    {
      Yse.pHandle handle = base.CreateObject(type);
      if (handle == null) return null;
      else return new Handle(handle);
    }

    public void DeleteObject(IHandle handle)
    {
      base.DeleteObject(((Handle)handle).GetSource());
    }
  }
}
