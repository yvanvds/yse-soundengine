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
  verb.create();
  verb.setPreset(REVERB_UNDERWATER);
  verb.setSize(10);
  verb.setActive(false);
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
  fx.depth(value);
  if (value > 0) {
    verb.setActive(true);
    verb.setPosition(ListenerImpl().pos);
  } else {
    verb.setActive(false);
  }
  return *this;
}

YSE::DSP::MODULES::underWater& YSE::INTERNAL::underWaterEffect::module() {
  return fx;
}
