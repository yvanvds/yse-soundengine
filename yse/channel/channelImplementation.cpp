/*
  ==============================================================================

    channelImplementation.cpp
    Created: 30 Jan 2014 4:21:26pm
    Author:  yvan

  ==============================================================================
*/

#include "channelImplementation.h"
#include "../internal/global.h"
#include "../internal/underWaterEffect.h"
#include "../reverb/reverbManager.h"

YSE::CHANNEL::implementationObject::implementationObject(interfaceObject * head) :
implementationTemplate<channelSubSystem>(head),
ThreadPoolJob(head->getName()),
newVolume(1.f), lastVolume(1.f), userChannel(true),
allowVirtual(true), parent(nullptr)
{
}

void YSE::CHANNEL::implementationObject::exit() {
  // exit the dsp thread for this channel
  INTERNAL::Global.waitForFastJob(this);

  if (INTERNAL::Global.isActive()) {
    parent->disconnect(this);
    childrenToParent();
  }
}

Bool YSE::CHANNEL::implementationObject::connect(CHANNEL::implementationObject * ch) {
  if (ch != this) {
    if (ch->parent != nullptr) ch->parent->disconnect(ch);
    ch->parent = this;
    children.push_front(ch);
    return true;
  }
  else ch->parent = nullptr;
  return false;
}

Bool YSE::CHANNEL::implementationObject::disconnect(YSE::CHANNEL::implementationObject *ch) {
  children.remove(ch);
  return true;
}

Bool YSE::CHANNEL::implementationObject::connect(YSE::SOUND::implementationObject * s) {
  if (s->parent != nullptr) {
    s->parent->disconnect(s);
  }
  s->parent = this;
  sounds.push_front(s);
  return true;
}

Bool YSE::CHANNEL::implementationObject::disconnect(YSE::SOUND::implementationObject * s) {
  sounds.remove(s);
  return true;
}


ThreadPoolJob::JobStatus YSE::CHANNEL::implementationObject::runJob() {
  dsp();
  return jobHasFinished;
}


void YSE::CHANNEL::implementationObject::dsp() {
  // if no sounds or other channels are linked, we skip this channel
  if (children.empty() && sounds.empty()) return;

  // clear channel buffer
  clearBuffers();

  // calculate child channels if there are any
  for (auto i = children.begin(); i != children.end(); ++i) {
    INTERNAL::Global.addFastJob(*i);
  }

  // calculate sounds in this channel
  for (auto i = sounds.begin(); i != sounds.end(); ++i) {
    if ((*i)->dsp()) {
      (*i)->toChannels();
    }
  }

  adjustVolume();

  INTERNAL::Global.getReverbManager().process(this);

  if (INTERNAL::UnderWaterEffect().channel() == this) {
    INTERNAL::UnderWaterEffect().apply(out);
  }
}

void YSE::CHANNEL::implementationObject::buffersToParent() {
  INTERNAL::Global.waitForFastJob(this);

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

void YSE::CHANNEL::implementationObject::attachUnderWaterFX() {
  INTERNAL::UnderWaterEffect().channel(this);
}

/////////////////////////////////////////////////////
// private functions
/////////////////////////////////////////////////////

void YSE::CHANNEL::implementationObject::implementationSetup() {
  out.resize(INTERNAL::Global.getChannelManager().getNumberOfOutputs());
  outConf.resize(INTERNAL::Global.getChannelManager().getNumberOfOutputs());
  for (UInt i = 0; i < INTERNAL::Global.getChannelManager().getNumberOfOutputs(); i++) {
    outConf[i].angle = INTERNAL::Global.getChannelManager().getOutputAngle(i);
  }
}

Bool YSE::CHANNEL::implementationObject::implementationReadyCheck() {
  return outConf.size() == INTERNAL::Global.getChannelManager().getNumberOfOutputs();
}

void YSE::CHANNEL::implementationObject::doThisWhenReady() {
  parent->connect(this);
}

void YSE::CHANNEL::implementationObject::parseMessage(const messageObject & message) {
  switch (message.ID) {
    case ATTACH_REVERB: 
      INTERNAL::Global.getReverbManager().attachToChannel(this); 
      break;
    case MOVE:
    {
      interfaceObject * ptr = (interfaceObject*)message.ptrValue;
      if (ptr != nullptr) {
        ptr->getImplementation()->connect(this);
      }
      break;
    }
    case VIRTUAL: 
      allowVirtual = message.boolValue; 
      break;
    case VOLUME: 
      newVolume = message.floatValue; 
      break;
    
  }
}



void YSE::CHANNEL::implementationObject::childrenToParent() {
  // don't do this if there is no parent channel
  if (parent == nullptr) return;

  for (auto i = children.begin(); i != children.end(); ++i) {
    parent->connect(*i);
  }

  for (auto i = sounds.begin(); i != sounds.end(); ++i) {
    parent->connect(*i);
  }
}

void YSE::CHANNEL::implementationObject::clearBuffers() {
  for (UInt i = 0; i < out.size(); ++i) {
    out[i] = 0.0f;
  }
}

void YSE::CHANNEL::implementationObject::adjustVolume() {
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

