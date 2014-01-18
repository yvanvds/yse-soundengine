#include "stdafx.h"
#include "delay.hpp"
#include "utils/memory.hpp"
#include "implementations.h"




YSE::DSP::delay::delay() : impl(new delayImpl(0)) {}

YSE::DSP::delay::~delay() {
  delete impl;
}

YSE::DSP::delay& YSE::DSP::delay::set(UInt size) {
	impl->size = size;
	return (*this);
}

YSE::DSP::delay& YSE::DSP::delay::update(SAMPLE s) {
	{
		Int nsamps = (Int)(impl->size * sampleRate * 0.001f);
		if (nsamps < 1) nsamps = 1;
		nsamps += ((- nsamps) & (SAMPBLK - 1));
		nsamps += DEFDELVS;
		if (impl->bufferlength != nsamps) {
			ReallocZero(&impl->buffer, impl->bufferlength, (nsamps + XTRASAMPS));
			impl->bufferlength = nsamps;
			impl->phase = XTRASAMPS;
		}
	}

	impl->currentLength = s.getLength();
	Flt * in = s.getBuffer();
	UInt length = impl->currentLength;
	Int ph = impl->phase;
	Flt * vp = impl->buffer;
	Flt * bp = vp + ph;
	Flt * ep = vp + impl->bufferlength + XTRASAMPS;
	ph += impl->currentLength;
	
	while (length--) {
		Flt f = *in++;
		*bp++ = f;
		if (bp == ep) {
			vp[0] = ep[-4];
			vp[1] = ep[-3];
			vp[2] = ep[-2];
			vp[3] = ep[-1];
			bp = vp + XTRASAMPS;
			ph -= impl->bufferlength;
		}
	}
	bp = vp + impl->phase;
	impl->phase = ph;

	return (*this);
}

YSE::DSP::delay& YSE::DSP::delay::get(sample& s, UInt delayTime) {
	if (s.getLength() < impl->currentLength) s.resize(impl->currentLength);
	UInt delaySamples = (UInt)(sampleRate * delayTime * 0.001f) + impl->currentLength;
	if (delaySamples < s.getLength()) delaySamples = s.getLength();
	else if (delaySamples > impl->bufferlength - DEFDELVS) delaySamples = impl->bufferlength - DEFDELVS;

	Int ph = impl->phase - delaySamples;

	Flt * vp = impl->buffer;
	Flt * ep = vp + (impl->bufferlength + XTRASAMPS);
	if (ph < 0) ph += impl->bufferlength;
	Flt *bp = vp + ph;

	Flt * out = s.getBuffer();

	Int length = impl->currentLength;
	while (length--) {
		*out++ = *bp++;
		if (bp == ep) bp -= impl->bufferlength;
	}

	return (*this);
}

YSE::DSP::delay& YSE::DSP::delay::get(sample& s, SAMPLE delayTime) {
	if (s.getLength() < impl->currentLength) s.resize(impl->currentLength);

  Flt * ctrl = delayTime.getBuffer();
  Flt * out = s.getBuffer();

  Flt * vp = impl->buffer;
	//Flt * ep = vp + (impl->bufferlength + XTRASAMPS);

  for (UInt i = 0; i < s.getLength(); i++) {
    UInt delaySamples = (UInt)(sampleRate * ctrl[i] * 0.001f) + impl->currentLength;
	  if (delaySamples < s.getLength()) delaySamples = s.getLength();
	  else if (delaySamples > impl->bufferlength - DEFDELVS) delaySamples = impl->bufferlength - DEFDELVS;

	  Int ph = impl->phase - delaySamples + i;
    if (ph < 0) ph += impl->bufferlength;
	  Flt *bp = vp + ph;
		*out++ = *bp;
	}

	return (*this);
}


void YSE::DSP::readInterpolated(SAMPLE ctrl, YSE::DSP::sample& out, SAMPLE buffer, UInt &pos) {
	Flt * input = ctrl.getBuffer();
	Flt * output = out.getBuffer();
	Int n = ctrl.getLength();
	Int nsamps = buffer.getLength();
	Flt limit = nsamps - n - 1.0f;
	Flt fn = n - 1.0f;

	Flt * vp = buffer.getBuffer();
	Flt *bp;
	Flt *wp = vp + pos;

	while (n--) {
		Flt delsamps = sampleRate * *input++, frac;
		Int idelsamps;
		Flt a, b, c, d, cminusb;
		if (delsamps < 1.00001f) delsamps = 1.00001f;
		if (delsamps > limit) delsamps = limit;
		delsamps += fn;
		fn = fn - 1.0f;
		idelsamps = (Int)delsamps;
		frac = delsamps - (Flt)idelsamps;
		bp = wp - idelsamps;
		if (bp < vp + 4) bp += nsamps;
		d = bp[-3];
		c = bp[-2];
		b = bp[-1];
		a = bp[0];
		cminusb = c-b;
		*output++ = b + frac * (
			cminusb - 0.1666667f * (1.0f-frac) * (
				(d - a - 3.0f * cminusb) * frac + (d + 2.0f * a - 3.0f * b)
			)
		);

	}
	pos = (UInt)(bp - vp);
}