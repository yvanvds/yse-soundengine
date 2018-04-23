using System;
using System.Collections.Generic;
using System.Text;

namespace IYse
{
  public interface IBufferIO
  {
    bool Active { set; get; }
    bool BufferNameExists(String ID);
    bool BufferExists(byte[] bufer);

    bool AddBuffer(String ID, byte[] buffer, int length);

    bool RemoveBufferByName(String ID);
    bool RemoveBuffer(byte[] buffer);
  }
}
