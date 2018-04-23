using System;
using System.Collections.Generic;
using System.Text;
using YSE;

namespace YSE
{
  class LogHandler : Yse.logHandler
  {
    public override void AddMessage(string message)
    {
      OnMessage(message);
    }

    public event IYse.OnMessageEventHandler OnMessage;
  }

  public class Log : IYse.ILog
  {
    public Log()
    {
      // needed to pass log messages from unmanaged to managed code
      LH = new LogHandler();
      LH.OnMessage += (message) => OnMessage(message);
      Yse.Yse.Log().setHandler(LH);
    }

    public IYse.ERROR_LEVEL Level
    {
      get => Convert(Yse.Yse.Log().getLevel());
      set => Yse.Yse.Log().setLevel(Convert(value));
    }

    public string LogFile
    {
      get => Yse.Yse.Log().getLogfile();
      set => Yse.Yse.Log().setLogfile(value);
    }

    public event IYse.OnMessageEventHandler OnMessage;

    public void SendMessage(string message)
    {
      Yse.Yse.Log().sendMessage(message);
    }

    private LogHandler LH;

    static IYse.ERROR_LEVEL Convert(Yse.ERROR_LEVEL value)
    {
      switch(value)
      {
        case Yse.ERROR_LEVEL.EL_NONE: return IYse.ERROR_LEVEL.NONE;
        case Yse.ERROR_LEVEL.EL_WARNING: return IYse.ERROR_LEVEL.WARNING;
        case Yse.ERROR_LEVEL.EL_ERROR: return IYse.ERROR_LEVEL.ERROR;
        case Yse.ERROR_LEVEL.EL_DEBUG: return IYse.ERROR_LEVEL.DEBUG;
      }
      return IYse.ERROR_LEVEL.NONE;
    }

    static Yse.ERROR_LEVEL Convert(IYse.ERROR_LEVEL value)
    {
      switch (value)
      {
        case IYse.ERROR_LEVEL.NONE: return Yse.ERROR_LEVEL.EL_NONE;
        case IYse.ERROR_LEVEL.WARNING: return Yse.ERROR_LEVEL.EL_WARNING;
        case IYse.ERROR_LEVEL.ERROR: return Yse.ERROR_LEVEL.EL_ERROR;
        case IYse.ERROR_LEVEL.DEBUG: return Yse.ERROR_LEVEL.EL_DEBUG;
      }
      return Yse.ERROR_LEVEL.EL_NONE;
    }
  }
}
