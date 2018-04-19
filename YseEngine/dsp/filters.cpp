/*
  ==============================================================================

    filters.cpp
    Created: 31 Jan 2014 2:53:30pm
    Author:  yvan

  ==============================================================================
*/

#include "filters.hpp"
#include <math.h>
#include "../utils/misc.hpp"

YSE::DSP::filterBase::filterBase() : freq(0), gain(0), q(0),
last(0), previous(0),
coef1(0), coef2(0),
ff1(0), ff2(0), ff3(0), fb1(0), fb2(0) {
}

YSE::DSP::filterBase::filterBase(const YSE::DSP::filterBase & source)  {
  freq.store(source.freq);
  gain.store(source.gain);
  q = source.q;
  last = source.last;
  previous = source.previous;
  coef1.store(source.coef1);
  coef2.store(source.coef2);
  ff1.store(source.ff1);
  ff2.store(source.ff2);
  ff3.store(source.ff3);
  fb1.store(source.fb1);
  fb2.store(source.fb2);
}

YSE::DSP::highPass& YSE::DSP::highPass::setFrequency(Flt f) {
  if (f < 0) f = 0;
  freq = f;
  coef1 = 1 - f * (2 * 3.14159f) / SAMPLERATE;
  Clamp(coef1, 0.f, 1.f);

  return (*this);
}

YSE::DSP::buffer & YSE::DSP::highPass::operator()(YSE::DSP::buffer & in) {
  if (in.getLength() != samples.getLength()) samples.resize(in.getLength());

  Flt * inPtr = in.getPtr();
  Flt * outPtr = samples.getPtr();
  UInt length = in.getLength();
  Flt coef = coef1;
  if (coef < 1) {
    for (UInt i = 0; i < length; i++) {
      Flt f = *inPtr++ + coef * last;
      *outPtr++ = f - last;
      last = f;
    }
  }
  else {
    last = 0;
    return in;
  }

  return samples;
}

/*******************************************************************************************/


YSE::DSP::lowPass& YSE::DSP::lowPass::setFrequency(Flt f) {
  if (f < 0) f = 0;
  freq = f;
  coef1 = f * (2 * 3.14159f) / SAMPLERATE;
  Clamp(coef1, 0.f, 1.f);

  return (*this);
}

YSE::DSP::buffer & YSE::DSP::lowPass::operator()(YSE::DSP::buffer & in) {
  if (in.getLength() != samples.getLength()) samples.resize(in.getLength());

  Flt * inPtr = in.getPtr();
  Flt * outPtr = samples.getPtr();
  UInt length = in.getLength();

  Flt feedback = 1 - coef1;
  for (UInt i = 0; i < length; i++) {
    last = *outPtr++ = coef1 * *inPtr++ + feedback * last;
  }

  return samples;
}

/*******************************************************************************************/


YSE::DSP::bandPass& YSE::DSP::bandPass::set(Flt freq, Flt q) {
  this->freq = freq;
  this->q = q;
  calcCoef();
  return (*this);
}

YSE::DSP::bandPass& YSE::DSP::bandPass::setFrequency(Flt freq) {
  this->freq = freq;
  calcCoef();
  return (*this);
}

YSE::DSP::bandPass& YSE::DSP::bandPass::setQ(Flt q) {
  this->q = q;
  calcCoef();
  return (*this);
}

void YSE::DSP::bandPass::calcCoef() {
  Flt r, oneminusr, omega;
  if (freq < 0.001) freq = 10;
  if (q < 0) q = 0;
  omega = freq * (2.0f * 3.14159f) / SAMPLERATE;
  if (q < 0.001) oneminusr = 1.0f;
  else oneminusr = omega / q;
  if (oneminusr >  1.0f) oneminusr = 1.0f;
  r = 1.0f - oneminusr;
  coef1 = 2.0f * qCos(omega) * r;
  coef2 = -r * r;

  gain = 2 * oneminusr * (oneminusr + r * omega);
}

float YSE::DSP::bandPass::qCos(Flt omega) {
  if (omega >= -(0.5 * 3.14159f) && omega <= 0.5f * 3.14159f) {
    Flt result = omega * omega;
    return (((result * result * result * (-1.0f / 720.0f) + result * result * (1.0f / 24.0f)) - result * 0.5f) + 1);
  }
  else return 0;
}

YSE::DSP::bandPass::bandPass() {
  calcCoef();
}

