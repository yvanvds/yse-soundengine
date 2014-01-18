#include "stdafx.h"
#include "filters.hpp"
#include <math.h>
#include "utils/misc.hpp"
#include "implementations.h"

YSE::DSP::filter::filter() : impl(new filterImpl) {
}

YSE::DSP::filter::~filter() {
  delete impl;
}


YSE::DSP::highPass& YSE::DSP::highPass::setFrequency(Flt f) {
	if (f < 0) f = 0;
	impl->freq = f;
	impl->coef1 = 1 - f * (2 * 3.14159) / sampleRate;
	Clamp(impl->coef1, 0, 1);

	return (*this);
}

SAMPLE YSE::DSP::highPass::operator()(SAMPLE in) {
	if (in.getLength() != impl->buffer.getLength()) impl->buffer.resize(in.getLength());

	Flt * inPtr = in.getBuffer();
	Flt * outPtr = impl->buffer.getBuffer();
	UInt length = in.getLength();
  Flt coef = impl->coef1;
	if (coef < 1) {
		for (UInt i = 0; i < length; i++) {
			Flt f = *inPtr++ + coef * impl->last;
			*outPtr++ = f - impl->last;
			impl->last = f;
		}
	} else {
		impl->last = 0;
		return in;
	}

	return impl->buffer;
}

/*******************************************************************************************/


YSE::DSP::lowPass& YSE::DSP::lowPass::setFrequency(Flt f) {
	if (f < 0) f = 0;
	impl->freq = f;
	impl->coef1 = f * (2 * 3.14159) / sampleRate;
  Clamp(impl->coef1, 0, 1);

	return (*this);
}

SAMPLE YSE::DSP::lowPass::operator()(SAMPLE in) {
	if (in.getLength() != impl->buffer.getLength()) impl->buffer.resize(in.getLength());

	Flt * inPtr = in.getBuffer();
	Flt * outPtr = impl->buffer.getBuffer();
	UInt length = in.getLength();

	Flt feedback = 1 - impl->coef1;
	for (UInt i = 0; i < length; i++) {
		impl->last = *outPtr++ = impl->coef1 * *inPtr++ + feedback * impl->last;
	}

	return impl->buffer;
}

/*******************************************************************************************/


YSE::DSP::bandPass& YSE::DSP::bandPass::set(Flt freq, Flt q) {
	impl->freq = freq;
	impl->q = q;
	calcCoef();
	return (*this);
}

YSE::DSP::bandPass& YSE::DSP::bandPass::setFrequency(Flt freq) {
	impl->freq = freq;
	calcCoef();
	return (*this);
}

YSE::DSP::bandPass& YSE::DSP::bandPass::setQ(Flt q) {
	impl->q = q;
	calcCoef();
	return (*this);
}

void YSE::DSP::bandPass::calcCoef() {
	Flt r, oneminusr, omega;
	if (impl->freq < 0.001) impl->freq = 10;
	if (impl->q < 0) impl->q = 0;
	omega = impl->freq * (2.0f * 3.14159f) / sampleRate;
	if (impl->q < 0.001) oneminusr = 1.0f;
	else oneminusr= omega/impl->q;
	if (oneminusr >  1.0f) oneminusr = 1.0f;
	r = 1.0f - oneminusr;
	impl->coef1 = 2.0f * qCos(omega) * r;
	impl->coef2 = -r * r;

	impl->gain = 2 * oneminusr * (oneminusr + r * omega);
}

float YSE::DSP::bandPass::qCos(Flt omega) {
	if (omega >= -(0.5 * 3.14159f) && omega <= 0.5f * 3.14159f) {
		Flt result = omega * omega;
		return (((result * result * result * (-1.0f/720.0f) + result * result * (1.0f / 24.0f)) - result * 0.5) + 1);
	} else return 0;
}

YSE::DSP::bandPass::bandPass() {
	calcCoef();
}

SAMPLE YSE::DSP::bandPass::operator()(SAMPLE in) {
	if (in.getLength() != impl->buffer.getLength()) impl->buffer.resize(in.getLength());

	Flt * inPtr = in.getBuffer();
	Flt * outPtr = impl->buffer.getBuffer();
	UInt length = in.getLength();

  Flt coef1 = impl->coef1;
  Flt coef2 = impl->coef2;

	for (Int i = 0; i < length; i++) {
		Flt output = *inPtr++ + coef1 * impl->last + coef2 * impl->previous;
		*outPtr++ = impl->gain * output;
		impl->previous = impl->last;
		impl->last = output;
	}

	return impl->buffer;
}

/*******************************************************************************************/

YSE::DSP::biQuad::biQuad() { 

}

YSE::DSP::biQuad& YSE::DSP::biQuad::setType	(BQ_TYPE type	) { 
  this->type = type; 
  calc(); 
  return (*this);
}

