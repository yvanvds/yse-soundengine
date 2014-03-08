/*
  ==============================================================================

    channelImplementation.cpp
    Created: 30 Jan 2014 4:21:26pm
    Author:  yvan

  ==============================================================================
*/

#include "channelImplementation.h"
#include "soundImplementation.h"
#include "../utils/misc.hpp"
#include "../internal/global.h"
#include "../internal/underWaterEffect.h"
#include "../internal/reverbManager.h"
#include "../internal/channelManager.h"
#include "../internal/deviceManager.h"
#include "../utils/lfQueue.hpp"

YSE::INTERNAL::channelImplementation::channelImplementation(const String & name, channel * head) :
ThreadPoolJob(name),
newVolume(1.f), lastVolume(1.f), userChannel(true),
allowVirtual(true), objectStatus(CIS_CONSTRUCTED), parent(nullptr),
head(head)
{
}

YSE::INTERNAL::channelImplementation::~channelImplementation() {
  exit(); // exit the dsp thread for this channel

  if (Global.isActive()) {
    parent->disconnect(this);
    childrenToParent();
  }
}

Bool YSE::INTERNAL::channelImplementation::connect(YSE::INTERNAL::channelImplementation * ch) {
  if (ch != this) {
    if (ch->parent != nullptr) ch->parent->disconnect(ch);
    ch->parent = this;
    children.push_front(ch);
    return true;
  }
  else ch->parent = nullptr;
  return false;
}

Bool YSE::INTERNAL::channelImplementation::disconnect(YSE::INTERNAL::channelImplementation *ch) {
  children.remove(ch);
  return true;
}

Bool YSE::INTERNAL::channelImplementation::connect(YSE::INTERNAL::soundImplementation * s) {
  if (s->parent != nullptr) {
    s->parent->disconnect(s);
  }
  s->parent = this;
  sounds.push_front(s);
  return true;
}

Bool YSE::INTERNAL::channelImplementation::disconnect(YSE::INTERNAL::soundImplementation*s) {
  sounds.remove(s);
  return true;
}


ThreadPoolJob::JobStatus YSE::INTERNAL::channelImplementation::runJob() {
  dsp();
  return jobHasFinished;
}


void YSE::INTERNAL::channelImplementation::dsp() {
  // if no sounds or other channels are linked, we skip this channel
  if (children.empty() && sounds.empty()) return;

  // clear channel buffer
  clearBuffers();

  // calculate child channels if there are any
  for (auto i = children.begin(); i != children.end(); ++i) {
    Global.addFastJob(*i);
  }

  // calculate sounds in this channel
  for (auto i = sounds.begin(); i != sounds.end(); ++i) {
    if ((*i)->dsp()) {
      (*i)->toChannels();
    }
  }

  adjustVolume();

  Global.getReverbManager().process(this);

  if (UnderWaterEffect().channel() == this) {
    UnderWaterEffect().apply(out);
  }
}

void YSE::INTERNAL::channelImplementation::buffersToParent() {
  Global.waitForFastJob(this);

  // call this recursively on all child channels 
  for (auto i = children.begin(); i != children.end(); ++i) {
    (*i)->buffersToParent();
  }


  // if this is the main channel, we're done here
  if (parent == nullptr) return;
  if (children.empty() && sounds.empty()) return;

  // if not the main channel, add output to parent channel
  for (UInt i = 0; i < out.size(); ++i) {
    // parent size is not checked but should be ok because it's adjusted before calling this
    parent->out[i] += out[i];
  }
}

void YSE::INTERNAL::channelImplementation::attachUnderWaterFX() {
  UnderWaterEffect().channel(this);
}

/////////////////////////////////////////////////////
// private functions
/////////////////////////////////////////////////////

void YSE::INTERNAL::channelImplementation::setup() {
  if (objectStatus >= CIS_CREATED) {
    out.resize(INTERNAL::Global.getChannelManager().getNumberOfOutputs());
    outConf.resize(INTERNAL::Global.getChannelManager().getNumberOfOutputs());
    for (UInt i = 0; i < INTERNAL::Global.getChannelManager().getNumberOfOutputs(); i++) {
      outConf[i].angle = INTERNAL::Global.getChannelManager().getOutputAngle(i);
    }
    objectStatus = CIS_SETUP;
  }
}

Bool YSE::INTERNAL::channelImplementation::readyCheck() {
  if (objectStatus == CIS_SETUP) {
    // make sure the number of is not changed since setup
    if (outConf.size() == Global.getChannelManager().getNumberOfOutputs()) {
      objectStatus = CIS_READY;
      return true;
    }
    else {
      // setup has changed, return to loading stage
      objectStatus = CIS_CREATED;
    }
  }
  return false;
}

void YSE::INTERNAL::channelImplementation::sync() {
  // remove if interface is gone
  if (head == nullptr) {
    objectStatus = CIS_RELEASE;
    return;
  }

  channelMessage temp;
  while (head->messages.try_pop(temp)) {
    switch (temp.message) {
      case CM_ATTACH_REVERB: 
        Global.getReverbManager().attachToChannel(this); 
        break;
      case CM_MOVE: 
        ((channel*)temp.ptrValue)->pimpl->connect(this); 
        break;
      case CM_VIRTUAL: 
        allowVirtual = temp.boolValue; 
        break;
      case CM_VOLUME: 
        newVolume = temp.floatValue; 
        break;
    }
  }
}

void YSE::INTERNAL::channelImplementation::exit() {
  Global.waitForFastJob(this);
}

void YSE::INTERNAL::channelImplementation::childrenToParent() {
  // don't do this if there is no parent channel
  if (parent == nullptr) return;

  for (auto i = children.begin(); i != children.end(); ++i) {
    parent->connect(*i);
  }

  for (auto i = sounds.begin(); i != sounds.end(); ++i) {
    parent->connect(*i);
  }
}

void YSE::INTERNAL::channelImplementation::clearBuffers() {
  for (UInt i = 0; i < out.size(); ++i) {
    out[i] = 0.0f;
  }
}

void YSE::INTERNAL::channelImplementation::adjustVolume() {
  if (newVolume != lastVolume) {
    // new value, create a ramp
    Flt step = (newVolume - lastVolume) / STANDARD_BUFFERSIZE;

    for (UInt i = 0; i < out.size(); ++i) {
      Flt multiplier = lastVolume;
      Flt * ptr = out[i].getBuffer();
      for (UInt j = 0; j < STANDARD_BUFFERSIZE; j++) {
        *ptr++ *= multiplier;
        multiplier += step;
      }
    }
    lastVolume = newVolume;
  }
  else {
    // same volume, just copy
    for (UInt i = 0; i < out.size(); ++i) {
      out[i] *= newVolume;
    }
  }
}

