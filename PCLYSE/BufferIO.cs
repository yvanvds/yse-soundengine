using System;
using System.Collections.Generic;
using System.Text;

namespace YSE
{
  public class BufferIO : Yse.BufferIO, IYse.IBufferIO
  {
    public BufferIO() : base(true) {}

    public bool Active { get => GetActive(); set => SetActive(value); }
  }
}
