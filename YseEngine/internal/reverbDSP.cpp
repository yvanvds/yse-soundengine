/*
  ==============================================================================

    reverbDSP.cpp
    Created: 1 Feb 2014 6:58:46pm
    Author:  yvan

  ==============================================================================
*/

#include "reverbDSP.h"
#include "../reverb/reverbInterface.hpp"
#include "../utils/misc.hpp"
#include <limits>

#define LOGTEN 2.302585092994

#define muted 0
#define fixedgain 0.015f
#define scalewet 3.0f
#define scaledry 2.0f
#define scaledamp 0.4f
#define scaleroom 0.28f
#define offsetroom 0.7f
#define initialroom 0.5f
#define initialdamp 0.5f
#define initialwet 1.0f / scalewet
#define initialdry 0.0f
#define initialwidth 1.0f
#define initialmode 0
#define initialbypass 0
#define freezemode 0.5f
#define stereospread 23

inline void Undenormal(Flt& v) {
  if (v != 0 && fabsf(v) < std::numeric_limits<float>::min()) v = 0;
}

/* these values assume 44.1KHz sample rate
they will probably be OK for 48KHz sample rate
but would need scaling for 96KHz (or other) sample rates.
the values were obtained by listening tests.                */
static const int combtuning[8] = {1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617};
static const int allpasstuning[4] = {556, 441, 341, 225};

// The combtuning / allpasstuning arrays above are calibrated for this
// reference sample rate. At runtime the tunings are scaled by
// (SAMPLERATE / kReverbTuningReferenceRate) so the perceived reverb
// character stays consistent across device-negotiated sample rates.
static constexpr float kReverbTuningReferenceRate = 44100.0f;

// The comb/allpass kernels and every loop-carried working value used to be
// file-scope globals ("for speed optimisation", 2014). That made the whole
// DSP core non-re-entrant: two reverbDSP instances processing concurrently
// (the manager's global instance and a morphingReverb channel insert on
// another fast-pool worker) raced on them. All state now lives in the
// instance; the kernels below take their per-filter state by reference and
// keep the exact arithmetic of the old globals-based versions (issue #326).

inline Flt YSE::INTERNAL::reverbDSP::combProcess(Flt input, std::vector<Flt>& buf, Flt& filterStore,
                                                 Int& index, Int tuning) {
  Flt output = buf[index];
  Undenormal(output);

  filterStore = (output * _combDamp2) + (filterStore * _combDamp1);
  Undenormal(filterStore);

  buf[index] = input + (filterStore * _combFeedback);
  if (++index >= tuning) index = 0;
  return output;
}

inline void YSE::INTERNAL::reverbDSP::allpassProcess(Flt& value, std::vector<Flt>& buf, Int& index,
                                                     Int tuning) {
  Flt bufout = buf[index];
  Undenormal(bufout);

  buf[index] = value + (bufout * _allpassFeedback);
  if (++index >= tuning) index = 0;

  value = -value + bufout;
}

void YSE::INTERNAL::reverbDSP::combDamp(Flt value) {
  _combDamp1 = value;
  _combDamp2 = 1 - value;
}

YSE::INTERNAL::reverbDSP& YSE::INTERNAL::reverbDSP::combFeedback(Flt value) {
  _combFeedback = value;
  return (*this);
}

Flt YSE::INTERNAL::reverbDSP::combFeedback() {
  return _combFeedback;
}

YSE::INTERNAL::reverbDSP& YSE::INTERNAL::reverbDSP::allpassFeedback(Flt value) {
  _allpassFeedback = value;
  return (*this);
}

Flt YSE::INTERNAL::reverbDSP::allpassFeedback() {
  return _allpassFeedback;
}

YSE::INTERNAL::reverbDSP& YSE::INTERNAL::reverbDSP::bypass(Bool value) {
  _bypass = value;
  if (_bypass) {
    if (_freeze) return (*this);

    for (UInt i = 0; i < channel.size(); i++)
      channel[i].clear();
  }

  return (*this);
}

Bool YSE::INTERNAL::reverbDSP::bypass() {
  return _bypass;
}

