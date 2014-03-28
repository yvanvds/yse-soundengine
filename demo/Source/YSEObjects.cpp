/*
  ==============================================================================

    YSEObjects.cpp
    Created: 24 Feb 2014 10:09:58pm
    Author:  yvan

  ==============================================================================
*/

#include "YSEObjects.h"


valueTreeLink::valueTreeLink() : channel(NULL) {}

void valueTreeLink::set(ValueTree tree) {
  this->tree = tree;
  type = static_cast<ITEM_TYPE>(tree["type"].operator int());
  switch (type) {
  case SOUND: {
                sound = new YSE::sound;
                // find parent channel
                valueTreeLink * parent = Sound().findParent(tree);
                if (parent != NULL) {
                  sound->create(new MemoryInputStream(BinaryData::kick_ogg, BinaryData::kick_oggSize, false), parent->channel, true);
                  sound->play();
                }
                
                break;
  }
  case USERCHANNEL: {
                      userChannel = new YSE::channel;
                      channel = userChannel;
                      break;
  }
    case MAINCHANNEL: channel = &YSE::ChannelMaster(); break;
    case FXCHANNEL: channel = &YSE::ChannelFX(); break;
    case AMBIENTCHANNEL: channel = &YSE::ChannelAmbient(); break;
    case VOICECHANNEL: channel = &YSE::ChannelVoice(); break;
    case GUICHANNEL: channel = &YSE::ChannelGui(); break;
    case MUSICCHANNEL: channel = &YSE::ChannelMusic(); break;
  }
}

void valueTreeLink::move() {
  // find parent
  valueTreeLink * parent = Sound().findParent(tree);
  if (type == SOUND) {
    // we've made sure the parent is a channel while dropping the
    // item on another one
    sound->moveTo(*parent->channel);
  }
  else {
    channel->moveTo(*parent->channel);
  }
}

YSEObjects & Sound() {
  static YSEObjects y;
  return y;
}

void YSEObjects::init() {
  basicTab.add(new YSE::sound)->create(new MemoryInputStream(BinaryData::drone_ogg, BinaryData::drone_oggSize, false), NULL, true).set2D(true);
  basicTab.add(new YSE::sound)->create(new MemoryInputStream(BinaryData::drone_ogg, BinaryData::drone_oggSize, false), NULL, true).set2D(true);
  basicTab.add(new YSE::sound)->create(new MemoryInputStream(BinaryData::drone_ogg, BinaryData::drone_oggSize, false), NULL, true).set2D(true);
}

void YSEObjects::close() {
  basicTab.clear();
  basic3DTab.clear();
  cpuTab.clear();
  treelinks.clear();
}

void YSEObjects::addTree(ValueTree tree) {
  treelinks.emplace_front();
  treelinks.front().set(tree);
}

void YSEObjects::move(ValueTree tree) {
  for (auto i = treelinks.begin(); i != treelinks.end(); i++) {
    if ((*i).tree == tree) {
      (*i).move();
      return;
    }
  }
}

valueTreeLink * YSEObjects::findParent(ValueTree tree) {
  ValueTree parent = tree.getParent();
  for (auto i = treelinks.begin(); i != treelinks.end(); i++) {
    if ((*i).tree == parent) {
      return &(*i);
    }
  }
} 