/*
  ==============================================================================

    yseTimerThread.h
    Created: 12 Feb 2014 6:02:30pm
    Author:  yvan

  ==============================================================================
*/

#ifndef YSETIMERTHREAD_H_INCLUDED
#define YSETIMERTHREAD_H_INCLUDED

#include "../JuceLibraryCode/JuceHeader.h"

class yseTimer : public Timer {
public:
  yseTimer() : cpuLoad(NULL){}

  void timerCallback();
  Label * cpuLoad;
};

yseTimer & YseTimer();


#endif  // YSETIMERTHREAD_H_INCLUDED
