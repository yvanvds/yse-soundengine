/*
  ==============================================================================

    fft.cpp
    Created: 27 Jul 2015 2:04:09pm
    Author:  yvan

  ==============================================================================
*/

#include <cassert>
#include "fft.hpp"
#include "mayer.h"
#include "../math_functions.h"

// flip function for real fft
static void flip(int length, Flt * in, Flt * out) {
  while (length--) *(--out) = -*in++;
}

// zero function for real fft
static void zero(Flt * in, int length) {
  for (; length > 7; length -= 8, in += 8) {
    in[0] = 0; in[1] = 0; in[2] = 0; in[3] = 0;
    in[4] = 0; in[5] = 0; in[6] = 0; in[7] = 0;
  }

  while (length--) *in++ = 0;
}

/******************************************************************
** fft
*******************************************************************/

YSE::DSP::buffer & YSE::DSP::fft::getReal() {
  return real;
}

YSE::DSP::buffer & YSE::DSP::fft::getImaginary() {
  return imaginary;
}

YSE::DSP::buffer & YSE::DSP::fft::operator()(YSE::DSP::buffer & realIn, YSE::DSP::buffer & imaginaryIn) {
  if (realIn.getLength() != imaginaryIn.getLength()) {
    assert(false);
  }

  if (realIn.getLength() != real.getLength()) real.resize(realIn.getLength());
  if (imaginaryIn.getLength() != imaginary.getLength()) imaginary.resize(imaginaryIn.getLength());

  if (real.getPtr() == imaginaryIn.getPtr() && imaginary.getPtr() == realIn.getPtr()) {
    real.swap(imaginary);
  }
  else if (real.getPtr() == imaginaryIn.getPtr()) {
    real = realIn;
    imaginary = imaginaryIn;
  }
  else {
    if (real.getPtr() != realIn.getPtr()) real = realIn;
    if (imaginary.getPtr() != imaginaryIn.getPtr()) imaginary = imaginaryIn;
  }

  mayer_fft(real.getLength(), real.getPtr(), imaginary.getPtr());

  return real;
}

/******************************************************************
** inverseFft
*******************************************************************/

YSE::DSP::buffer & YSE::DSP::inverseFft::getReal() {
  return real;
}

YSE::DSP::buffer & YSE::DSP::inverseFft::getImaginary() {
  return imaginary;
}

YSE::DSP::buffer & YSE::DSP::inverseFft::operator()(YSE::DSP::buffer & realIn, YSE::DSP::buffer & imaginaryIn) {
  if (realIn.getLength() != imaginaryIn.getLength()) {
    assert(false);
  }

  if (realIn.getLength() != real.getLength()) real.resize(realIn.getLength());
  if (imaginaryIn.getLength() != imaginary.getLength()) imaginary.resize(imaginaryIn.getLength());
  
  if (real.getPtr() == imaginaryIn.getPtr() && imaginary.getPtr() == realIn.getPtr()) {
    real.swap(imaginary);
  }
  else if (real.getPtr() == imaginaryIn.getPtr()) {
    real = realIn;
    imaginary = imaginaryIn;
  }
  else {
    if (real.getPtr() != realIn.getPtr()) real = realIn;
    if (imaginary.getPtr() != imaginaryIn.getPtr()) imaginary = imaginaryIn;
  }

  mayer_ifft(real.getLength(), real.getPtr(), imaginary.getPtr());

  return real;
}

/******************************************************************
** realFft
*******************************************************************/

YSE::DSP::buffer & YSE::DSP::realFft::getReal() {
  return real;
}

YSE::DSP::buffer & YSE::DSP::realFft::getImaginary() {
  return imaginary;
}

