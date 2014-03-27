/*
  ==============================================================================

    channelManager.cpp
    Created: 1 Feb 2014 2:43:30pm
    Author:  yvan

  ==============================================================================
*/

#include "channelManager.h"
#include "../internal/global.h"
#include "../utils/misc.hpp"

juce_ImplementSingleton(YSE::CHANNEL::managerObject)


YSE::CHANNEL::managerObject::managerObject() : super("channelManager"), outputChannels(0), outputAngles(nullptr) {}

YSE::CHANNEL::managerObject::~managerObject() {
  delete[] outputAngles;
  clearSingletonInstance();
}

YSE::channel & YSE::CHANNEL::managerObject::master() {
  return _master;
}

YSE::channel & YSE::CHANNEL::managerObject::FX() {
  return _fx;
}

YSE::channel & YSE::CHANNEL::managerObject::music() {
  return _music;
}

YSE::channel & YSE::CHANNEL::managerObject::ambient() {
  return _ambient;
}

YSE::channel & YSE::CHANNEL::managerObject::voice() {
  return _voice;
}

YSE::channel & YSE::CHANNEL::managerObject::gui() {
  return _gui;
}

void YSE::CHANNEL::managerObject::update() {
  // master channel is not in inUse list
  INTERNAL::Global.getDeviceManager().getMaster().sync();
  super::update();
}

UInt YSE::CHANNEL::managerObject::getNumberOfOutputs() {
  return outputChannels;
}

Flt YSE::CHANNEL::managerObject::getOutputAngle(UInt nr) {
  if (nr >= outputChannels) {
    return 0.f;
  } else {
    return outputAngles[nr];
  }
}

void YSE::CHANNEL::managerObject::setMaster(CHANNEL::implementationObject * impl) {
  impl->objectStatus = OBJECT_CREATED;
  impl->setup();
  INTERNAL::Global.getDeviceManager().setMaster(impl);
}

void YSE::CHANNEL::managerObject::changeChannelConf(CHANNEL_TYPE type, Int outputs) {
  delete[] outputAngles;
  outputChannels = outputs;
  outputAngles = new aFlt[outputs];
  switch (type) {
  case CT_AUTO: setAuto(outputs); break;
  case CT_MONO: setMono(); break;
  case CT_STEREO: setStereo(); break;
  case CT_QUAD: setQuad(); break;
  case CT_51: set51(); break;
  case CT_51SIDE: set51Side(); break;
  case CT_61:	set61(); break;
  case CT_71:	set71(); break;
  case CT_CUSTOM: break; // we've set number of outputs. CT_CUSTOM expects the positions will be 
                         // set later
  }

  INTERNAL::Global.getReverbManager().setOutputChannels(outputChannels);
  for (auto i = inUse.begin(); i != inUse.end(); i++) {
    (*i)->setup();
  }
}

void YSE::CHANNEL::managerObject::setAuto(Int count) {
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

void YSE::CHANNEL::managerObject::setMono() {
  outputAngles[0] = 0;
}

void YSE::CHANNEL::managerObject::setStereo() {
  outputAngles[0] = Pi / 180.0f * -90.0f;
  outputAngles[1] = Pi / 180.0f *  90.0f;
}

void YSE::CHANNEL::managerObject::setQuad() {
  outputAngles[0] = Pi / 180.0f *  -45.0f;
  outputAngles[1] = Pi / 180.0f *   45.0f;
  outputAngles[2] = Pi / 180.0f * -135.0f;
  outputAngles[3] = Pi / 180.0f *  135.0f;
}

void YSE::CHANNEL::managerObject::set51() {
  outputAngles[0] = Pi / 180.0f *  -45.0f;
  outputAngles[1] = Pi / 180.0f *   45.0f;
  outputAngles[2] = Pi / 180.0f *	   0.0f;
  outputAngles[3] = Pi / 180.0f * -135.0f;
  outputAngles[4] = Pi / 180.0f *	 135.0f;
}

void YSE::CHANNEL::managerObject::set51Side() {
  outputAngles[0] = Pi / 180.0f * -45.0f;
  outputAngles[1] = Pi / 180.0f *  45.0f;
  outputAngles[2] = Pi / 180.0f *		0.0f;
  outputAngles[3] = Pi / 180.0f * -90.0f;
  outputAngles[4] = Pi / 180.0f *  90.0f;
}

void YSE::CHANNEL::managerObject::set61() {
  outputAngles[0] = Pi / 180.0f * -45.0f;
  outputAngles[1] = Pi / 180.0f *  45.0f;
  outputAngles[2] = Pi / 180.0f *	  0.0f;
  outputAngles[3] = Pi / 180.0f * -90.0f;
  outputAngles[4] = Pi / 180.0f *  90.0f;
  outputAngles[5] = Pi / 180.0f * 180.0f;
}

void YSE::CHANNEL::managerObject::set71() {
  outputAngles[0] = Pi / 180.0f *  -45.0f;
  outputAngles[1] = Pi / 180.0f *   45.0f;
  outputAngles[2] = Pi / 180.0f *	   0.0f;
  outputAngles[3] = Pi / 180.0f *  -90.0f;
  outputAngles[4] = Pi / 180.0f *	  90.0f;
  outputAngles[5] = Pi / 180.0f * -135.0f;
  outputAngles[6] = Pi / 180.0f *  135.0f;
}