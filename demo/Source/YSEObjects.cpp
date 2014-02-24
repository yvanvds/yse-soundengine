/*
  ==============================================================================

    YSEObjects.cpp
    Created: 24 Feb 2014 10:09:58pm
    Author:  yvan

  ==============================================================================
*/

#include "YSEObjects.h"

YSEObjects & Sound() {
  static YSEObjects y;
  return y;
}

void YSEObjects::init() {
  basicTab.add(new YSE::sound)->create("drone.ogg", NULL, true).set2D(true);
  basicTab.add(new YSE::sound)->create("drone.ogg", NULL, true).set2D(true);
  basicTab.add(new YSE::sound)->create("drone.ogg", NULL, true).set2D(true);
}

void YSEObjects::close() {
  basicTab.clear();
  basic3DTab.clear();
  cpuTab.clear();
}