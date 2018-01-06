#pragma once
#include "basePage.h"

class DemoOcclusion : public basePage
{
public:
  DemoOcclusion();
  ~DemoOcclusion();

  virtual void ExplainDemo();

private:
  void OcclusionInc();
  void OcclusionDec();

  YSE::sound sound;
};

