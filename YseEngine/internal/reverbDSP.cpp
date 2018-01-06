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

#define	muted					0
#define	fixedgain			0.015f
#define scalewet			3.0f
#define scaledry			2.0f
#define scaledamp			0.4f
#define scaleroom			0.28f
#define offsetroom		0.7f
#define initialroom		0.5f
#define initialdamp		0.5f
#define initialwet		1.0f/scalewet
#define initialdry		0.0f
#define initialwidth	1.0f
#define initialmode		0
#define initialbypass 0
#define freezemode		0.5f
#define	stereospread	23

inline void Undenormal(Flt &v) {
  if (v != 0 && fabsf(v) < std::numeric_limits<float>::min()) v = 0;
}

/* these values assume 44.1KHz sample rate
they will probably be OK for 48KHz sample rate
but would need scaling for 96KHz (or other) sample rates.
the values were obtained by listening tests.                */
static const int combtuning[8] = { 1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617 };
static const int allpasstuning[4] = { 556, 441, 341, 225 };

// static inline functions and pointer for speed optimisation
UInt ch;
Int filterIndex;
Int * curCombIndex;
Int * curCombTuning;
std::vector< std::vector<Flt> > * curBufComb;
Flt * curFilterStore;
std::vector< std::vector<Flt> > * curBufAll;
Int * curAllIndex;
Int * curAllTuning;
Flt _allpassFeedback;
Flt _combFeedback;
Flt combDamp1;
Flt combDamp2;
Int bufferIndex;

static inline Flt combProcess(Flt input) {
  Flt output;
  output = (*curBufComb)[filterIndex][bufferIndex];
  Undenormal(output);

  curFilterStore[filterIndex] = (output * combDamp2) + (curFilterStore[filterIndex] * combDamp1);
  Undenormal(curFilterStore[filterIndex]);

  (*curBufComb)[filterIndex][bufferIndex] = input + (curFilterStore[filterIndex] * _combFeedback);
  if (++bufferIndex >= curCombTuning[filterIndex]) bufferIndex = 0;
  return output;
}

static inline void allpassProcess(Flt & input) {
  Flt bufout;

  bufout = (*curBufAll)[filterIndex][bufferIndex];
  Undenormal(bufout);

  (*curBufAll)[filterIndex][bufferIndex] = input + (bufout * _allpassFeedback);
  if (++bufferIndex >= curAllTuning[filterIndex]) bufferIndex = 0;

  input = -input + bufout;
}


void YSE::INTERNAL::reverbDSP::combDamp(Flt value) {
  combDamp1 = value;
  combDamp2 = 1 - value;
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

    for (UInt i = 0; i < channel.size(); i++) channel[i].clear();
  }

  return (*this);
}

Bool YSE::INTERNAL::reverbDSP::bypass() {
  return _bypass;
}

