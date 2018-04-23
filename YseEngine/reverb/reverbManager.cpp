/*
  ==============================================================================

    reverbManager.cpp
    Created: 1 Feb 2014 7:02:37pm
    Author:  yvan

  ==============================================================================
*/

#include "../internalHeaders.h"


YSE::REVERB::managerObject & YSE::REVERB::Manager() {
  static managerObject m;
  return m;
}

YSE::REVERB::managerObject::managerObject() 
  : globalReverb(true), calculatedValues(true), mgrDelete(this) {
  reverbDSPObject.channels(CHANNEL::Manager().getNumberOfOutputs());
}

YSE::REVERB::managerObject::~managerObject() {
  mgrDelete.join();

  // remove all objects that are still in memory
  toLoad.clear();
  inUse.clear();
  implementations.clear();
}

void YSE::REVERB::managerObject::create() {
  globalReverb.create();
  calculatedValues.create();
}

YSE::REVERB::implementationObject * YSE::REVERB::managerObject::addImplementation(YSE::reverb * head) {
  implementations.emplace_front(head);
  return &implementations.front();
}

void YSE::REVERB::managerObject::setup(YSE::REVERB::implementationObject* impl) {
  // reverb object do not need any setup, but we cannot place them
  // in the inUse list because it won't be thread safe
  impl->setStatus(OBJECT_SETUP);
  toLoad.emplace_front(impl);
}

void YSE::REVERB::managerObject::setOutputChannels(Int value) {
  reverbDSPObject.channels(value);
}

YSE::reverb & YSE::REVERB::managerObject::getGlobalReverb() {
  return globalReverb;
}

Bool YSE::REVERB::managerObject::empty() {
  return implementations.empty();
}

void YSE::REVERB::managerObject::update() {
  toLoad.remove_if(implementationObject::canBeRemovedFromLoading);

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
        inUse.emplace_front(ptr);
      }
    }
  }

  ///////////////////////////////////////////
  // sync and update implementations
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

  Int reverbsActive = 0;
  calculatedValues.setPreset(REVERB_OFF);
  calculatedValues.setActive(false);
  calculatedValues.setDryWetBalance(0.f, 0.f); // this is ok here, because this reverb is only used for calculating the real one

  ///////////////////////////////////////
  // find local reverbs within distance
  ///////////////////////////////////////
  for (auto i = inUse.begin(); i != inUse.end(); i++) {
    if (!(*i)->active) continue;
    if (Dist((*i)->position, INTERNAL::ListenerImpl().getPos()) <= (*i)->size) {
      // add this reverb
      calculatedValues.roomsize += (*i)->roomsize;
      calculatedValues.damp += (*i)->damp;
      calculatedValues.wet += (*i)->wet;
      calculatedValues.dry += (*i)->dry;
      calculatedValues.modFrequency += (*i)->modFrequency;
      calculatedValues.modWidth += (*i)->modWidth;
      for (Int j = 0; j < 4; j++) {
        calculatedValues.earlyPtr[j] += (*i)->earlyPtr[j];
        calculatedValues.earlyGain[j] += (*i)->earlyGain[j];
      }
      reverbsActive++;
    }
  }

  // if exactly one reverb within distance has been found, totalAdjust will be 1
  // in this case we can just use this reverb. When more reverbs have been found
  // we need to average the result
  if (reverbsActive > 1) {
    calculatedValues.roomsize /= reverbsActive;
    calculatedValues.damp /= reverbsActive;
    calculatedValues.wet /= reverbsActive;
    calculatedValues.dry /= reverbsActive;
    calculatedValues.modFrequency /= reverbsActive;
    calculatedValues.modWidth /= reverbsActive;

    for (Int j = 0; j < 4; j++) {
      calculatedValues.earlyPtr[j] /= reverbsActive;
      calculatedValues.earlyGain[j] /= reverbsActive;
    }
  }
  else if (reverbsActive == 0) {
    // no reverbs are within distance. Check for rolloff's
    Flt partial = 0;
    for (auto i = inUse.begin(); i != inUse.end(); ++i) {
      if (!(*i)->active) continue;
      if (Dist((*i)->position, INTERNAL::ListenerImpl().getPos()) <= (*i)->size + (*i)->rolloff) {
        // add partial reverb
        Flt adjust = Dist((*i)->position, INTERNAL::ListenerImpl().getPos()) - (*i)->size;
        adjust = 1 - adjust / (*i)->rolloff;
        calculatedValues.roomsize += (*i)->roomsize * adjust;
        calculatedValues.damp += (*i)->damp * adjust;
        calculatedValues.wet += (*i)->wet * adjust;
        calculatedValues.dry += (*i)->dry * adjust;
        calculatedValues.modFrequency += (*i)->modFrequency * adjust;
        calculatedValues.modWidth += (*i)->modWidth * adjust;
        for (Int j = 0; j < 4; j++) {
          calculatedValues.earlyPtr[j] = static_cast<Int>(calculatedValues.earlyPtr[j] + (*i)->earlyPtr[j] * adjust);
          calculatedValues.earlyGain[j] += (*i)->earlyGain[j] * adjust;
        }
        partial += adjust;
      }
    }

    // if sum of partial reverbs > 1 we have to scale down
    if (partial > 1) {
      calculatedValues.roomsize /= partial;
      calculatedValues.damp /= partial;
      calculatedValues.wet /= partial;
      calculatedValues.dry /= partial;
      calculatedValues.modFrequency /= partial;
      calculatedValues.modWidth /= partial;

      for (Int j = 0; j < 4; j++) {
        calculatedValues.earlyPtr[j] = static_cast<Int>(calculatedValues.earlyPtr[j] / partial);
        calculatedValues.earlyGain[j] /= partial;
      }
    }

    // if partial == 0, we can just use the global reverb object
    else if (partial == 0) {
      calculatedValues = globalReverb;
      return; // important because active could be overwritten at the end of this function
    }

    // if sum of partial reverbs < 1 we have to add (part of) the global reverb
    else if (partial < 1) {
      if (globalReverb.getActive()) {
        calculatedValues.roomsize += globalReverb.roomsize * (1 - partial);
        calculatedValues.damp += globalReverb.damp * (1 - partial);
        calculatedValues.wet += globalReverb.wet * (1 - partial);
        calculatedValues.dry += globalReverb.dry * (1 - partial);
        calculatedValues.modFrequency += globalReverb.modFrequency * (1 - partial);
        calculatedValues.modWidth += globalReverb.modWidth * (1 - partial);
        for (Int j = 0; j < 4; j++) {
          calculatedValues.earlyGain[j] += globalReverb.earlyGain[j] * (1 - partial);
          calculatedValues.earlyPtr[j] = static_cast<Int>(calculatedValues.earlyPtr[j] + globalReverb.earlyGain[j] * (1 - partial));
        }
      }
    }
  }

  // end of calculations, disable reverb if no wet signal
  if (calculatedValues.wet < 0.001) {
    calculatedValues.active = false;
  }
  else {
    calculatedValues.active = true;
  }
}



void YSE::REVERB::managerObject::attachToChannel(YSE::CHANNEL::implementationObject * ptr) {
  reverbChannel = ptr;
}

void YSE::REVERB::managerObject::process(YSE::CHANNEL::implementationObject * ptr) {
  if (ptr != reverbChannel) return;
  if (!calculatedValues.active) return;
  reverbDSPObject.set(calculatedValues);

  // the actual reverb processing
  reverbDSPObject.process(ptr->out);
}