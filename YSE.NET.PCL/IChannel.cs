using System;
using System.Collections.Generic;
using System.Text;

namespace IYse
{
  public interface IChannel
  {
    void Create(String name, IChannel parent);

    float Volume { set; get; }

    void MoveTo(IChannel parent);

    void AttachReverb();
    bool Virtual { set; get; }
    bool Valid { get; }

    String Name { get; }

    bool IsPremade();

    void Dispose();
  }
}
