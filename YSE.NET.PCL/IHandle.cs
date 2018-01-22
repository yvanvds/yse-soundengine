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

    Pos Position { set; get; }

    int Inputs { get; }
    int Outputs { get; }

    bool IsDSPInput(uint inlet);
    OUT_TYPE OutputDataType(uint pin);
    
    string Name { get; }
    string Args { get; set; }

    uint GetConnections(uint outlet);
    uint GetConnectionTarget(uint outlet, uint connection);
    uint GetConnectionTargetInlet(uint outlet, uint connection);
  }
}