YSE::DSP::buffer & YSE::DSP::bandPass::operator()(YSE::DSP::buffer & in) {
  if (in.getLength() != samples.getLength()) samples.resize(in.getLength());

  Flt * inPtr = in.getPtr();
  Flt * outPtr = samples.getPtr();
  UInt length = in.getLength();

  // copy to Flt because we don't want atomic in whole buffer loops
  Flt cf1 = coef1;
  Flt cf2 = coef2;

  for (UInt i = 0; i < length; i++) {
    Flt output = *inPtr++ + cf1 * last + cf2 * previous;
    *outPtr++ = gain * output;
    previous = last;
    last = output;
  }

  return samples;
}

/*******************************************************************************************/


YSE::DSP::biQuad& YSE::DSP::biQuad::setType(BQ_TYPE type) {
  this->type = type;
  calc();
  return (*this);
}

YSE::DSP::biQuad& YSE::DSP::biQuad::setFreq(Flt frequency) {
  freq = frequency;
  calc();
  return (*this);
}

YSE::DSP::biQuad& YSE::DSP::biQuad::setQ(Flt Q) {
  q = Q;
  calc();
  return (*this);
}

YSE::DSP::biQuad& YSE::DSP::biQuad::setPeak(Flt peakGain) {
  gain = peakGain;
  calc();
  return (*this);
}

YSE::DSP::biQuad& YSE::DSP::biQuad::set(BQ_TYPE type, Flt frequency, Flt Q, Flt peakGain) {
  this->type = type;
  freq = frequency;
  q = Q;
  gain = peakGain;

  calc();
  return (*this);
}

YSE::DSP::biQuad& YSE::DSP::biQuad::setRaw(Flt fb1, Flt fb2, Flt ff1, Flt ff2, Flt ff3) {
  Flt discriminant = fb1 * fb1 + 4 * fb2;
  Bool zero = true;
  if (discriminant < 0) {
    if (fb2 >= -1.0f) zero = false;
  }
  else {
    if (fb1 <= 2.0f && fb1 >= -2.0f && 1.0f - fb1 - fb2 >= 0 && 1.0f + fb1 - fb2 >= 0) zero = false;
  }
  if (zero) fb1 = fb2 = ff1 = ff2 = ff3 = 0;
  this->fb1 = fb1;
  this->fb2 = fb2;
  this->ff1 = ff1;
  this->ff2 = ff2;
  this->ff3 = ff3;

  return (*this);
}

YSE::DSP::buffer & YSE::DSP::biQuad::operator()(YSE::DSP::buffer & in) {
  if (in.getLength() != samples.getLength()) samples.resize(in.getLength());

  Flt * inPtr = in.getPtr();
  Flt * outPtr = samples.getPtr();
  UInt length = in.getLength();

  Flt output;

  // copy to float because we don't want atomic to be used in a whole buffer calculation
  Flt b1 = fb1;
  Flt b2 = fb2;
  Flt f1 = ff1;
  Flt f2 = ff2;
  Flt f3 = ff3;

  for (UInt i = 0; i < length; i++) {
    output = *inPtr++ + b1 * last + b2 * previous;
    *outPtr++ = f1 * output + f2 * last + f3 * previous;
    previous = last;
    last = output;
  }

  return samples;
}