void YSE::INTERNAL::reverbDSP::process(MULTICHANNELBUFFER& buffer) {
  if (_bypass) return;
  if (channel.empty()) return;
  update();

  // update frequency modulation
  if (_modFrequency() != 0 && _modWidth() != 0) {
    modPtr = sinGen(_modFrequency(), buffer[0].getLength());
    modPtr *= _modWidth();
    modPtr = sawGen(modPtr);
    cosPtr1 = cos1(modPtr);
    modPtr += -0.25;
    cosPtr2 = cos2(modPtr);
  }

  for (UInt ch = 0; ch < channel.size(); ch++) {
    reverbChannel& c = channel[ch];
    Flt* ptr = buffer[ch].getPtr();
    c.out = 0;
    Flt* out = c.out.getPtr();
    Int length = buffer[ch].getLength();

    // update delay line
    c.delayline.process(buffer[ch]);
    for (Int i = 0; i < 4; i++) {
      if (c.earlyVolume[i].getValue() > 0) {
        c.delayline.read(c.early[i], c.earlyPtr[i]());
        c.early[i] *= c.earlyVolume[i]();
        buffer[ch] += c.early[i];
      }
    }

    for (; length > 7; length -= 8, ptr += 8, out += 8) {
      for (Int filterIndex = 0; filterIndex < COMBS; filterIndex++) {
        std::vector<Flt>& buf = c.bufComb[filterIndex];
        Flt& store = c.filterStore[filterIndex];
        Int& index = c.combIndex[filterIndex];
        const Int tuning = c.combTuning[filterIndex];

        out[0] += combProcess(ptr[0] * _gain, buf, store, index, tuning);
        out[1] += combProcess(ptr[1] * _gain, buf, store, index, tuning);
        out[2] += combProcess(ptr[2] * _gain, buf, store, index, tuning);
        out[3] += combProcess(ptr[3] * _gain, buf, store, index, tuning);
        out[4] += combProcess(ptr[4] * _gain, buf, store, index, tuning);
        out[5] += combProcess(ptr[5] * _gain, buf, store, index, tuning);
        out[6] += combProcess(ptr[6] * _gain, buf, store, index, tuning);
        out[7] += combProcess(ptr[7] * _gain, buf, store, index, tuning);
      }

      for (Int filterIndex = 0; filterIndex < APASS; filterIndex++) {
        std::vector<Flt>& buf = c.bufAll[filterIndex];
        Int& index = c.allIndex[filterIndex];
        const Int tuning = c.allTuning[filterIndex];

        allpassProcess(out[0], buf, index, tuning);
        allpassProcess(out[1], buf, index, tuning);
        allpassProcess(out[2], buf, index, tuning);
        allpassProcess(out[3], buf, index, tuning);
        allpassProcess(out[4], buf, index, tuning);
        allpassProcess(out[5], buf, index, tuning);
        allpassProcess(out[6], buf, index, tuning);
        allpassProcess(out[7], buf, index, tuning);
      }
    }

    while (length--) {
      // accumulate comb filters in parallel
      for (Int filterIndex = 0; filterIndex < COMBS; filterIndex++) {
        *out += combProcess(*ptr * _gain, c.bufComb[filterIndex], c.filterStore[filterIndex],
                            c.combIndex[filterIndex], c.combTuning[filterIndex]);
      }

      // feed through allpass in series
      for (Int filterIndex = 0; filterIndex < APASS; filterIndex++) {
        allpassProcess(*out, c.bufAll[filterIndex], c.allIndex[filterIndex],
                       c.allTuning[filterIndex]);
      }
      ptr++;
      out++;
    }

    // apply modulation to wet signal
    if (_modFrequency() != 0 && _modWidth() != 0) {
      channel[ch].hil(channel[ch].out, channel[ch].hil1, channel[ch].hil2);
      channel[ch].hil1 *= cosPtr1;
      channel[ch].hil2 *= cosPtr2;
      channel[ch].hil1 -= channel[ch].hil2;

      // Calculate output REPLACING anything already there
      buffer[ch] *= _dryFader();
      channel[ch].hil1 *= _wet1;
      buffer[ch] += channel[ch].hil1;
    } else {
      buffer[ch] *= _dryFader();
      channel[ch].out *= _wet1;
      buffer[ch] += channel[ch].out;
    }
  }
}

