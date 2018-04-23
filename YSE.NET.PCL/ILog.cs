using System;
using System.Collections.Generic;
using System.Text;

namespace IYse
{
  public enum ERROR_LEVEL
  {
    NONE,
    ERROR,
    WARNING,
    DEBUG,
  }

  public delegate void OnMessageEventHandler(string message);

  public interface ILog
  {
    void SendMessage(string message);
    ERROR_LEVEL Level { get; set; }
    string LogFile { get; set; }
    event OnMessageEventHandler OnMessage;
  }
}
