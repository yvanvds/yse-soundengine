using System;
using System.Collections.Generic;
using System.Text;

namespace YSE
{
  public delegate void OnPatcherIntEventHandler(int objID, int value);
  public delegate void OnPatcherFloatEventHandler(int objID, float value);

  public interface IPatcherEventHandler
  {
    event OnPatcherIntEventHandler OnInt;
    event OnPatcherFloatEventHandler OnFloat;
  }

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

    IPatcherEventHandler GetEventHandler();

    void Dispose();
  }
}