void YSE::INTERNAL::reverbDSP::update() {
  _roomsizeFader.update();
  _dampFader.update();
  _wetFader.update();
  _dryFader.update();
  _widthFader.update();
  _modFrequency.update();
  _modWidth.update();
  for (UInt i = 0; i < channel.size(); i++) {
    channel[i].update();
  }

  _wet1 = _wetFader() * (_widthFader() / 2 + 0.5f);
  _wet2 = _wetFader() * ((1 - _widthFader()) / 2);

  if (_freeze) {
    _roomsize1 = 1.;
    _damp1 = 0.;
    _gain = muted;
  } else {
    _roomsize1 = _roomsizeFader();
    _damp1 = _dampFader();
    _gain = static_cast<Flt>(fixedgain);
  }

  combFeedback(_roomsize1);
  combDamp(_damp1);
}

YSE::INTERNAL::reverbDSP& YSE::INTERNAL::reverbDSP::roomSize(Flt value) {
  _roomsizeFader.setIfNew((value * scaleroom) + offsetroom, 1000);
  return (*this);
}

Flt YSE::INTERNAL::reverbDSP::roomSize() {
  return (_roomsizeFader() - offsetroom) / scaleroom;
}

YSE::INTERNAL::reverbDSP& YSE::INTERNAL::reverbDSP::damp(Flt value) {
  _dampFader.setIfNew(value * scaledamp, 1000);
  return (*this);
}

Flt YSE::INTERNAL::reverbDSP::damp() {
  return _dampFader() / scaledamp;
}

YSE::INTERNAL::reverbDSP& YSE::INTERNAL::reverbDSP::wet(Flt value) {
  _wetFader.setIfNew(value * scalewet, 1000);
  return (*this);
}

Flt YSE::INTERNAL::reverbDSP::wet() {
  return _wetFader() / scalewet;
}

YSE::INTERNAL::reverbDSP& YSE::INTERNAL::reverbDSP::dry(Flt value) {
  _dryFader.setIfNew(value * scaledry, 1000);
  return (*this);
}

Flt YSE::INTERNAL::reverbDSP::dry() {
  return _dryFader() / scaledry;
}

YSE::INTERNAL::reverbDSP& YSE::INTERNAL::reverbDSP::width(Flt value) {
  _widthFader.setIfNew(value, 1000);
  return (*this);
}

Flt YSE::INTERNAL::reverbDSP::width() {
  return _widthFader();
}

YSE::INTERNAL::reverbDSP& YSE::INTERNAL::reverbDSP::freeze(Bool value) {
  _freeze = value;
  return (*this);
}

Bool YSE::INTERNAL::reverbDSP::freeze() {
  return _freeze;
}

void YSE::INTERNAL::reverbDSP::channels(Int value) {
  if (channel.size() != static_cast<size_t>(value)) channel.resize(static_cast<size_t>(value));
}

void YSE::INTERNAL::reverbDSP::modulate(Flt frequency, Flt width) {
  if (frequency < 0) frequency = 0;
  _modFrequency.setIfNew(frequency, 1000);
  _modWidth.setIfNew(width, 1000);
}

void YSE::INTERNAL::reverbDSP::set(YSE::reverb& params) {
  if (!params.getActive()) {
    bypass(true);
    return;
  }

  bypass(false);
  roomSize(params.getRoomSize());
  damp(params.getDamping());
  wet(params.getWet());
  dry(params.getDry());
  width(0);
  modulate(params.getModulationFrequency(), params.getModulationWidth());

  for (Int i = 0; i < 4; i++) {
    for (UInt ch = 0; ch < channel.size(); ch++) {
      channel[ch].earlyPtr[i].setIfNew(
          (float)(params.getReflectionTime(i) + channel[ch].earlyOffset), 1000);
      channel[ch].earlyVolume[i].setIfNew(params.getReflectionGain(i), 1000);
    }
  }
}