void YSE::DSP::biQuad::calc() {
  Flt norm;
  Flt v = (float)pow(10, std::abs(gain.load()) / 20.0f);
  Flt k = tan(Pi * freq / static_cast<Flt>(SAMPLERATE));

  switch (type) {
  case BQ_LOWPASS: {
                     norm = 1 / (1 + k / q + k * k);
                     ff1 = k * k * norm;
                     ff2 = 2 * ff1;
                     ff3 = ff1.load();
                     fb1 = 2 * (k * k - 1) * norm;
                     fb2 = (1 - k / q + k * k) * norm;
                     break;
  }
  case BQ_HIGHPASS: {
                      norm = 1 / (1 + k / q + k * k);
                      ff1 = 1 * norm;
                      ff2 = -2 * ff1;
                      ff3 = ff1.load();
                      fb1 = 2 * (k * k - 1) * norm;
                      fb2 = (1 - k / q + k * k) * norm;
                      break;
  }
  case BQ_BANDPASS: {
                      norm = 1 / (1 + k / q + k * k);
                      ff1 = k / q * norm;
                      ff2 = 0;
                      ff3 = -ff1;
                      fb1 = 2 * (k * k - 1) * norm;
                      fb2 = (1 - k / q + k * k) * norm;
                      break;
  }
  case BQ_NOTCH: {
                   norm = 1 / (1 + k / q + k * k);
                   ff1 = (1 + k * k) * norm;
                   ff2 = 2 * (k * k - 1) * norm;
                   ff3 = ff1.load();
                   fb1 = ff2.load();
                   fb2 = (1 - k / q + k * k) * norm;
                   break;
  }
  case BQ_PEAK: {
                  if (gain >= 0) {
                    norm = 1 / (1 + 1 / q * k + k * k);
                    ff1 = (1 + v / q * k + k * k) * norm;
                    ff2 = 2 * (k * k - 1) * norm;
                    ff3 = (1 - v / q * k + k * k) * norm;
                    fb1 = ff2.load();
                    fb2 = (1 - 1 / q * k + k * k) * norm;
                  }
                  else {
                    norm = 1 / (1 + v / q * k + k * k);
                    ff1 = (1 + 1 / q * k + k * k) * norm;
                    ff2 = 2 * (k * k - 1) * norm;
                    ff3 = (1 - 1 / q * k + k * k) * norm;
                    fb1 = ff2.load();
                    fb2 = (1 - v / q * k + k * k) * norm;
                  }
                  break;
  }
  case BQ_LOWSHELF: {
                      if (gain >= 0) {
                        norm = 1 / (1 + Sqrt2 * k + k * k);
                        ff1 = (1 + sqrt(2 * v) * k + v * k * k) * norm;
                        ff2 = 2 * (v * k * k - 1) * norm;
                        ff3 = (1 - sqrt(2 * v) * k + v * k * k) * norm;
                        fb1 = 2 * (k * k - 1) * norm;
                        fb2 = (1 - Sqrt2 * k + k * k) * norm;
                      }
                      else {
                        norm = 1 / (1 + sqrt(2 * v) * k + v * k * k);
                        ff1 = (1 + Sqrt2 * k + k * k) * norm;
                        ff2 = 2 * (k * k - 1) * norm;
                        ff3 = (1 - Sqrt2 * k + k * k) * norm;
                        fb1 = 2 * (v * k * k - 1) * norm;
                        fb2 = (1 - sqrt(2 * v) * k + v * k * k) * norm;
                      }
                      break;
  }
  case BQ_HIGHSHELF: {
                       if (gain >= 0) {
                         norm = 1 / (1 + Sqrt2 * k + k * k);
                         ff1 = (v + sqrt(2 * v) * k + k * k) * norm;
                         ff2 = 2 * (k * k - v) * norm;
                         ff3 = (v - sqrt(2 * v) * k + k * k) * norm;
                         fb1 = 2 * (k * k - 1) * norm;
                         fb2 = (1 - Sqrt2 * k + k * k) * norm;
                       }
                       else {
                         norm = 1 / (v + sqrt(2 * v) * k + k * k);
                         ff1 = (1 + Sqrt2 * k + k * k) * norm;
                         ff2 = 2 * (k * k - 1) * norm;
                         ff3 = (1 - Sqrt2 * k + k * k) * norm;
                         fb1 = 2 * (k * k - v) * norm;
                         fb2 = (v - sqrt(2 * v) * k + k * k) * norm;
                       }
                       break;

  }
  }
  fb1 = -fb1;
  fb2 = -fb2;

  Flt discriminant = fb1 * fb1 + 4 * fb2;
  Bool zero = true;
  if (discriminant < 0) {
    if (fb2 >= -1.0f) zero = false;
  }
  else {
    if (fb1 <= 2.0f && fb1 >= -2.0f && 1.0f - fb1 - fb2 >= 0 && 1.0f + fb1 - fb2 >= 0) zero = false;
  }
  if (zero) fb1 = fb2 = ff1 = ff2 = ff3 = 0;
}

/*******************************************************************************************/

YSE::DSP::sampleHold::sampleHold() : lastIn(0), lastOut(0) {
}

YSE::DSP::sampleHold& YSE::DSP::sampleHold::reset(Flt value) {
  lastIn = value;
  return (*this);
}

YSE::DSP::sampleHold& YSE::DSP::sampleHold::set(Flt value) {
  lastOut = value;
  return (*this);
}

YSE::DSP::buffer & YSE::DSP::sampleHold::operator()(YSE::DSP::buffer & in, YSE::DSP::buffer & signal) {
  if (in.getLength() != samples.getLength()) samples.resize(in.getLength());

  Flt * inPtr = in.getPtr();
  Flt * sigPtr = signal.getPtr();
  Flt * outPtr = samples.getPtr();
  UInt length = in.getLength();

  // it's a bad idea to parse the whole buffer with atomics, so make a copy first
  Flt li = lastIn;
  Flt lo = lastOut;

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-value"
  for (UInt i = 0; i < length; i++, *inPtr++) {
    if (*sigPtr < li) lo = *inPtr;
    *outPtr++ = lo;
    li = *sigPtr++;
  }
#pragma clang diagnostic pop
  lastIn = li;
  lastOut = lo;

  return samples;
}







