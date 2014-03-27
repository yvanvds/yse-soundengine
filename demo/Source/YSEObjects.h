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
#include "../../yse/yse.hpp"
#include "parts/ChannelTreeItem.h"
#include <forward_list>

// for the movingChannels demo
class valueTreeLink {
public:
  valueTreeLink();
  void set(ValueTree tree);
  void move();
  ValueTree tree;
  ITEM_TYPE type;
  ScopedPointer<YSE::sound> sound;
  ScopedPointer<YSE::channel> userChannel;
  YSE::channel * channel;
};

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

  // for movingChannels tab
  void addTree(ValueTree tree);
  valueTreeLink * findParent(ValueTree tree);
  void move(ValueTree tree);
  std::forward_list<valueTreeLink> treelinks;

};

YSEObjects & Sound();

#endif  // YSEOBJECTS_H_INCLUDED
