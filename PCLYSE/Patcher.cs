using System;
using System.Collections.Generic;
using System.Text;
using IYse;
using YSE;

namespace YSE
{
	class OscHandler : Yse.oscHandler
	{
		public override void Send(string to)
		{
			OnOscBang(to);
		}

		public override void Send(string to, float value)
		{
			OnOscFloat(to, value);
		}

		public override void Send(string to, int value)
		{
			OnOscInt(to, value);
		}

		public override void Send(string to, string value)
		{
			OnOscString(to, value);
		}

		public event IYse.OnOscBangHandler OnOscBang;
		public event IYse.OnOscIntHandler OnOscInt;
		public event IYse.OnOscFloatHandler OnOscFloat;
		public event IYse.OnOscStringHandler OnOscString;
	}

  public class Patcher : Yse.patcher, IYse.IPatcher
  {
		public event OnOscBangHandler OnOscBang;
		public event OnOscFloatHandler OnOscFloat;
		public event OnOscIntHandler OnOscInt;
		public event OnOscStringHandler OnOscString;

		private OscHandler Osc;

		public Patcher()
		{
			Osc = new OscHandler();
			Osc.OnOscBang += (to) => OnOscBang(to);
			Osc.OnOscInt += (to, value) => OnOscInt(to, value);
			Osc.OnOscFloat += (to, value) => OnOscFloat(to, value);
			Osc.OnOscString += (to, value) => OnOscString(to, value);
		}

		public void Connect(IYse.IHandle from, int pinOut, IYse.IHandle to, int pinIn)
    {
      base.Connect(((Handle)from).GetSource(), pinOut, ((Handle)to).GetSource(), pinIn);
    }

    public void Create(int mainOutputs)
    {
      create(mainOutputs);
			SetOscHandler(Osc);
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
