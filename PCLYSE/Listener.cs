using System;
using System.Collections.Generic;
using System.Text;
using YSE;

namespace YSENET
{
  class Listener : YSE.IListener
  {
    public Pos Forward()
    {
      Yse.Pos f = Yse.Yse.Listener().forward();
      return new YSE.Pos(f.x, f.y, f.z);
    }

    public void Orient(Pos forward)
    {
      Yse.Pos f = new Yse.Pos(forward.X, forward.Y, forward.Z);
      Yse.Yse.Listener().orient(f);
    }

    public void Orient(Pos forward, Pos Up)
    {
      Yse.Pos f = new Yse.Pos(forward.X, forward.Y, forward.Z);
      Yse.Pos u = new Yse.Pos(Up.X, Up.Y, Up.Z);
      Yse.Yse.Listener().orient(f, u);
    }

    public Pos Pos()
    {
      Yse.Pos f = Yse.Yse.Listener().pos();
      return new YSE.Pos(f.x, f.y, f.z);
    }

    public void Pos(Pos p)
    {
      Yse.Pos f = new Yse.Pos(p.X, p.Y, p.Z);
      Yse.Yse.Listener().pos(f);
    }

    public Pos Upward()
    {
      Yse.Pos f = Yse.Yse.Listener().upward();
      return new YSE.Pos(f.x, f.y, f.z);
    }

    public Pos Vel()
    {
      Yse.Pos f = Yse.Yse.Listener().vel();
      return new YSE.Pos(f.x, f.y, f.z);
    }
  }
}
