using System;
using System.Collections.Generic;
using System.Text;

namespace YSE
{
  public interface IPatcher
  {
    void Create(int mainOutputs);

    IHandle CreateObject(string type, string args = "");
    void DeleteObject(IHandle handle);
    void Clear();

    void Connect(IHandle from, int outlet, IHandle to, int inlet);
    void Disconnect(IHandle from, int outlet, IHandle to, int inlet);

    string DumpJSON();
    void ParseJSON(string content);

    uint NumObjects();
    IHandle GetHandleFromList(uint obj);
    IHandle GetHandleFromID(uint obj);

    void Dispose();
  }
}
