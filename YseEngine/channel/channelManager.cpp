/*
  ==============================================================================

    channelManager.cpp
    Created: 1 Feb 2014 2:43:30pm
    Author:  yvan

  ==============================================================================
*/


#include "../internalHeaders.h"

YSE::CHANNEL::managerObject & YSE::CHANNEL::Manager() {
  static managerObject m;
  return m;
}

YSE::CHANNEL::managerObject::managerObject() 
: mgrSetup( this), 
  mgrDelete(this), 
  outputAngles(nullptr),
  outputChannels(0) 
  {}

YSE::CHANNEL::managerObject::~managerObject() {
  // wait for jobs to finish
  mgrSetup.join();
  mgrDelete.join();

  // remove all objects that are still in memory
  toLoad.clear();
  inUse.clear();
  implementations.clear();
  delete[] outputAngles;
}

void YSE::CHANNEL::managerObject::update() {
  // master channel is not in inUse list
  DEVICE::Manager().getMaster().sync();
  ///////////////////////////////////////////
  // check if there are implementations that need setup
  ///////////////////////////////////////////
  if (!toLoad.empty() && !mgrSetup.isQueued()) {
    // removing cannot be done in a separate thread because we are iterating over this
    // list a during this update fuction
    toLoad.remove_if(implementationObject::canBeRemovedFromLoading);
    INTERNAL::Global().addSlowJob(&mgrSetup);
  }

  if (runDelete && !mgrDelete.isQueued()) {
    INTERNAL::Global().addSlowJob(&mgrDelete);
  }
  runDelete = false;

  ///////////////////////////////////////////
  // check if loaded implementations are ready
  ///////////////////////////////////////////
  {
    for (auto i = toLoad.begin(); i != toLoad.end(); i++) {
      if (i->load()->readyCheck()) {
        implementationObject * ptr = i->load();
        // place ptr in active sound list
        inUse.emplace_front(ptr);
        // add the sound to the channel that is supposed to use
        //ptr->parent->connect(ptr);
        ptr->doThisWhenReady();
      }
    }
  }

  ///////////////////////////////////////////
  // sync implementations
  ///////////////////////////////////////////
  {
    auto previous = inUse.before_begin();
    for (auto i = inUse.begin(); i != inUse.end();) {
      (*i)->sync();
      if ((*i)->getStatus() == OBJECT_RELEASE) {
        implementationObject * ptr = (*i);
        i = inUse.erase_after(previous);
        ptr->setStatus(OBJECT_DELETE);
        runDelete = true;
        continue;
      }
      previous = i;
      ++i;
    }
  }
}


YSE::CHANNEL::implementationObject * YSE::CHANNEL::managerObject::addImplementation(YSE::channel * head) {
  implementations.emplace_front(head);
  return &implementations.front();
}

void YSE::CHANNEL::managerObject::setup(implementationObject * impl) {
  impl->setStatus(OBJECT_CREATED);
  toLoad.emplace_front(impl);
}

Bool YSE::CHANNEL::managerObject::empty() {
  return implementations.empty();
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
  DEVICE::Manager().setMaster(impl);
}

void YSE::CHANNEL::managerObject::setChannelConf(CHANNEL_TYPE type, Int outputs) {
  outputChannels = outputs;
  channelType = type;
}

void YSE::CHANNEL::managerObject::changeChannelConf() {
  delete[] outputAngles;
  outputAngles = new aFlt[outputChannels.load()];
  switch (channelType.load()) {
    case CT_AUTO: setAuto(outputChannels); break;
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

  REVERB::Manager().setOutputChannels(outputChannels);
  
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
  case	6: set51(); break;
  case	7: set61(); break;
  case  8: set71(); break;
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