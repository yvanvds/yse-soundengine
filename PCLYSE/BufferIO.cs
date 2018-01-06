using System;
using System.Collections.Generic;
using System.Text;

namespace YSENET
{
  public class BufferIO : Yse.BufferIO, YSE.IBufferIO
  {
    public BufferIO() : base(true) {}

    public bool Active { get => GetActive(); set => SetActive(value); }
  }
}
