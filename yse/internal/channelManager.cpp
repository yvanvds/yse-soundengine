/*
  ==============================================================================

    channelManager.cpp
    Created: 1 Feb 2014 2:43:30pm
    Author:  yvan

  ==============================================================================
*/

#include "channelManager.h"
#include "../implementations/channelImplementation.h"
#include "../implementations/soundImplementation.h"
#include "../utils/misc.hpp"
#include "../internal/deviceManager.h"
#include "global.h"

juce_ImplementSingleton(YSE::INTERNAL::channelManager)

YSE::INTERNAL::channelManager::channelManager() : numberOfOutputs(0) {}

YSE::INTERNAL::channelManager::~channelManager() {
  clearSingletonInstance();
}

YSE::channel & YSE::INTERNAL::channelManager::mainMix() {
  return _mainMix;
}

YSE::channel & YSE::INTERNAL::channelManager::FX() {
  return _fx;
}

YSE::channel & YSE::INTERNAL::channelManager::music() {
  return _music;
}

YSE::channel & YSE::INTERNAL::channelManager::ambient() {
  return _ambient;
}

YSE::channel & YSE::INTERNAL::channelManager::voice() {
  return _voice;
}

YSE::channel & YSE::INTERNAL::channelManager::gui() {
  return _gui;
}

void YSE::INTERNAL::channelManager::update() {
  std::forward_list<INTERNAL::channelImplementation>::iterator previous = implementations.before_begin();
  for (std::forward_list<INTERNAL::channelImplementation>::iterator i = implementations.begin(); i != implementations.end(); ++i) {
    if (i->release) {

      // move subchannels to parent
      for (std::forward_list<INTERNAL::channelImplementation *>::iterator child = i->children.begin(); child != i->children.end(); ++child) {
        (*child)->parent = i->parent;
        i->parent->children.push_front((*child));
      }

      // move sounds to parent
      for (std::forward_list<soundImplementation *>::iterator sound = i->sounds.begin(); sound != i->sounds.end(); ++sound) {
        (*sound)->parent = i->parent;
        i->parent->sounds.push_front((*sound));
      }

      implementations.erase_after(previous);
      i = previous;
    }
    else {
      i->update();
      previous = i;
    }

  }
}

UInt YSE::INTERNAL::channelManager::getNumberOfOutputs() {
  return numberOfOutputs;
}

YSE::INTERNAL::channelImplementation * YSE::INTERNAL::channelManager::addChannelImplementation(const String & name) {
  implementations.emplace_front(name);
  return &implementations.front();
}

void YSE::INTERNAL::channelManager::removeChannelImplementation(YSE::INTERNAL::channelImplementation * ptr) {
  ptr->release = true;
}

void YSE::INTERNAL::channelManager::changeChannelConf(CHANNEL_TYPE type, Int outputs) {
  numberOfOutputs = outputs;
  switch (type) {
  case CT_AUTO: setAuto(outputs); break;
  case CT_MONO: setMono(); break;
  case CT_STEREO: setStereo(); break;
  case CT_QUAD: setQuad(); break;
  case CT_51: set51(); break;
  case CT_51SIDE: set51Side(); break;
  case CT_61:	set61(); break;
  case CT_71:	set71(); break;
  case CT_CUSTOM: _mainMix.setNumberOfSpeakers(outputs); break;
  }
}

void YSE::INTERNAL::channelManager::setAuto(Int count) {
  switch (count) {
  case	1: setMono(); break;
  case	2: setStereo(); break;
  case	4: setQuad(); break;
  case	5: set51(); break;
  case	6: set61(); break;
  case	7: set71(); break;
  default: setStereo(); break;
  }
}

void YSE::INTERNAL::channelManager::setMono() {
  _mainMix.setNumberOfSpeakers(1).pos(0, 0);
}

void YSE::INTERNAL::channelManager::setStereo() {
  _mainMix.setNumberOfSpeakers(2).pos(0, Pi / 180.0f * -90.0f).pos(1, Pi / 180.0f * 90.0f);
}

void YSE::INTERNAL::channelManager::setQuad() {
  _mainMix.setNumberOfSpeakers(4).pos(0, Pi / 180.0f * -45.0f).pos(1, Pi / 180.0f *  45.0f)
    .pos(2, Pi / 180.0f * 135.0f).pos(3, Pi / 180.0f * 135.0f);
}

void YSE::INTERNAL::channelManager::set51() {
  _mainMix.setNumberOfSpeakers(5).pos(0, Pi / 180.0f *  -45.0f).pos(1, Pi / 180.0f *  45.0f)
    .pos(2, Pi / 180.0f *	   0.0f)
    .pos(3, Pi / 180.0f * -135.0f).pos(4, Pi / 180.0f *	135.0f);
}

void YSE::INTERNAL::channelManager::set51Side() {
  _mainMix.setNumberOfSpeakers(5).pos(0, Pi / 180.0f * -45.0f).pos(1, Pi / 180.0f * 45.0f)
    .pos(2, Pi / 180.0f *		0.0f)
    .pos(3, Pi / 180.0f * -90.0f).pos(4, Pi / 180.0f * 90.0f);
}

void YSE::INTERNAL::channelManager::set61() {
  _mainMix.setNumberOfSpeakers(5).pos(0, Pi / 180.0f * -45.0f).pos(1, Pi / 180.0f * 45.0f)
    .pos(2, Pi / 180.0f *	  0.0f)
    .pos(3, Pi / 180.0f * -90.0f).pos(4, Pi / 180.0f * 90.0f)
    .pos(5, Pi / 180.0f * 180.0f);
}

void YSE::INTERNAL::channelManager::set71() {
  _mainMix.setNumberOfSpeakers(5).pos(0, Pi / 180.0f *  -45.0f).pos(1, Pi / 180.0f *  45.0f)
    .pos(2, Pi / 180.0f *	   0.0f)
    .pos(3, Pi / 180.0f *  -90.0f).pos(4, Pi / 180.0f *	 90.0f)
    .pos(5, Pi / 180.0f * -135.0f).pos(6, Pi / 180.0f * 135.0f);
}