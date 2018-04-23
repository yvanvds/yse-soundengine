using System;
using System.Collections.Generic;
using System.Text;
using YSE;

namespace YSE
{
  public class Listener : IYse.IListener
  {
    public IYse.Pos Forward()
    {
      Yse.Pos f = Yse.Yse.Listener().forward();
      return new IYse.Pos(f.x, f.y, f.z);
    }

    public void Orient(IYse.Pos forward)
    {
      Yse.Pos f = new Yse.Pos(forward.X, forward.Y, forward.Z);
      Yse.Yse.Listener().orient(f);
    }

    public void Orient(IYse.Pos forward, IYse.Pos Up)
    {
      Yse.Pos f = new Yse.Pos(forward.X, forward.Y, forward.Z);
      Yse.Pos u = new Yse.Pos(Up.X, Up.Y, Up.Z);
      Yse.Yse.Listener().orient(f, u);
    }

    public IYse.Pos Pos()
    {
      Yse.Pos f = Yse.Yse.Listener().pos();
      return new IYse.Pos(f.x, f.y, f.z);
    }

    public void Pos(IYse.Pos p)
    {
      Yse.Pos f = new Yse.Pos(p.X, p.Y, p.Z);
      Yse.Yse.Listener().pos(f);
    }

    public IYse.Pos Upward()
    {
      Yse.Pos f = Yse.Yse.Listener().upward();
      return new IYse.Pos(f.x, f.y, f.z);
    }

    public IYse.Pos Vel()
    {
      Yse.Pos f = Yse.Yse.Listener().vel();
      return new IYse.Pos(f.x, f.y, f.z);
    }
  }
}
