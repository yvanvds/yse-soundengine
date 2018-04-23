using System;
using System.Collections.Generic;
using System.Text;
using YSE;

namespace YSE
{
  public class Handle : IYse.IHandle
  {
    Yse.pHandle source;

    public int Inputs => source.GetInputs();

    public int Outputs => source.GetOutputs();

    public string Name => source.GetName();

    public string GetArgs()
    {
      return source.GetParams(); 
    }

    public void SetArgs(string args) {
      source.SetParams(args);
    }

    public Yse.pHandle GetSource()
    {
      return source;
    }

    public Handle(Yse.pHandle source)
    {
      this.source = source;
    }

    public void SetBang(uint inlet)
    {
      source.SetBang(inlet);
    }

    public void SetIntData(uint inlet, int value)
    {
      source.SetIntData(inlet, value);
    }

    public void SetFloatData(uint inlet, float value)
    {
      source.SetFloatData(inlet, value);
    }

    public void SetListData(uint inlet, string value)
    {
      source.SetListData(inlet, value);
    }

    public string Type()
    {
      return source.Type();
    }

    public IYse.OUT_TYPE OutputDataType(uint pin)
    {
      return Convert(source.OutputDataType(pin));
    }

    public bool IsDSPInput(uint inlet)
    {
      return source.IsDSPInput(inlet);
    }

    IYse.OUT_TYPE IYse.IHandle.OutputDataType(uint outlet)
    {
      return Convert(source.OutputDataType(outlet));
    }

    IYse.OUT_TYPE Convert(Yse.OUT_TYPE value)
    {
      switch (value)
      {
        case Yse.OUT_TYPE.BANG: return IYse.OUT_TYPE.BANG;
        case Yse.OUT_TYPE.BUFFER: return IYse.OUT_TYPE.BUFFER;
        case Yse.OUT_TYPE.FLOAT: return IYse.OUT_TYPE.FLOAT;
        default: return IYse.OUT_TYPE.INVALID;
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

    public uint GetID()
    {
      return source.GetID();
    }

    public string GetGuiValue()
    {
      return source.GetGuiValue();
    }

    public void SetGuiProperty(string key, string value)
    {
      source.SetGuiProperty(key, value);
    }

    public string GetGuiProperty(string key)
    {
      return source.GetGuiProperty(key);
    }
  }
}
