/*
  ==============================================================================

    morphingReverb.cpp
    Morphing reverb module — preset interpolation as a control input
    (issue #326).

  ==============================================================================
*/

#include "morphingReverb.hpp"
#include "../../utils/misc.hpp"

// ─── slot ─────────────────────────────────────────────────────────────────────

void YSE::DSP::MODULES::morphingReverb::slot::store(const YSE::REVERB::presetValues& v) {
  roomsize.store(v.roomsize);
  damp.store(v.damp);
  dry.store(v.dry);
  wet.store(v.wet);
  modFrequency.store(v.modFrequency);
  modWidth.store(v.modWidth);
  for (Int i = 0; i < 4; i++) {
    earlyTime[i].store(v.earlyTime[i]);
    earlyGain[i].store(v.earlyGain[i]);
  }
}

YSE::REVERB::presetValues YSE::DSP::MODULES::morphingReverb::slot::load() const {
  YSE::REVERB::presetValues v;
  v.roomsize = roomsize.load();
  v.damp = damp.load();
  v.dry = dry.load();
  v.wet = wet.load();
  v.modFrequency = modFrequency.load();
  v.modWidth = modWidth.load();
  for (Int i = 0; i < 4; i++) {
    v.earlyTime[i] = earlyTime[i].load();
    v.earlyGain[i] = earlyGain[i].load();
  }
  return v;
}

// ─── morphingReverb ───────────────────────────────────────────────────────────

YSE::DSP::MODULES::morphingReverb::morphingReverb() : parmMorph(0.f) {
  slotA.store(REVERB::getPresetValues(REVERB_GENERIC));
  slotB.store(REVERB::getPresetValues(REVERB_HALL));
}

YSE::DSP::MODULES::morphingReverb& YSE::DSP::MODULES::morphingReverb::presetA(REVERB_PRESET value) {
  slotA.store(REVERB::getPresetValues(value));
  return *this;
}

YSE::DSP::MODULES::morphingReverb&
YSE::DSP::MODULES::morphingReverb::presetA(const REVERB::presetValues& value) {
  slotA.store(value);
  return *this;
}

YSE::REVERB::presetValues YSE::DSP::MODULES::morphingReverb::presetA() const {
  return slotA.load();
}

YSE::DSP::MODULES::morphingReverb& YSE::DSP::MODULES::morphingReverb::presetB(REVERB_PRESET value) {
  slotB.store(REVERB::getPresetValues(value));
  return *this;
}

YSE::DSP::MODULES::morphingReverb&
YSE::DSP::MODULES::morphingReverb::presetB(const REVERB::presetValues& value) {
  slotB.store(value);
  return *this;
}

YSE::REVERB::presetValues YSE::DSP::MODULES::morphingReverb::presetB() const {
  return slotB.load();
}

YSE::DSP::MODULES::morphingReverb& YSE::DSP::MODULES::morphingReverb::morph(Flt value) {
  Clamp(value, 0.f, 1.f);
  parmMorph.store(value);
  return *this;
}

Flt YSE::DSP::MODULES::morphingReverb::morph() const {
  return parmMorph.load();
}

void YSE::DSP::MODULES::morphingReverb::create() {
  // Nothing to size yet: the per-channel reverb state depends on the channel
  // count, which is only known once process() sees a buffer. reverbDSP's
  // channels() sizes it there (a no-op in steady state).
}

void YSE::DSP::MODULES::morphingReverb::process(MULTICHANNELBUFFER& buffer) {
  createIfNeeded();

  if (buffer.empty()) return;

  // Size per-channel state to the buffer. This allocates only when the
  // channel count actually changed — the device-restart path, the one place
  // the dspObject contract permits allocation.
  verb.channels(static_cast<Int>(buffer.size()));

  // Blend the two endpoints at the current morph position and push the result
  // through the core's faders: repeated control-rate writes ramp click-free.
  REVERB::presetValues blended = REVERB::morph(slotA.load(), slotB.load(), parmMorph.load());

  // Insert-grade gain mapping. The freeverb core's faders carry historical
  // scale factors (scaledry = 2, and scalewet = 3 against a 0.5 width term)
  // tuned for the global zone path, where dry = 1 means x2 makeup gain.
  // Remap so this module's contract is what an insert user expects:
  // dry = 1 is unity gain — morphing fully to REVERB_OFF is a true bypass —
  // and wet = 1 is the reverb tank at its computed level.
  blended.dry *= 0.5f;
  blended.wet *= 2.0f / 3.0f;
  verb.set(blended);

  verb.process(buffer);
}