void YSE::INTERNAL::reverbDSP::process(MULTICHANNELBUFFER & buffer) {
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

  for (ch = 0; ch < channel.size(); ch++) {
    Flt * ptr = buffer[ch].getPtr();
    channel[ch].out = 0;
    Flt * out = channel[ch].out.getPtr();
    Int length = buffer[ch].getLength();
    curCombIndex = channel[ch].combIndex;
    curBufComb = &channel[ch].bufComb;
    curFilterStore = channel[ch].filterStore;
    curCombTuning = channel[ch].combTuning;
    curBufAll = &channel[ch].bufAll;
    curAllIndex = channel[ch].allIndex;
    curAllTuning = channel[ch].allTuning;

    // update delay line
    channel[ch].delayline.process(buffer[ch]);
    for (Int i = 0; i < 4; i++) {
      if (channel[ch].earlyVolume[i].getValue() > 0) {
        channel[ch].delayline.read(channel[ch].early[i], channel[ch].earlyPtr[i]());
        channel[ch].early[i] *= channel[ch].earlyVolume[i]();
        buffer[ch] += channel[ch].early[i];
      }
    }

    for (; length > 7; length -= 8, ptr += 8, out += 8) {
      //ptr[0] = ptr[1] = ptr[2] = ptr[3] = ptr[4] = ptr[5] = ptr[6] = ptr[7] = 0;			
      for (filterIndex = 0; filterIndex < COMBS; filterIndex++) {
        bufferIndex = curCombIndex[filterIndex];

        out[0] += combProcess(ptr[0] * _gain);
        out[1] += combProcess(ptr[1] * _gain);
        out[2] += combProcess(ptr[2] * _gain);
        out[3] += combProcess(ptr[3] * _gain);
        out[4] += combProcess(ptr[4] * _gain);
        out[5] += combProcess(ptr[5] * _gain);
        out[6] += combProcess(ptr[6] * _gain);
        out[7] += combProcess(ptr[7] * _gain);
        curCombIndex[filterIndex] = bufferIndex;
      }

      for (filterIndex = 0; filterIndex < APASS; filterIndex++) {
        bufferIndex = curAllIndex[filterIndex];
        allpassProcess(out[0]);
        allpassProcess(out[1]);
        allpassProcess(out[2]);
        allpassProcess(out[3]);
        allpassProcess(out[4]);
        allpassProcess(out[5]);
        allpassProcess(out[6]);
        allpassProcess(out[7]);
        curAllIndex[filterIndex] = bufferIndex;
      }
    }

    while (length--) {
      // accumulate comb filters in parallel
      for (filterIndex = 0; filterIndex < COMBS; filterIndex++) {
        bufferIndex = curCombIndex[filterIndex];
        *out += combProcess(*ptr * _gain);
        curCombIndex[filterIndex] = bufferIndex;
      }

      // feed through allpass in series
      for (filterIndex = 0; filterIndex < APASS; filterIndex++) {
        bufferIndex = curAllIndex[filterIndex];
        allpassProcess(*out);
        curAllIndex[filterIndex] = bufferIndex;
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
    }
    else {
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
  }
  else {
    _roomsize1 = _roomsizeFader();
    _damp1 = _dampFader();
    _gain = static_cast<Flt>(fixedgain);
  }

  combFeedback(_roomsize1);
  combDamp(_damp1);
}

YSE::INTERNAL::reverbDSP& YSE::INTERNAL::reverbDSP::roomSize(Flt value) {
  _roomsizeFader.setIfNew((value*scaleroom) + offsetroom, 1000);
  return (*this);
}

Flt YSE::INTERNAL::reverbDSP::roomSize() {
  return (_roomsizeFader() - offsetroom) / scaleroom;
}

YSE::INTERNAL::reverbDSP& YSE::INTERNAL::reverbDSP::damp(Flt value) {
  _dampFader.setIfNew(value*scaledamp, 1000);
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
  if (channel.size() != value) channel.resize(value);
}

void YSE::INTERNAL::reverbDSP::modulate(Flt frequency, Flt width) {
  if (frequency < 0) frequency = 0;
  _modFrequency.setIfNew(frequency, 1000);
  _modWidth.setIfNew(width, 1000);
}

void YSE::INTERNAL::reverbDSP::set(YSE::reverb & params) {
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
      channel[ch].earlyPtr[i].setIfNew((float)(params.getReflectionTime(i) + channel[ch].earlyOffset), 1000);
      channel[ch].earlyVolume[i].setIfNew(params.getReflectionGain(i), 1000);
    }
  }
}

YSE::INTERNAL::reverbDSP::reverbDSP() {
  _freeze = false;

  // default values
  _modFrequency.set(0, 0);
  _modWidth.set(0, 0);
  _allpassFeedback = 0.5;
  wet(initialwet);
  roomSize(initialroom);
  dry(initialdry);
  damp(initialdamp);
  width(initialwidth);
  bypass(true); // set mute for buffer cleaning
  bypass(false); // extra call doesn't cost much time
}

YSE::INTERNAL::reverbDSP::~reverbDSP() {

}

YSE::INTERNAL::reverbChannel::reverbChannel() : delayline(3000), bufComb(COMBS), bufAll(APASS) {
  Int rnd = Random(50);
  // recalculate the reverb parameters in case we don't run at 44.1kHz
  for (Int i = 0; i < COMBS; i++) {
    combTuning[i] = (Int)((combtuning[i] + rnd) * (SAMPLERATE / 44100.0f));
  }

  for (int i = 0; i < APASS; i++) {
    allTuning[i] = (Int)((allpasstuning[i] + rnd) * (SAMPLERATE / 44100.0f));
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

YSE::INTERNAL::reverbChannel::reverbChannel(const reverbChannel& source): delayline(3000), bufComb(COMBS), bufAll(APASS) {
    Int rnd = Random(50);
  // recalculate the reverb parameters in case we don't run at 44.1kHz
  for (Int i = 0; i < COMBS; i++) {
    combTuning[i] = (Int)((combtuning[i] + rnd) * (SAMPLERATE / 44100.0f));
  }

  for (int i = 0; i < APASS; i++) {
    allTuning[i] = (Int)((allpasstuning[i] + rnd) * (SAMPLERATE / 44100.0f));
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



