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

    void Connect(IHandle from, int outlet, IHandle to, int inlet);
    void Disconnect(IHandle from, int outlet, IHandle to, int inlet);

    void Dispose();
  }
}
