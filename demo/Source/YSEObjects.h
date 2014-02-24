/*
  ==============================================================================

    YSEObjects.h
    Created: 24 Feb 2014 10:09:58pm
    Author:  yvan

  ==============================================================================
*/

#ifndef YSEOBJECTS_H_INCLUDED
#define YSEOBJECTS_H_INCLUDED
#include "../JuceLibraryCode/JuceHeader.h"
#include "../../YSE/yse.hpp"
#include <forward_list>

class YSEObjects {
public:
  void init();
  void close();

  // sounds for basic tab
  OwnedArray<YSE::sound> basicTab;
  
  // sounds for 3D tab
  OwnedArray<YSE::sound> basic3DTab;

  // sounds for cpuload tab
  // using a forward list here because we're testing for speed
  // (although it might not make much difference at the moment)
  std::forward_list<YSE::sound> cpuTab;


};

YSEObjects & Sound();

#endif  // YSEOBJECTS_H_INCLUDED
