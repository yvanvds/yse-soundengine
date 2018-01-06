#pragma once
#include "basePage.h"
class DemoFilePosition :
  public basePage
{
public:
  DemoFilePosition();
  ~DemoFilePosition();

  virtual void ExplainDemo();

private: 
  void Zero();
  void One();
  void Two();
  void Three();
  void Four();
  void Five();
  void Six();
  void Seven();
  void Eight();
  void Nine();

  YSE::sound sound;
};

