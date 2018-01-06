#pragma once
#include "basePage.h"
class DemoReverb :
  public basePage
{
public:
  DemoReverb();
  ~DemoReverb();

  virtual void ExplainDemo();

private:
  void MoveForward();
  void MoveBack();
  void GlobalReverbOn();
  void GlobalReverbOff();

  YSE::sound snare;
  YSE::reverb bathroom, hall, sewer, custom;
};

