/*
  ==============================================================================

    rawFilters.cpp
    Created: 15 Sep 2015 6:39:45pm
    Author:  yvan

  ==============================================================================
*/

#include <cassert>
#include "rawFilters.hpp"

YSE::DSP::buffer & YSE::DSP::realOnePole::operator()(buffer & in1, buffer & in2) {
  if (in1.getLength() != in2.getLength()) {
    // buffers should have the same size!
    assert(false);
    out = 0;
    return out;
  }

  UInt n = in1.getLength();
  Flt * in1Ptr = in1.getPtr();
  Flt * in2Ptr = in2.getPtr();
  Flt * outPtr = out.getPtr();

  for (UInt i = 0; i < n; i++) {
    Flt next = *in1Ptr++;
    Flt coef = *in2Ptr++;
    *outPtr++ = lastSample = coef * lastSample + next;
  }

  return out;
}

/*********************************************************************/

YSE::DSP::buffer & YSE::DSP::realOneZero::operator()(buffer & in1, buffer & in2) {
  if (in1.getLength() != in2.getLength()) {
    // buffers should have the same size!
    assert(false);
    out = 0;
    return out;
  }

  UInt n = in1.getLength();
  Flt * in1Ptr = in1.getPtr();
  Flt * in2Ptr = in2.getPtr();
  Flt * outPtr = out.getPtr();

  for (UInt i = 0; i < n; i++) {
    Flt next = *in1Ptr++;
    Flt coef = *in2Ptr++;
    *outPtr++ = next - coef * lastSample;
    lastSample = next;
  }

  return out;
}

/*********************************************************************/

YSE::DSP::buffer & YSE::DSP::realOneZeroReversed::operator()(buffer & in1, buffer & in2) {
  if (in1.getLength() != in2.getLength()) {
    // buffers should have the same size!
    assert(false);
    out = 0;
    return out;
  }

  UInt n = in1.getLength();
  Flt * in1Ptr = in1.getPtr();
  Flt * in2Ptr = in2.getPtr();
  Flt * outPtr = out.getPtr();

  for (UInt i = 0; i < n; i++) {
    Flt next = *in1Ptr++;
    Flt coef = *in2Ptr++;
    *outPtr++ = lastSample - coef * next;
    lastSample = next;
  }

  return out;
}

/*********************************************************************/

YSE::DSP::complexOnePole::complexOnePole() : lastReal(0), lastImaginary(0) {
  out.emplace_back(STANDARD_BUFFERSIZE);
  out.emplace_back(STANDARD_BUFFERSIZE);
}

MULTICHANNELBUFFER & YSE::DSP::complexOnePole::operator()(MULTICHANNELBUFFER & in1, MULTICHANNELBUFFER & in2) {
  if (in1.size() != 2 || in2.size() != 2) {
    // every input should contain a real and imaginary part
    assert(false);
    out[0] = 0;
    out[1] = 0;
    return out;
  }

  if ( in1[0].getLength() != in1[1].getLength()
    || in1[0].getLength() != in2[0].getLength()
    || in1[0].getLength() != in2[1].getLength()) {
    // every input should have the same length
    assert(false);
    out[0] = 0;
    out[1] = 0;
    return out;
  }

  Flt * realIn1 = in1[0].getPtr();
  Flt * realIn2 = in2[0].getPtr();
  Flt * realOut = out[0].getPtr();
  Flt * imaginaryIn1 = in1[1].getPtr();
  Flt * imaginaryIn2 = in2[1].getPtr();
  Flt * imaginaryOut = out[1].getPtr();

  UInt n = in1[0].getLength();
  for (UInt i = 0; i < n; i++) {
    Flt nextReal = *realIn1++;
    Flt nextImaginary = *imaginaryIn1++;
    Flt coefReal = *realIn2++;
    Flt coefImaginary = *imaginaryIn2++;

    Flt tempReal = *realOut++ = nextReal + lastReal * coefReal - lastImaginary * coefImaginary;
    lastImaginary = *imaginaryOut++ = nextImaginary + lastReal * coefImaginary + lastImaginary * coefReal;
    lastReal = tempReal;
  }

  return out;
}

