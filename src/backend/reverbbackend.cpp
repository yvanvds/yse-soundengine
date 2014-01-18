#include "stdafx.h"
#include "reverbbackend.h"
#include "boost/math/special_functions/fpclassify.hpp"
#include "utils/misc.hpp"



YSE::reverbBackend YSE::ReverbBackend;

#define LOGTEN 2.302585092994

#define	muted					0
#define	fixedgain			0.015
#define scalewet			3.0
#define scaledry			2.0
#define scaledamp			0.4
#define scaleroom			0.28
#define offsetroom		0.7
#define initialroom		0.5
#define initialdamp		0.5
#define initialwet		1.0/scalewet
#define initialdry		0.0
#define initialwidth	1.0
#define initialmode		0
#define initialbypass 0
#define freezemode		0.5
#define	stereospread	23

inline void Undenormal(Flt &v) {
  if (!(boost::math::isnormal)(v)) v = 0;
}

/* these values assume 44.1KHz sample rate
   they will probably be OK for 48KHz sample rate
   but would need scaling for 96KHz (or other) sample rates.
   the values were obtained by listening tests.                */
static const int combtuning[8] = { 1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617 };
static const int allpasstuning[4] = { 556, 441, 341, 225 };


// static inline functions and pointer for speed optimisation
Int ch;
Int filterIndex;
Int * curCombIndex;
Int * curCombTuning;
Flt ** curBufComb;
Flt * curFilterStore;
Flt ** curBufAll;
Int * curAllIndex;
Int * curAllTuning;
Flt _allpassFeedback;
Flt _combFeedback;
Flt combDamp1;
Flt combDamp2;
Int bufferIndex;

static inline Flt combProcess(Flt input) {
	Flt output;
	output = curBufComb[filterIndex][bufferIndex];
	Undenormal(output);

	curFilterStore[filterIndex] = (output * combDamp2) + (curFilterStore[filterIndex] * combDamp1);
	Undenormal(curFilterStore[filterIndex]);
	
	curBufComb[filterIndex][bufferIndex] = input + (curFilterStore[filterIndex] * _combFeedback);
	if (++bufferIndex >= curCombTuning[filterIndex]) bufferIndex = 0;
	return output;
}

static inline void allpassProcess(Flt & input) {
	Flt bufout;
	
	bufout = curBufAll[filterIndex][bufferIndex];
	Undenormal(bufout);

	curBufAll[filterIndex][bufferIndex] = input + (bufout * _allpassFeedback);
	if (++bufferIndex >= curAllTuning[filterIndex]) bufferIndex = 0;

	input = -input + bufout;
}


void YSE::reverbBackend::combDamp(Flt value) {
	combDamp1 = value;
	combDamp2 = 1 - value;
}

YSE::reverbBackend& YSE::reverbBackend::combFeedback(Flt value) {
	_combFeedback = value;
	return (*this);
}

Flt YSE::reverbBackend::combFeedback() {
	return _combFeedback ;
}

YSE::reverbBackend& YSE::reverbBackend::allpassFeedback(Flt value) {
	_allpassFeedback = value;
	return (*this);
}

Flt YSE::reverbBackend::allpassFeedback() {
	return _allpassFeedback ;
}

YSE::reverbBackend& YSE::reverbBackend::bypass(Bool value) {
	_bypass = value;
	if (_bypass) {
		if (_freeze) return (*this);

		for (Int i = 0; i < numChannels; i++) channel[i].clear();
	}

	return (*this);
}

Bool YSE::reverbBackend::bypass() {
	return _bypass;
}

