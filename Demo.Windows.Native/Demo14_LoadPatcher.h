
#pragma once

#include "basePage.h"
#include "../YseEngine/yse.hpp"

class DemoLoadPatcher : public basePage {
public:
  DemoLoadPatcher();

  void LoadPatch1();

private:
  YSE::sound sound;
  YSE::patcher patcher;
};