void YSE::INTERNAL::reverbDSP::set(const YSE::REVERB::presetValues& params) {
  // Mirrors set(reverb&) but takes a plain parameter set, so a control input
  // (DSP::MODULES::morphingReverb) can feed blended presets straight into the
  // faders. set(reverb&) relies on the interface setters' clamping; a
  // presetValues can come from user code, so clamp here instead. Runs on the
  // audio thread: no allocation, faders ramp so repeated control-rate calls
  // stay click-free.
  bypass(false);

  Flt value = params.roomsize;
  Clamp(value, 0.f, 1.f);
  roomSize(value);

  value = params.damp;
  Clamp(value, 0.f, 1.f);
  damp(value);

  value = params.wet;
  Clamp(value, 0.f, 1.f);
  wet(value);

  value = params.dry;
  Clamp(value, 0.f, 1.f);
  dry(value);

  width(0);
  modulate(params.modFrequency, params.modWidth < 0.f ? 0.f : params.modWidth);

  for (Int i = 0; i < 4; i++) {
    Flt time = params.earlyTime[i];
    Clamp(time, 0.f, 2999.f);
    Flt gain = params.earlyGain[i];
    Clamp(gain, 0.f, 1.f);
    for (UInt ch = 0; ch < channel.size(); ch++) {
      channel[ch].earlyPtr[i].setIfNew(time + channel[ch].earlyOffset, 1000);
      channel[ch].earlyVolume[i].setIfNew(gain, 1000);
    }
  }
}

YSE::INTERNAL::reverbDSP::reverbDSP() {
  _freeze = false;

  // default values
  _modFrequency.set(0, 0);
  _modWidth.set(0, 0);
  _allpassFeedback = 0.5;
  _combFeedback = 0;
  _combDamp1 = 0;
  _combDamp2 = 1;
  wet(initialwet);
  roomSize(initialroom);
  dry(initialdry);
  damp(initialdamp);
  width(initialwidth);
  bypass(true); // set mute for buffer cleaning
  bypass(false); // extra call doesn't cost much time
}

YSE::INTERNAL::reverbDSP::~reverbDSP() {}

YSE::INTERNAL::reverbChannel::reverbChannel() : delayline(3000), bufComb(COMBS), bufAll(APASS) {
  Int rnd = Random(50);
  // recalculate the reverb parameters in case we don't run at 44.1kHz
  for (Int i = 0; i < COMBS; i++) {
    combTuning[i] = (Int)((combtuning[i] + rnd) * (SAMPLERATE / kReverbTuningReferenceRate));
  }

  for (int i = 0; i < APASS; i++) {
    allTuning[i] = (Int)((allpasstuning[i] + rnd) * (SAMPLERATE / kReverbTuningReferenceRate));
  }

  // get memory for delay lines
  for (Int i = 0; i < COMBS; i++) {
    bufComb[i].resize(combTuning[i]);
    combIndex[i] = 0;
  }

  for (Int i = 0; i < APASS; i++) {
    bufAll[i].resize(allTuning[i]);
    allIndex[i] = 0;
  }

  earlyOffset = Random(30);
  /*earlyPtr[0] = 60;
  earlyPtr[1] = 100;
  earlyPtr[2] = 150;
  earlyPtr[3] = 230;
  earlyVolume[0] = 0.50;
  earlyVolume[1] = 0.30;
  earlyVolume[2] = 0.35;
  earlyVolume[3] = 0.20;*/

  clear();
}

YSE::INTERNAL::reverbChannel::reverbChannel(const reverbChannel& /*source*/)
  : delayline(3000), bufComb(COMBS), bufAll(APASS) {
  Int rnd = Random(50);
  // recalculate the reverb parameters in case we don't run at 44.1kHz
  for (Int i = 0; i < COMBS; i++) {
    combTuning[i] = (Int)((combtuning[i] + rnd) * (SAMPLERATE / kReverbTuningReferenceRate));
  }

  for (int i = 0; i < APASS; i++) {
    allTuning[i] = (Int)((allpasstuning[i] + rnd) * (SAMPLERATE / kReverbTuningReferenceRate));
  }

  // get memory for delay lines
  for (Int i = 0; i < COMBS; i++) {
    bufComb[i].resize(combTuning[i]);
    combIndex[i] = 0;
  }

  for (Int i = 0; i < APASS; i++) {
    bufAll[i].resize(allTuning[i]);
    allIndex[i] = 0;
  }

  earlyOffset = Random(30);
  clear();
}

void YSE::INTERNAL::reverbChannel::clear() {
  for (UInt i = 0; i < bufComb.size(); i++) {
    bufComb[i].assign(bufComb[i].size(), 0.f);
    filterStore[i] = 0.f;
  }

  for (UInt i = 0; i < bufAll.size(); i++) {
    bufAll[i].assign(bufAll[i].size(), 0.f);
  }
}

void YSE::INTERNAL::reverbChannel::update() {
  for (Int i = 0; i < 4; i++) {
    earlyPtr[i].update();
    earlyVolume[i].update();
  }
}
