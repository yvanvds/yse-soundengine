#include "stdafx.h"
#include <cmath>
#include "math.hpp"
#include "implementations.h"

Flt YSE::DSP::MidiToFreq(Flt note) { 
  return 440.0f * std::pow(2.0f, (note-69.0f) / 12.0f); 
}

Flt YSE::DSP::FreqToMidi(Flt freq) {
  return 69.0f + 12.0f * std::log(freq / 440.0f) / std::log(2.0f); 
}

/*******************************************************************************************/

YSE::DSP::clip::clip() : impl(new clipImpl) {}
YSE::DSP::clip::~clip() { delete impl; }


YSE::DSP::clip& YSE::DSP::clip::set(Flt low, Flt high) { 
  impl->low	= low;	
  impl->high = high;	
  return (*this); 
}

YSE::DSP::clip& YSE::DSP::clip::setLow	(Flt low					) { 
  impl->low	= low;											
  return (*this); 
}

YSE::DSP::clip& YSE::DSP::clip::setHigh	(					Flt high) {										
  impl->high = high;	
  return (*this);
}


SAMPLE YSE::DSP::clip::operator()(SAMPLE in) {
	if (in.getLength() != impl->buffer.getLength()) impl->buffer.resize(in.getLength());
	Flt * inPtr = in.getBuffer();
	Flt * outPtr = impl->buffer.getBuffer();
	UInt length = in.getLength();

  // TODO: is this needed? Is aFlt really slower as Flt?
  Flt min = impl->low;
  Flt max = impl->high;
	while (length--) {
		Flt f = *inPtr++;
		if (f < min) f = min;
		if (f > max) f = max;
		*outPtr++ = f;
	}

	return impl->buffer;
}

/*******************************************************************************************/

#define DUMTAB1SIZE 256
#define DUMTAB2SIZE 1024

Flt rsqrt_exptab[DUMTAB1SIZE], rsqrt_mantissatab[DUMTAB2SIZE];
Bool rSqrtInitOK = false;



void setupSqrt() {
	if (rSqrtInitOK) return;

	for (Int i = 0; i < DUMTAB1SIZE; i++) {
		Flt f;
		Int l = (i ? (i == DUMTAB1SIZE-1 ? DUMTAB1SIZE-2 : i) : 1)<< 23;
		*(Int *)(&f) = l;
		rsqrt_exptab[i] = 1.0f/sqrt(f);
	}

	for (Int i = 0; i < DUMTAB2SIZE; i++) {
		Flt f = 1 + (1.0f/DUMTAB2SIZE) * i;
		rsqrt_mantissatab[i] = 1.0f/sqrt(f);
	}

	rSqrtInitOK = true;
}

YSE::DSP::rSqrt::rSqrt() {
	setupSqrt();
}

SAMPLE YSE::DSP::rSqrt::operator()(SAMPLE in) {
	if (in.getLength() != buffer.getLength()) buffer.resize(in.getLength());
	Flt * inPtr = in.getBuffer();
	Flt * outPtr = buffer.getBuffer();
	UInt length = in.getLength();

	while (length--) {
		Flt f = *inPtr;
		Int l = *(Int *)(inPtr++);
		if (f < 0) *outPtr++ = 0;
		else {
			Flt g = rsqrt_exptab[(l >> 23) & 0xff] * rsqrt_mantissatab[(l >> 13) & 0x3ff];
			*outPtr++ = 1.5f * g - 0.5f * g * g * g * f;
		}
	}

	return buffer;
}

/*******************************************************************************************/

YSE::DSP::sqrt::sqrt() {
	setupSqrt();
}

SAMPLE YSE::DSP::sqrt::operator()(SAMPLE in) {
	if (in.getLength() != buffer.getLength()) buffer.resize(in.getLength());
	Flt * inPtr = in.getBuffer();
	Flt * outPtr = buffer.getBuffer();
	UInt length = in.getLength();

	while (length--) {
		Flt f = *inPtr;
		Int l = *(Int *)(inPtr++);
		if (f < 0) *outPtr++ = 0;
		else {
			Flt g = rsqrt_exptab[(l >> 23) & 0xff] * rsqrt_mantissatab[(l >> 13) & 0x3ff];
			*outPtr++ = f * (1.5f * g - 0.5f * g * g * g * f);
		}
	}

	return buffer;
}

/*******************************************************************************************/

SAMPLE YSE::DSP::wrap::operator()(SAMPLE in) {
	if (in.getLength() != buffer.getLength()) buffer.resize(in.getLength());
	Flt * inPtr = in.getBuffer();
	Flt * outPtr = buffer.getBuffer();
	UInt length = in.getLength();

	while (length--) {
		Flt f = *inPtr++;
		Int k = (Int)f;
		if (f > 0) *outPtr++ = f - k;
		else *outPtr++ = f - (k-1);
	}

	return buffer;
}

/*******************************************************************************************/

SAMPLE YSE::DSP::midiToFreq::operator()(SAMPLE in) {
	if (in.getLength() != buffer.getLength()) buffer.resize(in.getLength());
	Flt * inPtr = in.getBuffer();
	Flt * outPtr = buffer.getBuffer();
	UInt length = in.getLength();

	for (; length--; inPtr++, outPtr++) {
		Flt f = *inPtr;
		if (f < -1500) *outPtr = 0;
		else {
			if (f > 1499) f = 1499;
			*outPtr = 8.17579891564f * ::exp(.0577622650f * f);
		}
	}

	return buffer;
}

