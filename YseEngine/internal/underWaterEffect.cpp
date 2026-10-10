/*
  ==============================================================================

    underWaterEffect.cpp
    Created: 1 Feb 2014 10:02:28pm
    Author:  yvan

    Re-expressed for issue #327: the DSP moved into the ordinary insert
    module DSP::MODULES::underWater; this class is now only the engine-side
    *driver* that binds the module to its default spatial control.

  ==============================================================================
*/

#include "../internalHeaders.h"

YSE::INTERNAL::underWaterEffect& YSE::INTERNAL::UnderWaterEffect() {
  static underWaterEffect u;
  return u;
}

YSE::INTERNAL::underWaterEffect::underWaterEffect() : lastTarget(nullptr) {
  ensureZone();
}

// (Re)build the REVERB_UNDERWATER zone for the current engine session.
//
// The zone is a persistent interface owned by this process-global driver, but
// its implementation is session state: REVERB::Manager().destroy() clears every
// reverb implementation at System::close(), and each implementation's
// destructor nulls its interface's pimpl. The manager re-creates its own two
// persistent reverbs (globalReverb, calculatedValues) in create(); nothing
// re-created this third one, because it was only ever built in this
// constructor — which runs once per process. Every session after the first then
// messaged a null implementation the moment a host touched the effect: an
// access violation in reverb::setActive(), and the fault the unfiltered
// yse_tests run died on (issue #715).
//
// The zone is rebuilt rather than re-created behind the existing interface
// because YSE::reverb caches every value it has sent and skips a setter whose
// value is unchanged: re-running create() on the old interface would leave the
// fresh implementation at its constructor defaults instead of the underwater
// preset. A new interface has no cached state, so the preset lands.
//
// Control thread only — the callers are the public system::underWaterFX() /
// setUnderWaterDepth() entry points — so the allocation is off every audio
// path.
bool YSE::INTERNAL::underWaterEffect::ensureZone() {
  if (verb && verb->isValid()) return true;
  // Nothing to attach an implementation to before init() or after close().
  if (!Global().isActive()) return false;
  verb = std::make_unique<reverb>();
  verb->create();
  verb->setPreset(REVERB_UNDERWATER);
  verb->setSize(10);
  verb->setActive(false);
  return true;
}

YSE::reverb* YSE::INTERNAL::underWaterEffect::zone() {
  return verb.get();
}

YSE::INTERNAL::underWaterEffect& YSE::INTERNAL::underWaterEffect::attach(const channel& target) {
  // Re-pointing to another channel: sever the module from its current owner
  // first so two channels can never run the same instance concurrently (the
  // module's filter state is single-owner). calledfrom is the engine-managed
  // back-pointer into the owning impl's insert_dsp slot; clearing through it
  // is the same pointer-store discipline dspObject's destructor uses (#298).
  // lastTarget is compared by identity only: if the previous interface has
  // been destroyed, its impl teardown already cleared fx.calledfrom and this
  // branch is a no-op. (The previous interface's getDSP() mirror goes stale
  // here — the same staleness the destructor path has always had.)
  if (lastTarget != &target && fx.calledfrom != nullptr) {
    *fx.calledfrom = nullptr;
    fx.calledfrom = nullptr;
  }
  lastTarget = &target;

  // The ordinary insert path: setDSP posts ATTACH_DSP, applied by the
  // channel's audio-thread sync(). setDSP is logically non-const on the
  // channel (it occupies the insert slot); the const_cast keeps the
  // historical system::underWaterFX(const channel&) signature intact.
  const_cast<channel&>(target).setDSP(&fx);
  return *this;
}

YSE::INTERNAL::underWaterEffect& YSE::INTERNAL::underWaterEffect::setDepth(Flt value) {
  // The module parameter is a plain atomic and carries no session state, so it
  // takes the value whether or not a session is up.
  fx.depth(value);
  // The zone does carry session state; with no session there is nothing to
  // drive and messaging the stale handle is the #715 crash.
  if (!ensureZone()) return *this;
  if (value > 0) {
    verb->setActive(true);
    verb->setPosition(ListenerImpl().pos);
  } else {
    verb->setActive(false);
  }
  return *this;
}

YSE::DSP::MODULES::underWater& YSE::INTERNAL::underWaterEffect::module() {
  return fx;
}
