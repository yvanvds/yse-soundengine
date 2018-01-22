using System;
using System.Collections.Generic;
using System.Text;
using YSE;

namespace YSENET
{
  class Handle : YSE.IHandle
  {
    Yse.pHandle source;

    public int Inputs => source.GetInputs();

    public int Outputs => source.GetOutputs();

    public Pos Position
    {
      get
      {
        Yse.Pos f = source.GetPosition();
        return new YSE.Pos(f.x, f.y, f.z);
      }
      set
      {
        Yse.Pos f = new Yse.Pos(value.X, value.Y, value.Z);
        source.SetPosition(f);
      }
    }

    public string Name => source.GetName();

    public string Args
    {
      get => source.GetParams();
      set => source.SetParams(value);
    }

    public Yse.pHandle GetSource()
    {
      return source;
    }

    public Handle(Yse.pHandle source)
    {
      this.source = source;
    }

    public void SetData(uint pin, float value)
    {
      source.SetData(pin, value);
    }

    public string Type()
    {
      return source.Type();
    }

    public OUT_TYPE OutputDataType(uint pin)
    {
      return Convert(source.OutputDataType(pin));
    }

    public bool IsDSPInput(uint inlet)
    {
      return source.IsDSPInput(inlet);
    }

    OUT_TYPE IHandle.OutputDataType(uint outlet)
    {
      return Convert(source.OutputDataType(outlet));
    }

    OUT_TYPE Convert(Yse.OUT_TYPE value)
    {
      switch (value)
      {
        case Yse.OUT_TYPE.BANG: return OUT_TYPE.BANG;
        case Yse.OUT_TYPE.BUFFER: return OUT_TYPE.BUFFER;
        case Yse.OUT_TYPE.FLOAT: return OUT_TYPE.FLOAT;
        default: return OUT_TYPE.INVALID;
      }
    }

    public uint GetConnections(uint outlet)
    {
      return source.GetConnections(outlet);
    }

    public uint GetConnectionTarget(uint outlet, uint connection)
    {
      return source.GetConnectionTarget(outlet, connection);
    }

    public uint GetConnectionTargetInlet(uint outlet, uint connection)
    {
      return source.GetConnectionTargetInlet(outlet, connection);
    }
  }
}
