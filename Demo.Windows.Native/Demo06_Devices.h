#pragma once
#include "basePage.h"


class DemoDevices :
  public basePage
{
public:
  DemoDevices();


  virtual void ExplainDemo();

private:
  void PickDevice();

  YSE::sound drone;
};

