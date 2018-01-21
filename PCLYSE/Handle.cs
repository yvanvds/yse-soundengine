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

    public void SetParam(uint pos, float value)
    {
      source.SetParam(pos, value);
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


  }
}
