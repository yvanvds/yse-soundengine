using System;
using System.Collections.Generic;
using System.Text;

namespace YSENET
{
  class Handle : YSE.IHandle
  {
    Yse.pHandle source;

    public Yse.pHandle GetSource()
    {
      return source;
    }

    public Handle(Yse.pHandle source)
    {
      this.source = source;
    }

    public bool SetData(uint pin, bool value)
    {
      return source.SetData(pin, value);
    }

    public bool SetData(uint pin, int value)
    {
      return source.SetData(pin, value);
    }

    public bool SetData(uint pin, float value)
    {
      return source.SetData(pin, value);
    }

    public bool SetData(uint pin, string value)
    {
      return source.SetData(pin, value);
    }

    public string Type()
    {
      return source.Type();
    }
  }
}
