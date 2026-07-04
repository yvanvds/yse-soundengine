/*
  ==============================================================================

    reverbManager.cpp
    Created: 1 Feb 2014 7:02:37pm
    Author:  yvan

  ==============================================================================
*/

#include "../internalHeaders.h"

YSE::REVERB::managerObject& YSE::REVERB::Manager() {
  static managerObject m;
  return m;
}

YSE::REVERB::managerObject::managerObject()
  : globalReverb(true), calculatedValues(true), mgrDelete(this) {
  reverbDSPObject.channels(CHANNEL::Manager().getNumberOfOutputs());
}

YSE::REVERB::managerObject::~managerObject() noexcept {
  try {
    mgrDelete.join();

    // drain any pointers still queued by the main thread; they reference impls
    // owned by `implementations` and will be freed when that list is cleared.
    implementationObject* drained;
    while (toLoadInbox.try_pop(drained)) {
      (void)drained;
    }

    // remove all objects that are still in memory
    toLoad.clear();
    inUse.clear();
    implementations.clear();
  } catch (...) {
    INTERNAL::LogImpl().emit(E_ERROR, "REVERB::Manager destructor swallowed exception");
  }
}

void YSE::REVERB::managerObject::create() {
  globalReverb.create();
  calculatedValues.create();
}

void YSE::REVERB::managerObject::destroy() {
  // Runs from INTERNAL::global::close() after both thread pools have been
  // joined and the audio device is closed, so nothing else can touch these
  // lists while we clear them.

  // Drain the main->audio inbox; the pointers it holds reference impls owned
  // by `implementations` and would dangle once that list is cleared below.
  implementationObject* drained;
  while (toLoadInbox.try_pop(drained)) {
    (void)drained;
  }
  toLoad.clear();
  inUse.clear();
  {
    std::scoped_lock lk(implementationsMutex);
    implementations.clear();
  }
  runDelete = false;

  // The global and calculated reverbs are persistent members that outlive the
  // session; their implementations were just destroyed above. Clear the now
  // dangling handles so the next System::init() re-runs reverb::create()
  // cleanly instead of tripping assert(pimpl == nullptr) (issue #132).
  globalReverb.pimpl = nullptr;
  calculatedValues.pimpl = nullptr;
}

YSE::REVERB::implementationObject*
YSE::REVERB::managerObject::addImplementation(YSE::reverb* head) {
  std::scoped_lock lk(implementationsMutex);
  implementations.emplace_front(head);
  return &implementations.front();
}

void YSE::REVERB::managerObject::setup(YSE::REVERB::implementationObject* impl) {
  // reverb objects don't need slow-pool setup; they go straight to
  // OBJECT_SETUP and are handed to the audio thread via the inbox.
  impl->setStatus(OBJECT_SETUP);
  toLoadInbox.push(impl);
}

void YSE::REVERB::managerObject::setOutputChannels(Int value) {
  reverbDSPObject.channels(value);
}

YSE::reverb& YSE::REVERB::managerObject::getGlobalReverb() {
  return globalReverb;
}

Bool YSE::REVERB::managerObject::empty() {
  return implementations.empty();
}

void YSE::REVERB::managerObject::update() {
  ///////////////////////////////////////////
  // drain the main→audio inbox of newly-set-up impls
  ///////////////////////////////////////////
  {
    implementationObject* p;
    while (toLoadInbox.try_pop(p))
      toLoad.push_front(p);
  }

  toLoad.remove_if(implementationObject::canBeRemovedFromLoading);

  if (runDelete && !mgrDelete.isQueued()) {
    INTERNAL::Global().addSlowJob(&mgrDelete);
  }
  runDelete = false;

  ///////////////////////////////////////////
  // check if loaded implementations are ready
  //
  // Erase from toLoad immediately on move to inUse — see SOUND/CHANNEL
  // manager update() for the use-after-free rationale this avoids.
  ///////////////////////////////////////////
  {
    for (auto c = toLoad.front(); c.valid();) {
      implementationObject* ptr = c.get();
      if (ptr->readyCheck()) {
        // Unlink from toLoad before linking into inUse: both share the impl's
        // single `_mgrNext` link (issue #194).
        c.erase();
        inUse.push_front(ptr);
      } else {
        c.next();
      }
    }
  }

  ///////////////////////////////////////////
  // sync and update implementations
  ///////////////////////////////////////////
  {
    for (auto c = inUse.front(); c.valid();) {
      implementationObject* ptr = c.get();
      ptr->sync();
      if (ptr->getStatus() == OBJECT_RELEASE) {
        c.erase();
        ptr->setStatus(OBJECT_DELETE);
        runDelete = true;
        continue; // c already refers to the successor after erase()
      }
      c.next();
    }
  }

  Int reverbsActive = 0;
  calculatedValues.setPreset(REVERB_OFF);
  calculatedValues.setActive(false);
  calculatedValues.setDryWetBalance(
      0.f, 0.f); // this is ok here, because this reverb is only used for calculating the real one

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
  } else if (reverbsActive == 0) {
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
          calculatedValues.earlyPtr[j] =
              static_cast<Int>(calculatedValues.earlyPtr[j] + (*i)->earlyPtr[j] * adjust);
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
      // Fold in the global reverb by copying only its DSP parameter fields —
      // never its pimpl (issue #192). Source the values from the impl the
      // audio thread just sync()'d, not the interface fields the main thread
      // writes concurrently.
      if (globalReverb.pimpl != nullptr) {
        globalReverb.pimpl->copyParamsInto(calculatedValues);
      }
      return; // important because active could be overwritten at the end of this function
    }

    // if sum of partial reverbs < 1 we have to add (part of) the global reverb
    else if (partial < 1) {
      // Read the global reverb's parameters from its synced impl (issue #192);
      // the interface fields are written by the main thread and racy here.
      const implementationObject* g = globalReverb.pimpl;
      if (g != nullptr && g->active) {
        calculatedValues.roomsize += g->roomsize * (1 - partial);
        calculatedValues.damp += g->damp * (1 - partial);
        calculatedValues.wet += g->wet * (1 - partial);
        calculatedValues.dry += g->dry * (1 - partial);
        calculatedValues.modFrequency += g->modFrequency * (1 - partial);
        calculatedValues.modWidth += g->modWidth * (1 - partial);
        for (Int j = 0; j < 4; j++) {
          calculatedValues.earlyGain[j] += g->earlyGain[j] * (1 - partial);
          calculatedValues.earlyPtr[j] =
              static_cast<Int>(calculatedValues.earlyPtr[j] + g->earlyGain[j] * (1 - partial));
        }
      }
    }
  }

  // end of calculations, disable reverb if no wet signal
  if (calculatedValues.wet < 0.001) {
    calculatedValues.active = false;
  } else {
    calculatedValues.active = true;
  }
}

void YSE::REVERB::managerObject::attachToChannel(YSE::CHANNEL::implementationObject* ptr) {
  reverbChannel = ptr;
}

void YSE::REVERB::managerObject::process(YSE::CHANNEL::implementationObject* ptr) {
  if (ptr != reverbChannel) return;
  if (!calculatedValues.active) return;
  reverbDSPObject.set(calculatedValues);

  // the actual reverb processing
  reverbDSPObject.process(ptr->out);
}