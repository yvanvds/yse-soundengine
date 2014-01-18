#pragma once
#include <string>
#include <vector>

namespace YSE {
  struct systemDevice;
  struct device;

  class API audioDevice {
  public:
    UInt                ID        ();
    Bool                defaultIn ();
    Bool                defaultOut();
    const char *        host      ();
    const char *        name      ();
    Int                 inputs    ();
    Int                 outputs   ();

  private:
    device * pimpl;
    friend struct systemDevice;
  };
}
