using System;
using System.Collections.Generic;
using System.Text;

namespace YSE
{
  public enum ERROR_LEVEL
  {
    NONE,
    ERROR,
    WARNING,
    DEBUG,
  }

  public interface ILog
  {
    void SendMessage(string message);
    ERROR_LEVEL Level { get; set; }
    string LogFile { get; set; }
  }
}
