using System;
using System.Collections.Generic;
using System.Text;

namespace YSE
{
  public interface IHandle
  {
    string Type();

    bool SetData(uint pin, bool value);
    bool SetData(uint pin, int value);
    bool SetData(uint pin, float value);
    bool SetData(uint pin, string value);
  }
}
