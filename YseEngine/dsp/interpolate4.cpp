/*
  ==============================================================================

    interpolate4.cpp
    Created: 4 Sep 2015 11:53:08am
    Author:  yvan

  ==============================================================================
*/

#include "interpolate4.hpp"
#include "../utils/misc.hpp"

YSE::DSP::interpolate4::interpolate4() : data(NULL), parmOnset(0) {}

YSE::DSP::interpolate4 & YSE::DSP::interpolate4::source(YSE::DSP::buffer & data) {
  this->data = &data;
  return *this;
}

YSE::DSP::buffer * YSE::DSP::interpolate4::source() {
  return data;
}

YSE::DSP::interpolate4 & YSE::DSP::interpolate4::onset(Int value) {
  if (data) {
    Clamp(value, 0, data->getLength() - 1);
    parmOnset.store(value);
  }
  return *this;
}

Int YSE::DSP::interpolate4::onset() {
  return parmOnset;
}

YSE::DSP::buffer & YSE::DSP::interpolate4::operator()(YSE::DSP::buffer & in) {
  Int n = in.getLength();
  Flt * inData = in.getPtr();
  Flt * outData = out.getPtr();
  Flt * wp;
  Int onset = parmOnset.load();
  
  if (!data) goto zero;

  int maxindex;
  maxindex = data->getLength() - 3;
  if (maxindex < 0) goto zero;

  for (int i = 0; i < n; i++) {
    double findex = *inData++ + onset;
    int index = (int)findex;
    Flt frac, a, b, c, d, cminusb;

    if (index < 1) index = 1, frac = 0;
    else if (index > maxindex) index = maxindex, frac = 1;
    else frac = (float)findex - index;
    wp = data->getPtr() + index;
    a = wp[-1];
    b = wp[0];
    c = wp[1];
    d = wp[2];
    cminusb = c - b;
    *outData++ = b + frac * (
      cminusb - 0.1666667f * (1.f - frac) * (
        (d - a - 3.f * cminusb) * frac + (d + 2.f*a - 3.f*b)
      )
    );
  
  }
  return out;

zero:
  while (n--) *outData++ = 0;
  return out;
}