/*********************************************************************/

YSE::DSP::complexOneZero::complexOneZero() : lastReal(0), lastImaginary(0) {
  out.emplace_back(STANDARD_BUFFERSIZE);
  out.emplace_back(STANDARD_BUFFERSIZE);
}

MULTICHANNELBUFFER & YSE::DSP::complexOneZero::operator()(MULTICHANNELBUFFER & in1, MULTICHANNELBUFFER & in2) {
  if (in1.size() != 2 || in2.size() != 2) {
    // every input should contain a real and imaginary part
    assert(false);
    out[0] = 0;
    out[1] = 0;
    return out;
  }

  if (in1[0].getLength() != in1[1].getLength()
    || in1[0].getLength() != in2[0].getLength()
    || in1[0].getLength() != in2[1].getLength()) {
    // every input should have the same length
    assert(false);
    out[0] = 0;
    out[1] = 0;
    return out;
  }

  Flt * realIn1 = in1[0].getPtr();
  Flt * realIn2 = in2[0].getPtr();
  Flt * realOut = out[0].getPtr();
  Flt * imaginaryIn1 = in1[1].getPtr();
  Flt * imaginaryIn2 = in2[1].getPtr();
  Flt * imaginaryOut = out[1].getPtr();

  UInt n = in1[0].getLength();
  for (UInt i = 0; i < n; i++) {
    Flt nextReal = *realIn1++;
    Flt nextImaginary = *imaginaryIn1++;
    Flt coefReal = *realIn2++;
    Flt coefImaginary = *imaginaryIn2++;

    *realOut++ = nextReal - lastReal * coefReal + lastImaginary * coefImaginary;
    *imaginaryOut++ = nextImaginary - lastReal * coefImaginary - lastImaginary * coefReal;
    lastReal = nextReal;
    lastImaginary = nextImaginary;
  }

  return out;
}

/*********************************************************************/

YSE::DSP::complexOneZeroReversed::complexOneZeroReversed() : lastReal(0), lastImaginary(0) {
  out.emplace_back(STANDARD_BUFFERSIZE);
  out.emplace_back(STANDARD_BUFFERSIZE);
}

MULTICHANNELBUFFER & YSE::DSP::complexOneZeroReversed::operator()(MULTICHANNELBUFFER & in1, MULTICHANNELBUFFER & in2) {
  if (in1.size() != 2 || in2.size() != 2) {
    // every input should contain a real and imaginary part
    assert(false);
    out[0] = 0;
    out[1] = 0;
    return out;
  }

  if (in1[0].getLength() != in1[1].getLength()
    || in1[0].getLength() != in2[0].getLength()
    || in1[0].getLength() != in2[1].getLength()) {
    // every input should have the same length
    assert(false);
    out[0] = 0;
    out[1] = 0;
    return out;
  }

  Flt * realIn1 = in1[0].getPtr();
  Flt * realIn2 = in2[0].getPtr();
  Flt * realOut = out[0].getPtr();
  Flt * imaginaryIn1 = in1[1].getPtr();
  Flt * imaginaryIn2 = in2[1].getPtr();
  Flt * imaginaryOut = out[1].getPtr();

  UInt n = in1[0].getLength();
  for (UInt i = 0; i < n; i++) {
    Flt nextReal = *realIn1++;
    Flt nextImaginary = *imaginaryIn1++;
    Flt coefReal = *realIn2++;
    Flt coefImaginary = *imaginaryIn2++;

    *realOut++ = nextReal - lastReal * coefReal + lastImaginary * coefImaginary;
    *imaginaryOut++ = nextImaginary - lastReal * coefImaginary - lastImaginary * coefReal;
    lastReal = nextReal;
    lastImaginary = nextImaginary;
  }

  return out;
}

