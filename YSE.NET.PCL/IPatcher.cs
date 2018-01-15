using System;
using System.Collections.Generic;
using System.Text;

namespace YSE
{
  public interface IPatcher
  {
    void Create(int mainOutputs);

    IHandle CreateObject(string type);
    void DeleteObject(IHandle handle);

    void Connect(IHandle from, int pinOut, IHandle to, int pinIn);
    void Disconnect(IHandle to, int pinIn);

    IHandle GetOutputHandle(uint output);

    void Dispose();
  }
}