void YSE::reverbBackend::process(BUFFER buffer) {
	if (_bypass) return;
	if (channel == NULL) return;
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

	for (ch = 0; ch < numChannels; ch++) {
		Flt * ptr = buffer[ch].getBuffer();
		channel[ch].out = 0;
		Flt * out = channel[ch].out.getBuffer();
		Int length = buffer[ch].getLength();
		curCombIndex = channel[ch].combIndex;
		curBufComb = channel[ch].bufComb;
		curFilterStore = channel[ch].filterStore;
		curCombTuning = channel[ch].combTuning;
		curBufAll = channel[ch].bufAll;
		curAllIndex = channel[ch].allIndex;
		curAllTuning = channel[ch].allTuning;

		// update delay line
		channel[ch].delayline.update(buffer[ch]);
		for (Int i = 0; i < 4; i++) {
      if (channel[ch].earlyVolume[i].getValue() > 0) {
				channel[ch].delayline.get(channel[ch].early[i], channel[ch].earlyPtr[i]());
				channel[ch].early[i] *= channel[ch].earlyVolume[i]();
				buffer[ch] += channel[ch].early[i];
			}
		}

		for ( ; length > 7; length -= 8, ptr += 8, out += 8) {
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

		while(length--) {
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
		} else {
			buffer[ch] *= _dryFader();
			channel[ch].out *= _wet1;
			buffer[ch] += channel[ch].out;
		}
	}
}

void YSE::reverbBackend::update() {
  _roomsizeFader.update();
  _dampFader.update();
  _wetFader.update();
  _dryFader.update();
  _widthFader.update();
  _modFrequency.update();
  _modWidth.update();
  for (Int i = 0; i < numChannels; i++) {
    channel[i].update();
  }

	_wet1 = _wetFader() * (_widthFader() / 2 + 0.5);
	_wet2 = _wetFader() * ((1 - _widthFader()) / 2);

	if (_freeze) {
		_roomsize1 = 1.;
		_damp1 = 0.;
		_gain = muted;
	} else {
		_roomsize1 = _roomsizeFader();
		_damp1 = _dampFader();
		_gain = (Flt)fixedgain;
	}

	combFeedback(_roomsize1);
	combDamp(_damp1);
}

YSE::reverbBackend& YSE::reverbBackend::roomSize(Flt value) {
  _roomsizeFader.setIfNew((value*scaleroom) + offsetroom, 1000);
	return (*this);
}

Flt YSE::reverbBackend::roomSize() {
	return (_roomsizeFader() - offsetroom) / scaleroom;
}

YSE::reverbBackend& YSE::reverbBackend::damp(Flt value) {
  _dampFader.setIfNew(value*scaledamp, 1000);
	return (*this);
}

Flt YSE::reverbBackend::damp() {
	return _dampFader() / scaledamp;
}

YSE::reverbBackend& YSE::reverbBackend::wet(Flt value) {
	_wetFader.setIfNew(value * scalewet, 1000);
	return (*this);
}

Flt YSE::reverbBackend::wet() {
	return _wetFader() / scalewet;
}

YSE::reverbBackend& YSE::reverbBackend::dry(Flt value) {
  _dryFader.setIfNew(value * scaledry, 1000);
	return (*this);
}

Flt YSE::reverbBackend::dry() {
	return _dryFader() / scaledry;
}

YSE::reverbBackend& YSE::reverbBackend::width(Flt value) {
	_widthFader.setIfNew(value, 1000);
	return (*this);
}

Flt YSE::reverbBackend::width() {
	return _widthFader();
}

YSE::reverbBackend& YSE::reverbBackend::freeze(Bool value) {
	_freeze = value;
	return (*this);
}

Bool YSE::reverbBackend::freeze() {
	return _freeze;
}

void YSE::reverbBackend::channels(Int value) {
	if (channel != NULL) delete[] channel;
	channel = new reverbChannel[value];
	numChannels = value;
}

YSE::reverbBackend::reverbBackend() {
	_freeze = false;
	channel = NULL;
	numChannels = 0;

	// default values
	_modFrequency.set(0,0);
	_modWidth.set(0,0);
	_allpassFeedback = 0.5;
	wet(initialwet);
	roomSize(initialroom);
	dry(initialdry);
	damp(initialdamp);
	width(initialwidth);
	bypass(true); // set mute for buffer cleaning
	bypass(false); // extra call doesn't cost much time
}

YSE::reverbBackend::~reverbBackend() {
	if (channel != NULL) delete[] channel;
  channel = NULL;
}

YSE::reverbChannel::reverbChannel() {
	Int rnd = Random(50);
	// recalculate the reverb parameters in case we don't run at 44.1kHz
	for (Int i = 0; i < COMBS; i++) {
		combTuning[i] = (Int)((combtuning[i]+rnd) * (sampleRate / 44100.0f));
	}

	for (int i = 0; i < APASS; i++) {
		allTuning[i] = (Int)((allpasstuning[i]+rnd) * (sampleRate / 44100.0f));
	}

	// get memory for delay lines
	for (Int i = 0; i < COMBS; i++) {
		bufComb[i] = new Flt[combTuning[i]];
		combIndex[i] = 0;
	}

	for (Int i = 0; i < APASS; i++) {
		bufAll[i] = new Flt[allTuning[i]];
		allIndex[i] = 0;
	}
	delayline.set(3000);

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

YSE::reverbChannel::~reverbChannel() {
	// release memory
	for (Int i = 0; i < APASS; i++) {
    if (bufAll[i] != NULL) delete[] bufAll[i];
    bufAll[i] = NULL;
  }
	for (Int i = 0; i < COMBS; i++) {
    if (bufComb[i] != NULL) delete[] bufComb[i];
    bufComb[i] = NULL;
  }
}

void YSE::reverbChannel::clear() {
	for (Int i = 0; i < COMBS; i++) {
		memset(bufComb[i], 0x0, combTuning[i] * sizeof(Flt));
		filterStore[i] = 0.0;
	}

	for (Int i = 0; i < APASS; i++) {
		memset(bufAll[i], 0x0, allTuning[i] * sizeof(Flt));
	}
}

void YSE::reverbChannel::update() {
  for (Int i = 0; i < 4; i++) {
    earlyPtr[i].update();
    earlyVolume[i].update();
  }
}


void YSE::reverbBackend::modulate(Flt frequency, Flt width) {
  if (frequency < 0) frequency = 0;
  _modFrequency.setIfNew(frequency, 1000);
  _modWidth.setIfNew(width, 1000);
}

void YSE::reverbBackend::set(YSE::reverbimpl & params) {
	if (!params._active) {
		bypass(true);
		return;
	}

	bypass(false);
	roomSize(params._roomsize);
	damp(params._damp);
	wet(params._wet);
	dry(params._dry);
	width(0);
	modulate(params._modFrequency, params._modWidth);

	for (Int i = 0; i < 4; i++) {
		for (Int ch = 0; ch < numChannels; ch++) {
      channel[ch].earlyPtr[i].setIfNew(params._earlyPtr[i] + channel[ch].earlyOffset, 1000);
			channel[ch].earlyVolume[i].setIfNew(params._earlyGain[i], 1000);
		}
	}
}