YSE::DSP::biQuad& YSE::DSP::biQuad::setFreq	(Flt frequency) { 
  impl->freq	= frequency; 
  calc(); 
  return (*this);
}

YSE::DSP::biQuad& YSE::DSP::biQuad::setQ		(Flt Q				) { 
  impl->q	= Q; 
  calc(); 
  return (*this);
}

YSE::DSP::biQuad& YSE::DSP::biQuad::setPeak	(Flt peakGain	) { 
  impl->gain = peakGain; 
  calc(); 
  return (*this);
}

YSE::DSP::biQuad& YSE::DSP::biQuad::set(BQ_TYPE type, Flt frequency, Flt Q, Flt peakGain) {
	this->type = type;
	impl->freq = frequency;
	impl->q = Q;
	impl->gain = peakGain;

	calc();
	return (*this);
}

YSE::DSP::biQuad& YSE::DSP::biQuad::setRaw(Flt fb1, Flt fb2, Flt ff1, Flt ff2, Flt ff3) {
	Flt discriminant = fb1 * fb1 + 4 * fb2;
	Bool zero = true;
	if (discriminant < 0) {
		if (fb2 >= -1.0f) zero = false;
	} else {
		if (fb1 <= 2.0f && fb1 >= -2.0f && 1.0f - fb1 - fb2 >= 0 && 1.0f + fb1 - fb2 >= 0) zero = false;
	}
	if (zero) fb1 = fb2 = ff1 = ff2 = ff3 = 0;
	impl->fb1 = fb1;
	impl->fb2 = fb2;
	impl->ff1 = ff1;
	impl->ff2 = ff2;
	impl->ff3 = ff3;

	return (*this);
}

SAMPLE YSE::DSP::biQuad::operator()(SAMPLE in) {
	if (in.getLength() != impl->buffer.getLength()) impl->buffer.resize(in.getLength()); 

	Flt * inPtr = in.getBuffer();
	Flt * outPtr = impl->buffer.getBuffer();
	UInt length = in.getLength();

	Flt output;
  Flt fb1 = impl->fb1;
  Flt fb2 = impl->fb2;
  Flt ff1 = impl->ff1;
  Flt ff2 = impl->ff2;
  Flt ff3 = impl->ff3;

	for (UInt i = 0; i < length; i++) {
		output = *inPtr++ + fb1 * impl->last + fb2 * impl->previous;
		*outPtr++ = ff1 * output + ff2 * impl->last + ff3 * impl->previous;
		impl->previous = impl->last;
		impl->last = output;
	}

	return impl->buffer;
}



