using System;
using System.Collections.Generic;
using System.Text;
using YSE;

namespace YSE
{

  public class Patcher : Yse.patcher, IYse.IPatcher
  {
    public void Connect(IYse.IHandle from, int pinOut, IYse.IHandle to, int pinIn)
    {
      base.Connect(((Handle)from).GetSource(), pinOut, ((Handle)to).GetSource(), pinIn);
    }

    public void Create(int mainOutputs)
    {
      create(mainOutputs);
    }

    public void Disconnect(IYse.IHandle from, int outlet, IYse.IHandle to, int inlet)
    {
      base.Disconnect(((Handle)from).GetSource(), outlet, ((Handle)to).GetSource(), inlet);
    }

    public void DeleteObject(IYse.IHandle handle)
    {
      base.DeleteObject(((Handle)handle).GetSource());
    }

    IYse.IHandle IYse.IPatcher.CreateObject(string type, string args)
    {
      Yse.pHandle handle = base.CreateObject(type, args);
      if (handle == null) return null;
      else return new Handle(handle);
    }

    public uint NumObjects()
    {
      return base.Objects();
    }

    IYse.IHandle IYse.IPatcher.GetHandleFromList(uint obj)
    {
      Yse.pHandle handle = base.GetHandleFromList(obj);
      if (handle == null) return null;
      else return new Handle(handle);
    }

    IYse.IHandle IYse.IPatcher.GetHandleFromID(uint obj)
    {
      Yse.pHandle handle = base.GetHandleFromID(obj);
      if (handle == null) return null;
      else return new Handle(handle);
    }

  }
}
