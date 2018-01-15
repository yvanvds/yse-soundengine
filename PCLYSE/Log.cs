using System;
using System.Collections.Generic;
using System.Text;
using YSE;

namespace YSENET
{
  class Log : YSE.ILog
  {
    public ERROR_LEVEL Level
    {
      get => Convert(Yse.Yse.Log().getLevel());
      set => Yse.Yse.Log().setLevel(Convert(value));
    }

    public string LogFile
    {
      get => Yse.Yse.Log().getLogfile();
      set => Yse.Yse.Log().setLogfile(value);
    }

    public void SendMessage(string message)
    {
      Yse.Yse.Log().sendMessage(message);
    }

    static ERROR_LEVEL Convert(Yse.ERROR_LEVEL value)
    {
      switch(value)
      {
        case Yse.ERROR_LEVEL.EL_NONE: return ERROR_LEVEL.NONE;
        case Yse.ERROR_LEVEL.EL_WARNING: return ERROR_LEVEL.WARNING;
        case Yse.ERROR_LEVEL.EL_ERROR: return ERROR_LEVEL.ERROR;
        case Yse.ERROR_LEVEL.EL_DEBUG: return ERROR_LEVEL.DEBUG;
      }
      return ERROR_LEVEL.NONE;
    }

    static Yse.ERROR_LEVEL Convert(ERROR_LEVEL value)
    {
      switch (value)
      {
        case ERROR_LEVEL.NONE: return Yse.ERROR_LEVEL.EL_NONE;
        case ERROR_LEVEL.WARNING: return Yse.ERROR_LEVEL.EL_WARNING;
        case ERROR_LEVEL.ERROR: return Yse.ERROR_LEVEL.EL_ERROR;
        case ERROR_LEVEL.DEBUG: return Yse.ERROR_LEVEL.EL_DEBUG;
      }
      return Yse.ERROR_LEVEL.EL_NONE;
    }
  }
}