void YSE::DSP::biQuad::calc() {
	Flt norm;
	Flt v = pow(10, abs(impl->gain.load()) / 20.0);
	Flt k = tan(Pi * impl->freq / (Flt)sampleRate);

	switch (type) {
		case BQ_LOWPASS: {
			norm = 1 / (1 + k / impl->q + k * k);
			impl->ff1 = k * k * norm;
			impl->ff2 = 2 * impl->ff1;
			impl->ff3 = impl->ff1.load();
			impl->fb1 = 2 * (k * k - 1) * norm;
			impl->fb2 = (1 - k / impl->q + k * k) * norm;
			break;
		}
		case BQ_HIGHPASS: {
			norm = 1 / (1 + k / impl->q + k * k);
			impl->ff1 = 1 * norm;
			impl->ff2 = -2 * impl->ff1;
			impl->ff3 = impl->ff1.load();
			impl->fb1 = 2 * (k * k - 1) * norm;
			impl->fb2 = (1 - k / impl->q + k * k) * norm;
			break;
		}
		case BQ_BANDPASS: {
			norm = 1 / (1 + k / impl->q + k * k);
			impl->ff1 = k / impl->q * norm;
			impl->ff2 = 0;
			impl->ff3 = -impl->ff1;
			impl->fb1 = 2 * (k * k - 1) * norm;
			impl->fb2 = (1 - k / impl->q + k * k) * norm;
			break;
		}
		case BQ_NOTCH: {
			norm = 1 / (1 + k / impl->q + k * k);
			impl->ff1 = (1 + k * k) * norm;
			impl->ff2 = 2 * (k * k - 1) * norm;
			impl->ff3 = impl->ff1.load();
			impl->fb1 = impl->ff2.load();
			impl->fb2 = (1 - k / impl->q + k * k) * norm;
			break;
		}
		case BQ_PEAK: {
			if (impl->gain >= 0) {
				norm = 1 / (1 + 1/impl->q * k + k * k);
				impl->ff1 = (1 + v/impl->q * k + k * k) * norm;
				impl->ff2 = 2 * (k * k - 1) * norm;
				impl->ff3 = (1 - v/impl->q * k + k * k) * norm;
        impl->fb1 = impl->ff2.load();
				impl->fb2 = (1 - 1/impl->q * k + k * k) * norm;
			} else {
				norm = 1 / (1 + v/impl->q * k + k * k);
				impl->ff1 = (1 + 1/impl->q * k + k * k) * norm;
				impl->ff2 = 2 * (k * k - 1) * norm;
				impl->ff3 = (1 - 1/impl->q * k + k * k) * norm;
				impl->fb1 = impl->ff2.load();
				impl->fb2 = (1 - v/impl->q * k + k * k) * norm;
			}
			break;
		}
		case BQ_LOWSHELF: {
			if (impl->gain >= 0) {
				norm = 1 / (1 + Sqrt2 * k + k * k);
				impl->ff1 = (1 + sqrt(2*v) * k + v * k * k) * norm;
				impl->ff2 = 2 * (v * k * k - 1) * norm;
				impl->ff3 = (1 - sqrt(2*v) * k + v * k * k) * norm;
				impl->fb1 = 2 * (k * k - 1) * norm;
				impl->fb2 = (1 - Sqrt2 * k + k * k) * norm;
			} else {	
				norm = 1 / (1 + sqrt(2*v) * k + v * k * k);
				impl->ff1 = (1 + Sqrt2 * k + k * k) * norm;
				impl->ff2 = 2 * (k * k - 1) * norm;
				impl->ff3 = (1 - Sqrt2 * k + k * k) * norm;
				impl->fb1 = 2 * (v * k * k - 1) * norm;
				impl->fb2 = (1 - sqrt(2*v) * k + v * k * k) * norm;
			}
			break;
		}
		case BQ_HIGHSHELF: {
			if (impl->gain >= 0) {
        norm = 1 / (1 + Sqrt2 * k + k * k);
				impl->ff1 = (v + sqrt(2*v) * k + k * k) * norm;
				impl->ff2 = 2 * (k * k - v) * norm;
				impl->ff3 = (v - sqrt(2*v) * k + k * k) * norm;
				impl->fb1 = 2 * (k * k - 1) * norm;
				impl->fb2 = (1 - Sqrt2 * k + k * k) * norm;
			}
			else {	
				norm = 1 / (v + sqrt(2*v) * k + k * k);
				impl->ff1 = (1 + Sqrt2 * k + k * k) * norm;
				impl->ff2 = 2 * (k * k - 1) * norm;
				impl->ff3 = (1 - Sqrt2 * k + k * k) * norm;
				impl->fb1 = 2 * (k * k - v) * norm;
				impl->fb2 = (v - sqrt(2*v) * k + k * k) * norm;
			}
			break;
	
		}
	}
	impl->fb1 = -impl->fb1;
	impl->fb2 = -impl->fb2;

	Flt discriminant = impl->fb1 * impl->fb1 + 4 * impl->fb2;
	Bool zero = true;
	if (discriminant < 0) {
		if (impl->fb2 >= -1.0f) zero = false;
	} else {
		if (impl->fb1 <= 2.0f && impl->fb1 >= -2.0f && 1.0f - impl->fb1 - impl->fb2 >= 0 && 1.0f + impl->fb1 - impl->fb2 >= 0) zero = false;
	}
	if (zero) impl->fb1 = impl->fb2 = impl->ff1 = impl->ff2 = impl->ff3 = 0;
}

/*******************************************************************************************/

YSE::DSP::sampleHold::sampleHold() {
  impl = new sHoldImpl;
	impl->lastIn = impl->lastOut = 0;
}

YSE::DSP::sampleHold::~sampleHold() {
  delete impl;
}

YSE::DSP::sampleHold& YSE::DSP::sampleHold::reset	(Flt value) { 
  impl->lastIn	= value; 
  return (*this); 
}

YSE::DSP::sampleHold& YSE::DSP::sampleHold::set		(Flt value) { 
  impl->lastOut = value; 
  return (*this); 
}

SAMPLE YSE::DSP::sampleHold::operator()(SAMPLE in, SAMPLE signal) {
	if (in.getLength() != impl->buffer.getLength()) impl->buffer.resize(in.getLength()); 

	Flt * inPtr = in.getBuffer();
	Flt * sigPtr = signal.getBuffer();
	Flt * outPtr = impl->buffer.getBuffer();
	UInt length = in.getLength();

  Flt lastIn = impl->lastIn;
  Flt lastOut = impl->lastOut;

	for (UInt i = 0; i < length; i++, *inPtr++) {
		if (*sigPtr < lastIn) lastOut = *inPtr;
		*outPtr++ = lastOut;
		lastIn = *sigPtr++;
	}

  impl->lastIn = lastIn;
  impl->lastOut = lastOut;

	return impl->buffer;
}



	



