/*
  ==============================================================================

    delay.cpp
    Created: 31 Jan 2014 2:52:41pm
    Author:  yvan

  ==============================================================================
*/

#include "delay.hpp"

#define XTRASAMPS 4
#define DEFDELVS 64
#define SAMPBLK 4

YSE::DSP::delay::delay(Int size) : bufferlength(size), buffer(size + XTRASAMPS), size(size) {}
YSE::DSP::delay::delay(const YSE::DSP::delay & source) {
  delay(source.size);
}

YSE::DSP::delay& YSE::DSP::delay::setSize(UInt size) {
  this->size = size;
  return (*this);
}

YSE::DSP::delay& YSE::DSP::delay::process(YSE::DSP::buffer & s) {
  {
    Int nsamps = (Int)(this->size * SAMPLERATE * 0.001f);
    if (nsamps < 1) nsamps = 1;
    nsamps += ((-nsamps) & (SAMPBLK - 1));
    nsamps += DEFDELVS;
    if ((Int)bufferlength != nsamps) {
      buffer.resize(nsamps + XTRASAMPS, 0.f);
      bufferlength = nsamps;
      phase = XTRASAMPS;
    }
  }

  currentLength = s.getLength();
  Flt * in = s.getPtr();
  UInt length = currentLength;
  Int ph = phase;
  Flt * vp = buffer.data();
  Flt * bp = vp + ph;
  Flt * ep = vp + bufferlength + XTRASAMPS;
  ph += currentLength;

  while (length--) {
    Flt f = *in++;
    *bp++ = f;
    if (bp == ep) {
      vp[0] = ep[-4];
      vp[1] = ep[-3];
      vp[2] = ep[-2];
      vp[3] = ep[-1];
      bp = vp + XTRASAMPS;
      ph -= bufferlength;
    }
  }
  bp = vp + phase;
  phase = ph;

  return (*this);
}

YSE::DSP::delay& YSE::DSP::delay::read(YSE::DSP::buffer & result, UInt delayTime) {
  if (result.getLength() < currentLength) result.resize(currentLength);
  UInt delaySamples = (UInt)(SAMPLERATE * delayTime * 0.001f) + currentLength;
  if (delaySamples < result.getLength()) delaySamples = result.getLength();
  else if (delaySamples > bufferlength - DEFDELVS) delaySamples = bufferlength - DEFDELVS;

  Int ph = phase - delaySamples;

  Flt * vp = buffer.data();
  Flt * ep = vp + (bufferlength + XTRASAMPS);
  if (ph < 0) ph += bufferlength;
  Flt *bp = vp + ph;

  Flt * out = result.getPtr();

  Int length = currentLength;
  while (length--) {
    *out++ = *bp++;
    if (bp == ep) bp -= bufferlength;
  }

  return (*this);
}

YSE::DSP::delay& YSE::DSP::delay::read(YSE::DSP::buffer & result, YSE::DSP::buffer & delayTime) {
  if (result.getLength() < currentLength) result.resize(currentLength);

  Flt * ctrl = delayTime.getPtr();
  Flt * out = result.getPtr();

  Flt * vp = buffer.data();
  //Flt * ep = vp + (impl->bufferlength + XTRASAMPS);

  for (UInt i = 0; i < result.getLength(); i++) {
    UInt delaySamples = (UInt)(SAMPLERATE * ctrl[i] * 0.001f) + currentLength;
    if (delaySamples < result.getLength()) delaySamples = result.getLength();
    else if (delaySamples > bufferlength - DEFDELVS) delaySamples = bufferlength - DEFDELVS;

    Int ph = phase - delaySamples + i;
    if (ph < 0) ph += bufferlength;
    Flt *bp = vp + ph;
    *out++ = *bp;
  }

  return (*this);
}


void YSE::DSP::readInterpolated(YSE::DSP::buffer & ctrl, YSE::DSP::buffer& out, YSE::DSP::buffer & buffer, UInt &pos) {
  Flt * input = ctrl.getPtr();
  Flt * output = out.getPtr();
  Int n = ctrl.getLength();
  Int nsamps = buffer.getLength();
  Flt limit = nsamps - n - 1.0f;
  Flt fn = n - 1.0f;

  Flt * vp = buffer.getPtr();
  Flt *bp = nullptr;
  Flt *wp = vp + pos;

  while (n--) {
    Flt delsamps = SAMPLERATE * *input++, frac;
    Int idelsamps;
    Flt a, b, c, d, cminusb;
    if (delsamps < 1.00001f) delsamps = 1.00001f;
    if (delsamps > limit) delsamps = limit;
    delsamps += fn;
    fn = fn - 1.0f;
    idelsamps = (Int)delsamps;
    frac = delsamps - static_cast<Flt>(idelsamps);
    bp = wp - idelsamps;
    if (bp < vp + 4) bp += nsamps;
    d = bp[-3];
    c = bp[-2];
    b = bp[-1];
    a = bp[0];
    cminusb = c - b;
    *output++ = b + frac * (
      cminusb - 0.1666667f * (1.0f - frac) * (
      (d - a - 3.0f * cminusb) * frac + (d + 2.0f * a - 3.0f * b)
      )
      );

  }
  pos = (UInt)(bp - vp);
}