/*******************************************************************************************/

SAMPLE YSE::DSP::freqToMidi::operator()(SAMPLE in) {
	if (in.getLength() != buffer.getLength()) buffer.resize(in.getLength());
	Flt * inPtr = in.getBuffer();
	Flt * outPtr = buffer.getBuffer();
	UInt length = in.getLength();

	for (; length--; *inPtr++, *outPtr++) {
		Flt f = *inPtr;
		*outPtr = (f > 0 ? 17.3123405046f * ::log(.12231220585f * f) : -1500);
	}

	return buffer;
}


/*******************************************************************************************/

#define LOGTEN 2.302585092994f

SAMPLE YSE::DSP::dbToRms::operator()(SAMPLE in) {
	if (in.getLength() != buffer.getLength()) buffer.resize(in.getLength());
	Flt * inPtr = in.getBuffer();
	Flt * outPtr = buffer.getBuffer();
	UInt length = in.getLength();

	for (; length--; *inPtr++, *outPtr++) {
		Flt f = *inPtr;
		if (f <= 0) *outPtr = 0;
		else {
			if (f > 485) f = 485;
			*outPtr = ::exp((LOGTEN * 0.05f) * (f-100.0f));
		}
	}

	return buffer;
}

/*******************************************************************************************/

SAMPLE YSE::DSP::rmsToDb::operator()(SAMPLE in) {
	if (in.getLength() != buffer.getLength()) buffer.resize(in.getLength());
	Flt * inPtr = in.getBuffer();
	Flt * outPtr = buffer.getBuffer();
	UInt length = in.getLength();

	for (; length--; *inPtr++, *outPtr++) {
		Flt f = *inPtr;
		if (f <= 0) *outPtr = 0;
		else {
			Flt g = 100 + 20.0f/LOGTEN * ::log(f);
			*outPtr = (g < 0 ? 0 : g);
		}
	}

	return buffer;
}

/*******************************************************************************************/

SAMPLE YSE::DSP::dbToPow::operator()(SAMPLE in) {
	if (in.getLength() != buffer.getLength()) buffer.resize(in.getLength());
	Flt * inPtr = in.getBuffer();
	Flt * outPtr = buffer.getBuffer();
	UInt length = in.getLength();

	for (; length--; *inPtr++, *outPtr++) {
		Flt f = *inPtr;
		if (f <= 0) *outPtr = 0;
		else {
			if (f > 870) f = 870;
			*outPtr = ::exp((LOGTEN * 0.1f) * (f-100.0f));
		}
	}

	return buffer;
}

/*******************************************************************************************/

SAMPLE YSE::DSP::powToDb::operator()(SAMPLE in) {
	if (in.getLength() != buffer.getLength()) buffer.resize(in.getLength());
	Flt * inPtr = in.getBuffer();
	Flt * outPtr = buffer.getBuffer();
	UInt length = in.getLength();

	for (; length--; *inPtr++, *outPtr++) {
		Flt f = *inPtr;
		if (f <= 0) *outPtr = 0;
		else {
			Flt g = 100 + 10.0f/LOGTEN * ::log(f);
			*outPtr = (g < 0 ? 0 : g);
		}
	}

	return buffer;
}

/*******************************************************************************************/

SAMPLE YSE::DSP::pow::operator()(SAMPLE in1, SAMPLE in2) {
	if (in1.getLength() != buffer.getLength()) buffer.resize(in1.getLength());
	Flt * in1Ptr = in1.getBuffer();
	Flt * in2Ptr = in2.getBuffer();
	Flt * outPtr = buffer.getBuffer();
	UInt length = in1.getLength();

	while (length--) {
		Flt f = *in1Ptr++;
		if (f > 0) *outPtr = ::pow(f, *in2Ptr);
		else *outPtr = 0;
		outPtr++;
		in2Ptr++;
	}

	return buffer;
}


/*******************************************************************************************/

SAMPLE YSE::DSP::exp::operator()(SAMPLE in) {
	if (in.getLength() != buffer.getLength()) buffer.resize(in.getLength());
	Flt * inPtr = in.getBuffer();
	Flt * outPtr = buffer.getBuffer();
	UInt length = in.getLength();

	while (length--) *outPtr++ = ::exp(*inPtr++);

	return buffer;
}

/*******************************************************************************************/

SAMPLE YSE::DSP::log::operator()(SAMPLE in1, SAMPLE in2) {
	if (in1.getLength() != buffer.getLength()) buffer.resize(in1.getLength());
	Flt * in1Ptr = in1.getBuffer();
	Flt * in2Ptr = in2.getBuffer();
	Flt * outPtr = buffer.getBuffer();
	UInt length = in1.getLength();

	while (length--) {
		Flt f = *in1Ptr++, g = *in2Ptr++;
		if (f <= 0) *outPtr = ::log(f);
		else if (g <= 0) *outPtr = ::log(f); 
		else *outPtr = ::log(f)/::log(g);
		outPtr++;
	}

	return buffer;
}


/*******************************************************************************************/

SAMPLE YSE::DSP::abs::operator()(SAMPLE in) {
	if (in.getLength() != buffer.getLength()) buffer.resize(in.getLength());
	Flt * inPtr = in.getBuffer();
	Flt * outPtr = buffer.getBuffer();
	UInt length = in.getLength();

	while (length--) {
		Flt f = *inPtr++;
		*outPtr++ = (f >= 0 ? f : -f);
	}

	return buffer;
}