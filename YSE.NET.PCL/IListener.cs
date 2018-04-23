using System;
using System.Collections.Generic;
using System.Text;

namespace IYse
{
  

  public interface IListener
  {
    Pos Pos();
    Pos Vel();
    Pos Forward();
    Pos Upward();

    void Pos(Pos p);
    void Orient(Pos forward);
    void Orient(Pos forward, Pos Up);
  }
}
