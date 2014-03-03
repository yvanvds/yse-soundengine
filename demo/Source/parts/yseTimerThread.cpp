/*
  ==============================================================================

    yseTimerThread.cpp
    Created: 12 Feb 2014 6:02:30pm
    Author:  yvan

  ==============================================================================
*/

#include "yseTimerThread.h"
#include "../../yse/yse.hpp"

yseTimer & YseTimer() {
  static yseTimer y;
  return y;
}

void yseTimer::timerCallback() {
  YSE::System().update();
  if (cpuLoad != NULL) {
    cpuLoad->setText(String(YSE::System().cpuLoad()), sendNotification);
  }
}