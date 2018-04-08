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

    void SetBang(uint inlet);
    void SetIntData(uint inlet, int value);
    void SetFloatData(uint inlet, float value);

    void SetPosition(Pos pos);
    Pos GetPosition();

    int Inputs { get; }
    int Outputs { get; }

    bool IsDSPInput(uint inlet);
    OUT_TYPE OutputDataType(uint pin);
    
    string Name { get; }
    uint GetID();
    string GetArgs();
    void SetArgs(string args);

    string GetGuiValue();

    uint GetConnections(uint outlet);
    uint GetConnectionTarget(uint outlet, uint connection);
    uint GetConnectionTargetInlet(uint outlet, uint connection);
  }
}
