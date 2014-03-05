/*
  ==============================================================================

    reverbManager.cpp
    Created: 1 Feb 2014 7:02:37pm
    Author:  yvan

  ==============================================================================
*/

#include "reverbManager.h"
#include "../implementations/channelImplementation.h"
#include "global.h"
#include "../implementations/listenerImplementation.h"
#include "../reverb.hpp"

juce_ImplementSingleton(YSE::INTERNAL::reverbManager)

YSE::INTERNAL::reverbManager::reverbManager() : active(false), globalReverb(true), calculatedValues(true), dspValues(true) {
  reverbObject = reverbDSP::getInstance();

}

YSE::INTERNAL::reverbManager::~reverbManager() {
  // this is important because we don't want reverbs to 
  // unregister themselves when the manager is gone.
  for (auto i = reverbs.begin(); i != reverbs.end(); i++) {
    (*i)->connectedToManager = false;
  }
  reverbDSP::deleteInstance();
  clearSingletonInstance();
}

YSE::reverb & YSE::INTERNAL::reverbManager::getGlobalReverb() {
  return globalReverb;
}

void YSE::INTERNAL::reverbManager::add(reverb * r) {
  reverbs.emplace_front(r);
  r->connectedToManager = true;
}

void YSE::INTERNAL::reverbManager::remove(reverb * r) {
  auto previous = reverbs.before_begin();
  for (auto i = reverbs.begin(); i != reverbs.end();) {
    if ((*i) == r) {
      i = reverbs.erase_after(previous);
      r->connectedToManager = false;
      return;
    }
    else {
      previous = i;
      ++i;
    }
  }
}
 
void YSE::INTERNAL::reverbManager::update() {
  Int totalAdjust = 0;
  calculatedValues.setPreset(REVERB_OFF);
  calculatedValues.setActive(false);
  calculatedValues.setDryWetBalance(0.f, 0.f); // this is ok here, because this reverb is only used for calculating the real one

  // find reverbs within distance first
  for (auto i = reverbs.begin(); i != reverbs.end(); i++) {
    if ((*i)->global) continue;
    if (Dist((*i)->position, Global.getListener().newPos) <= (*i)->size) {
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
      totalAdjust++;
    }
  }

  // if exactly one reverb within distance has been found, totalAdjust will be 1
  // in this case we can just use this reverb. When more reverbs have been found
  // we need to average the result
  if (totalAdjust > 1) {
    calculatedValues.roomsize /= totalAdjust;
    calculatedValues.damp /= totalAdjust;
    calculatedValues.wet /= totalAdjust;
    calculatedValues.dry /= totalAdjust;
    calculatedValues.modFrequency /= totalAdjust;
    calculatedValues.modWidth /= totalAdjust;

    for (Int j = 0; j < 4; j++) {
      calculatedValues.earlyPtr[j] /= totalAdjust;
      calculatedValues.earlyGain[j] /= totalAdjust;
    }
  }
  else if (totalAdjust == 0) {
    // no reverbs are within distance. Check for rolloff's
    Flt partial = 0;
    for (auto i = reverbs.begin(); i != reverbs.end(); ++i) {
      if (!(*i)->active) continue;
      if (Dist((*i)->position, Global.getListener().newPos) <= (*i)->size + (*i)->rolloff) {
        // add partial reverb
        Flt adjust = Dist((*i)->position, Global.getListener().newPos) - (*i)->size;
        calculatedValues.roomsize += (*i)->roomsize * adjust;
        calculatedValues.damp += (*i)->damp * adjust;
        calculatedValues.wet += (*i)->wet * adjust;
        if (adjust != 0) calculatedValues.dry += (*i)->dry / adjust;
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
    active = false;
  }
  else {
    active = true;
    roomsize = calculatedValues.roomsize;
    damp = calculatedValues.damp;
    wet = calculatedValues.wet;
    dry = calculatedValues.dry;
    modFrequency = calculatedValues.modFrequency;
    modWidth = calculatedValues.modWidth;
    for (Int j = 0; j < 4; j++) {
      earlyGain[j] = calculatedValues.earlyGain[j];
      earlyPtr[j] = calculatedValues.earlyPtr[j];
    }
  }
}

void YSE::INTERNAL::reverbManager::attachToChannel(YSE::INTERNAL::channelImplementation * ptr) {
  reverbChannel = ptr;
}

void YSE::INTERNAL::reverbManager::process(YSE::INTERNAL::channelImplementation * ptr) {
  if (ptr != reverbChannel) return;
  if (!active) return;

  // put all values in dspValues (which is only used inside the dsp callback)
  dspValues.roomsize = roomsize;
  dspValues.damp = damp;
  dspValues.wet = wet;
  dspValues.dry = dry;
  dspValues.modFrequency = modFrequency;
  dspValues.modWidth = modWidth;
  for (Int j = 0; j < 4; j++) {
    dspValues.earlyGain[j] = earlyGain[j];
    dspValues.earlyPtr[j] = earlyPtr[j];
  }
  reverbObject->set(dspValues);

  // the actual reverb processing
  reverbObject->process(ptr->out);
}