/*
  ==============================================================================

    underWater.cpp
    Underwater effect module — the channel-path special case re-expressed as
    an ordinary insert (issue #327).

  ==============================================================================
*/

#include "underWater.hpp"
#include "../math.hpp"

YSE::DSP::MODULES::underWater::underWater() : parmDepth(0.f) {}

YSE::DSP::MODULES::underWater& YSE::DSP::MODULES::underWater::depth(Flt value) {
  if (value < 0.f) value = 0.f;
  parmDepth.store(value);
  return *this;
}

Flt YSE::DSP::MODULES::underWater::depth() const {
  return parmDepth.load();
}

void YSE::DSP::MODULES::underWater::create() {
  // Nothing to size yet: the mixdown scratch depends on the block length,
  // which is only known once process() sees a buffer.
}

void YSE::DSP::MODULES::underWater::process(MULTICHANNELBUFFER& buffer) {
  createIfNeeded();

  // The effect only engages below one unit of depth — the same threshold the
  // legacy channel-path implementation used. Above water (or barely under)
  // the buffer passes through untouched.
  const Flt d = parmDepth.load();
  if (d <= 1.f || buffer.empty()) return;

  // (Re)size the mixdown scratch to the block length. This allocates only
  // when the length actually changed — the device-restart path, the one
  // place the dspObject contract permits allocation.
  const UInt length = buffer[0].getLength();
  if (mono.getLength() != length) mono.resize(length);

  // Sound underwater is more position neutral. Because the speed of sound is
  // much higher, the ear cannot hear what direction it comes from: build a
  // position-neutral average of all channels first.
  mono = 0.f;
  for (UInt i = 0; i < buffer.size(); ++i) {
    mono += buffer[i];
  }
  mono /= static_cast<Flt>(buffer.size());

  // The deeper the listener, the darker the mix: the cutoff falls with depth
  // along the legacy curve, floored at 200 Hz.
  Flt cutoff = MidiToFreq(140.f - d * 5.f);
  if (cutoff < 200.f) cutoff = 200.f;
  filter.setFrequency(cutoff);
  // The one-pole returns its own output buffer (sized on the first call — the
  // create path); scaling it below is fine, it is rewritten every block.
  YSE::DSP::buffer& lp = filter(mono);

  if (d > 5.f) {
    // Fully submerged: discard position information entirely.
    for (UInt i = 0; i < buffer.size(); ++i) {
      buffer[i] = lp;
    }
  } else {
    // Partly replace the positioned signal with the neutral version.
    lp *= d / 5.f;
    for (UInt i = 0; i < buffer.size(); ++i) {
      buffer[i] *= (5.f - d) / 5.f;
      buffer[i] += lp;
    }
  }
}
