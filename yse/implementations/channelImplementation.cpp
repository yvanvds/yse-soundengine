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

YSE::INTERNAL::channelImplementation& YSE::INTERNAL::channelImplementation::volume(Flt value) {
  newVolume = value;
  Clamp(newVolume, 0.f, 1.f);
  return (*this);
}

Flt YSE::INTERNAL::channelImplementation::volume() {
  return newVolume;
}

YSE::INTERNAL::channelImplementation::channelImplementation(const String & name) : 
    Thread(name),
    newVolume(1.f), lastVolume(1.f), userChannel(true),
    allowVirtual(true), release(false), parent(NULL),
    link(NULL) {
}

YSE::INTERNAL::channelImplementation::~channelImplementation() {
  exit(); // exit the dsp thread for this channel
  if (link) *link = NULL;
}

void YSE::INTERNAL::channelImplementation::update() {

}

void YSE::INTERNAL::channelImplementation::attachUnderWaterFX() {
  UnderWaterEffect().channel(this);
}

//TODO: add and remove functions are unlikely to be threadsafe!
Bool YSE::INTERNAL::channelImplementation::add(YSE::INTERNAL::channelImplementation * ch) {
  if (ch != this) {
    if (ch->parent != NULL) ch->parent->remove(ch);
    ch->parent = this;
    children.push_front(ch);
    return true;
  }
  else ch->parent = NULL;
  return false;
}

Bool YSE::INTERNAL::channelImplementation::remove(YSE::INTERNAL::channelImplementation *ch) {
  children.remove(ch);
  return true;
}

Bool YSE::INTERNAL::channelImplementation::add(YSE::INTERNAL::soundImplementation * s) {
  if (s->parent != NULL) {
    s->parent->remove(s);
  }
  s->parent = this;
  sounds.push_front(s);
  return true;
}

Bool YSE::INTERNAL::channelImplementation::remove(YSE::INTERNAL::soundImplementation*s) {
  sounds.remove(s);
  return true;
}

YSE::INTERNAL::channelImplementation& YSE::INTERNAL::channelImplementation::set(UInt count) {
  // delete current outputs if there are too much
  out.resize(count);
  while (outConf.size() > count) {
    outConf.pop_back();
  }

  while (outConf.size() < count) {
    outConf.emplace_back();
  }

  for (std::forward_list<channelImplementation*>::iterator i = children.begin(); i != children.end(); ++i) {
    (*i)->set(count);
  }

  return (*this);
}

YSE::INTERNAL::channelImplementation& YSE::INTERNAL::channelImplementation::pos(UInt nr, Flt angle) {
  if (nr >= 0 && nr < outConf.size()) outConf[nr].angle = angle;
  for (std::forward_list<channelImplementation*>::iterator i = children.begin(); i != children.end(); ++i) {
    (*i)->pos(nr, angle);
  }

  return (*this);
}

void YSE::INTERNAL::channelImplementation::clearBuffers() {
  for (UInt i = 0; i < out.size(); ++i) {
    out[i] = 0.0f;
  }
}

void YSE::INTERNAL::channelImplementation::run() {
  for (;;) {
    dsp();
    if (threadShouldExit()) return;
    wait(-1);
  }
}

void YSE::INTERNAL::channelImplementation::exit() {
  signalThreadShouldExit();
  notify();
  waitForThreadToExit(-1);
}

void YSE::INTERNAL::channelImplementation::dsp() {
  // if no sounds or other channels are linked, we skip this channel
  if (children.empty() && sounds.empty()) return;

  // claim dsp critical section to block parent channel later on
  const ScopedLock lock(dspActive);

  // clear channel buffer
  clearBuffers();

  // calculate child channels if there are any
  for (std::forward_list<channelImplementation*>::iterator i = children.begin(); i != children.end(); ++i) {
    if ((*i)->isThreadRunning()) {
      (*i)->notify();
    }
    else {
      (*i)->startThread(10);
    }
  }

  // calculate sounds in this channel
  for (std::forward_list<soundImplementation*>::iterator i = sounds.begin(); i != sounds.end(); ++i) {
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

void YSE::INTERNAL::channelImplementation::adjustVolume() {
  Flt volume = newVolume;
  if (volume != lastVolume) {
    // new value, create a ramp
    Flt step = (volume - lastVolume) / STANDARD_BUFFERSIZE;

    for (UInt i = 0; i < out.size(); ++i) {
      Flt multiplier = lastVolume;
      Flt * ptr = out[i].getBuffer();
      for (UInt j = 0; j < STANDARD_BUFFERSIZE; j++) {
        *ptr++ *= multiplier;
        multiplier += step;
      }
    }
    lastVolume = volume;
  }
  else {
    // same volume, just copy
    for (UInt i = 0; i < out.size(); ++i) {
      out[i] *= volume;
    }
  }
}

void YSE::INTERNAL::channelImplementation::buffersToParent() {
  // wait for dsp thread to finish
  const ScopedLock lock(dspActive);

  // call this recursively on all child channels 
  for (std::forward_list<channelImplementation*>::iterator i = children.begin(); i != children.end(); ++i) {
    (*i)->buffersToParent();
  }
  // if this is the main channel, we're done here
  if (parent == NULL) return;

  // if not the main channel, add output to parent channel
  for (UInt i = 0; i < out.size(); ++i) {
    // parent size is not checked but should be ok because it's adjusted before calling this
    parent->out[i] += out[i];
  }
}

YSE::INTERNAL::channelImplementation& YSE::INTERNAL::channelImplementation::allowVirtualSounds(Bool value) {
  allowVirtual = value;
  for (std::forward_list<channelImplementation*>::iterator i = children.begin(); i != children.end(); ++i) {
    (*i)->allowVirtualSounds(value);
  }
  return (*this);
}

Bool YSE::INTERNAL::channelImplementation::allowVirtualSounds() {
  return allowVirtual;
}