YSE::DSP::buffer & YSE::DSP::realFft::operator()(YSE::DSP::buffer & in) {

  if (in.getLength() != real.getLength()) real.resize(in.getLength());
  if (in.getLength() != imaginary.getLength()) imaginary.resize(in.getLength());

  int n = in.getLength();
  int n2 = (n >> 1);

  if (n < 4) {
    assert(false); // minimum length is 4
    return real;
  }

  if (in.getPtr() != real.getPtr()) {
    real = in;
  }
  mayer_realfft(real.getLength(), real.getPtr());
  flip(n2 - 1, real.getPtr() + (n2 + 1), imaginary.getPtr() + n2);
  zero(real.getPtr() + (n2 + 1), ((n2 - 1)&(~7)));
  zero(real.getPtr() + (n2 + 1) + ((n2 - 1)&(~7)), ((n2 - 1) & 7));
  zero(imaginary.getPtr() + n2, n2);
  zero(imaginary.getPtr(), 1);

  return real;
}

/******************************************************************
** inverseRealFft
*******************************************************************/

YSE::DSP::buffer & YSE::DSP::inverseRealFft::getReal() {
  return real;
}

YSE::DSP::buffer & YSE::DSP::inverseRealFft::operator()(YSE::DSP::buffer & realIn, YSE::DSP::buffer & imaginaryIn) {
  if (realIn.getLength() != imaginaryIn.getLength()) {
    assert(false);
  }

  if (realIn.getLength() != real.getLength()) real.resize(realIn.getLength());

  int n = realIn.getLength();
  int n2 = (n >> 1);

  if (n < 4) {
    assert(false); // minimum length is 4
    return real;
  }

  if (imaginaryIn.getPtr() == real.getPtr()) {
    flip(n2 - 1, real.getPtr() + 1, real.getPtr() + n);
    real.copyFrom(realIn, 0, 0, n2 + 1);
  }
  else {
    if (realIn.getPtr() != real.getPtr()) real.copyFrom(realIn, 0, 0, n2 + 1);
    flip(n2 - 1, imaginaryIn.getPtr() + 1, real.getPtr() + n);
  }

  mayer_realifft(real.getLength(), real.getPtr());

  return real;
}

/******************************************************************
** fftStats
*******************************************************************/

YSE::DSP::buffer & YSE::DSP::fftStats::getFrequencies() {
  return frequencies;
}

YSE::DSP::buffer & YSE::DSP::fftStats::getAmplitudes() {
  return amplitudes;
}

void YSE::DSP::fftStats::operator()(YSE::DSP::buffer & real, YSE::DSP::buffer & imaginary) {
  if (real.getLength() != imaginary.getLength()) {
    assert(false);
  }

  int n = real.getLength();
  int n2 = (n >> 1);

  if (n < 4) {
    assert(false);
    return;
  }

  Flt * inReal = real.getPtr();
  Flt * inImag = imaginary.getPtr();
  Flt * freq = frequencies.getPtr();
  Flt * amp = amplitudes.getPtr();

  Flt lastReal = 0, currentReal = inReal[0], nextReal = inReal[1];
  Flt lastImag = 0, currentImag = inImag[0], nextImag = inImag[1];
  int m = n2 + 1;
  Flt fbin = 1, oneOverN2 = 1.f / ((Flt)n2 * (Flt)n2);

  inReal += 2;
  inImag += 2;

  *amp++ = *freq++ = 0;

  n2 -= 2;

  while (n2--) {
    Flt re, im, pow, fr;
    
    lastReal = currentReal;
    currentReal = nextReal;
    nextReal = *inReal++;

    lastImag = currentImag;
    currentImag = nextImag;
    nextImag = *inImag++;

    re = currentReal - 0.5f * (lastReal + nextReal);
    im = currentImag - 0.5f * (lastImag + nextImag);
    pow = re * re + im * im;

    if (pow > 1e-19) {
      Flt detune = ((lastReal + nextReal) * re + (lastImag - nextImag) * im) / (2.f * pow);
      if (detune > 2 || detune < -2) fr = pow = 0;
      else fr = fbin + detune;
    }
    else {
      fr = pow = 0;
    }

    *freq++ = fr;
    *amp++ = oneOverN2 * pow;
    fbin += 1.f;
  }
  while (m--) *amp++ = *freq++ = 0;

  YSE::DSP::sqrtFunc(frequencies.getPtr(), frequencies.getPtr(), n >> 1);
}