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

ThreadPoolJob::JobStatus YSE::INTERNAL::channelSetupJob::runJob() {
  for (auto i = Global.getChannelManager().toCreate.begin(); i != Global.getChannelManager().toCreate.end(); ++i) {
    i->load()->setup();
  }
  return jobHasFinished;
}

ThreadPoolJob::JobStatus YSE::INTERNAL::channelDeleteJob::runJob() {
  //Global.getChannelManager().implementations.remove_if(channelImplementation::canBeDeleted);
  return jobHasFinished;
}


YSE::INTERNAL::channelManager::channelManager() : numberOfOutputs(0) {}

YSE::INTERNAL::channelManager::~channelManager() {
  Global.waitForSlowJob(&channelSetup);
  Global.waitForSlowJob(&channelDelete);
  toCreate.clear();
  inUse.clear();
  implementations.clear();
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
  ///////////////////////////////////////////
  // check if there are channelimplementations that need setup
  ///////////////////////////////////////////
  if (!toCreate.empty() && !Global.containsSlowJob(&channelSetup)) {
    Global.addSlowJob(&channelSetup);
  }

  if (runDelete && !Global.containsSlowJob(&channelDelete)) {
    Global.addSlowJob(&channelDelete);
  }
  runDelete = false;

  ///////////////////////////////////////////
  // check if loading channelimplementations are ready
  ///////////////////////////////////////////
  {
    for (auto i = toCreate.begin(); i != toCreate.end(); i++) {
      if (i->load()->readyCheck()) {
        channelImplementation * ptr = i->load();
        // place ptr in active sound list
        inUse.emplace_front(ptr);
        // add the channel to parent
        ptr->parent->add(ptr);
      }
    }
  }

  ///////////////////////////////////////////
  // sync channel implementations
  ///////////////////////////////////////////
  {
    auto previous = inUse.before_begin();
    for (auto i = inUse.begin(); i != inUse.end();) {
      (*i)->sync();

      if ((*i)->objectStatus == CIS_RELEASE) {
        channelImplementation * ptr = (*i);
        // move subchannels to parent
        for (auto child = ptr->children.begin(); child != ptr->children.end(); ++child) {
          (*child)->parent = ptr->parent;
          ptr->parent->children.push_front((*child));
        }

        // move sounds to parent
        for (auto sound = ptr->sounds.begin(); sound != ptr->sounds.end(); ++sound) {
          (*sound)->parent = ptr->parent;
          ptr->parent->sounds.push_front((*sound));
        }

        i = inUse.erase_after(previous);
        ptr->objectStatus = CIS_DELETE;
        runDelete = true;
        continue;
      }
      previous = i;
      ++i;
    }
  }
}

UInt YSE::INTERNAL::channelManager::getNumberOfOutputs() {
  return numberOfOutputs;
}

YSE::INTERNAL::channelImplementation * YSE::INTERNAL::channelManager::add(const String & name, channel * head) {
  implementations.emplace_front(name, head);
  return &implementations.front();
}

void YSE::INTERNAL::channelManager::setup(channelImplementation * impl) {
  impl->objectStatus = CIS_CREATED;
  toCreate.emplace_front(impl);
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
  case CT_CUSTOM: Global.getDeviceManager().getMaster().set(outputs); break;
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
  Global.getDeviceManager().getMaster().set(1).pos(0, 0);
}

void YSE::INTERNAL::channelManager::setStereo() {
  Global.getDeviceManager().getMaster().set(2).pos(0, Pi / 180.0f * -90.0f).pos(1, Pi / 180.0f * 90.0f);
}

void YSE::INTERNAL::channelManager::setQuad() {
  Global.getDeviceManager().getMaster().set(4).pos(0, Pi / 180.0f * -45.0f).pos(1, Pi / 180.0f *  45.0f)
    .pos(2, Pi / 180.0f * 135.0f).pos(3, Pi / 180.0f * 135.0f);
}

void YSE::INTERNAL::channelManager::set51() {
  Global.getDeviceManager().getMaster().set(5).pos(0, Pi / 180.0f *  -45.0f).pos(1, Pi / 180.0f *  45.0f)
    .pos(2, Pi / 180.0f *	   0.0f)
    .pos(3, Pi / 180.0f * -135.0f).pos(4, Pi / 180.0f *	135.0f);
}

void YSE::INTERNAL::channelManager::set51Side() {
  Global.getDeviceManager().getMaster().set(5).pos(0, Pi / 180.0f * -45.0f).pos(1, Pi / 180.0f * 45.0f)
    .pos(2, Pi / 180.0f *		0.0f)
    .pos(3, Pi / 180.0f * -90.0f).pos(4, Pi / 180.0f * 90.0f);
}

void YSE::INTERNAL::channelManager::set61() {
  Global.getDeviceManager().getMaster().set(5).pos(0, Pi / 180.0f * -45.0f).pos(1, Pi / 180.0f * 45.0f)
    .pos(2, Pi / 180.0f *	  0.0f)
    .pos(3, Pi / 180.0f * -90.0f).pos(4, Pi / 180.0f * 90.0f)
    .pos(5, Pi / 180.0f * 180.0f);
}

void YSE::INTERNAL::channelManager::set71() {
  Global.getDeviceManager().getMaster().set(5).pos(0, Pi / 180.0f *  -45.0f).pos(1, Pi / 180.0f *  45.0f)
    .pos(2, Pi / 180.0f *	   0.0f)
    .pos(3, Pi / 180.0f *  -90.0f).pos(4, Pi / 180.0f *	 90.0f)
    .pos(5, Pi / 180.0f * -135.0f).pos(6, Pi / 180.0f * 135.0f);
}