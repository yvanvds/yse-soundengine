using System;
using System.Collections.Generic;
using System.Text;

namespace YSE
{
  public enum OUT_TYPE
  {
    INVALID,
    BANG,
    FLOAT,
    BUFFER,
  };

  public interface IHandle
  {
    string Type();

    void SetData(uint inlet, float value);
    void SetParam(uint pos, float value);

    int Inputs { get; }
    int Outputs { get; }

    bool IsDSPInput(uint inlet);

    OUT_TYPE OutputDataType(uint pin);
    
  }
}
