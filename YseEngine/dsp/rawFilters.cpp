/*
  ==============================================================================

    rawFilters.cpp
    Created: 15 Sep 2015 6:39:45pm
    Author:  yvan

  ==============================================================================
*/

#include <cassert>
#include "rawFilters.hpp"

// The output buffers below used to keep the length they were constructed with
// (STANDARD_BUFFERSIZE) while the render loops wrote in1.getLength() samples,
// so every caller passing a different block length wrote past the end of the
// heap allocation — silently in release, as heap corruption in debug (issue
// #651). Each operator() now sizes its output to the input first, which is the
// same idiom every other DSP node in the engine uses (filters.cpp, math.cpp,
// oscillators.cpp, fft.cpp).
//
// Real-time note: std::vector keeps its capacity when it shrinks, so the resize
// allocates at most once per distinct block length seen and is a plain
// comparison in the steady state — no allocation on the per-block audio path.
//
// The length preconditions that remain are enforced in release too: the guard
// itself is a live `if` that zeroes the (now correctly sized) output and
// returns; the assert only adds a loud diagnostic in debug builds.

YSE::DSP::buffer& YSE::DSP::realOnePole::operator()(buffer& in1, buffer& in2) {
  UInt n = in1.getLength();
  if (out.getLength() != n) out.resize(n);

  if (in1.getLength() != in2.getLength()) {
    // buffers should have the same size!
    assert(false);
    out = 0;
    return out;
  }

  Flt* in1Ptr = in1.getPtr();
  Flt* in2Ptr = in2.getPtr();
  Flt* outPtr = out.getPtr();

  for (UInt i = 0; i < n; i++) {
    Flt next = *in1Ptr++;
    Flt coef = *in2Ptr++;
    *outPtr++ = lastSample = coef * lastSample + next;
  }

  return out;
}

/*********************************************************************/

YSE::DSP::buffer& YSE::DSP::realOneZero::operator()(buffer& in1, buffer& in2) {
  UInt n = in1.getLength();
  if (out.getLength() != n) out.resize(n);

  if (in1.getLength() != in2.getLength()) {
    // buffers should have the same size!
    assert(false);
    out = 0;
    return out;
  }

  Flt* in1Ptr = in1.getPtr();
  Flt* in2Ptr = in2.getPtr();
  Flt* outPtr = out.getPtr();

  for (UInt i = 0; i < n; i++) {
    Flt next = *in1Ptr++;
    Flt coef = *in2Ptr++;
    *outPtr++ = next - coef * lastSample;
    lastSample = next;
  }

  return out;
}

/*********************************************************************/

YSE::DSP::buffer& YSE::DSP::realOneZeroReversed::operator()(buffer& in1, buffer& in2) {
  UInt n = in1.getLength();
  if (out.getLength() != n) out.resize(n);

  if (in1.getLength() != in2.getLength()) {
    // buffers should have the same size!
    assert(false);
    out = 0;
    return out;
  }

  Flt* in1Ptr = in1.getPtr();
  Flt* in2Ptr = in2.getPtr();
  Flt* outPtr = out.getPtr();

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

MULTICHANNELBUFFER& YSE::DSP::complexOnePole::operator()(MULTICHANNELBUFFER& in1,
                                                         MULTICHANNELBUFFER& in2) {
  if (in1.size() != 2 || in2.size() != 2) {
    // every input should contain a real and imaginary part
    assert(false);
    out[0] = 0;
    out[1] = 0;
    return out;
  }

  UInt n = in1[0].getLength();
  if (out[0].getLength() != n) out[0].resize(n);
  if (out[1].getLength() != n) out[1].resize(n);

  if (in1[0].getLength() != in1[1].getLength() || in1[0].getLength() != in2[0].getLength() ||
      in1[0].getLength() != in2[1].getLength()) {
    // every input should have the same length
    assert(false);
    out[0] = 0;
    out[1] = 0;
    return out;
  }

  Flt* realIn1 = in1[0].getPtr();
  Flt* realIn2 = in2[0].getPtr();
  Flt* realOut = out[0].getPtr();
  Flt* imaginaryIn1 = in1[1].getPtr();
  Flt* imaginaryIn2 = in2[1].getPtr();
  Flt* imaginaryOut = out[1].getPtr();

  for (UInt i = 0; i < n; i++) {
    Flt nextReal = *realIn1++;
    Flt nextImaginary = *imaginaryIn1++;
    Flt coefReal = *realIn2++;
    Flt coefImaginary = *imaginaryIn2++;

    Flt tempReal = *realOut++ = nextReal + lastReal * coefReal - lastImaginary * coefImaginary;
    lastImaginary = *imaginaryOut++ =
        nextImaginary + lastReal * coefImaginary + lastImaginary * coefReal;
    lastReal = tempReal;
  }

  return out;
}

/*********************************************************************/

YSE::DSP::complexOneZero::complexOneZero() : lastReal(0), lastImaginary(0) {
  out.emplace_back(STANDARD_BUFFERSIZE);
  out.emplace_back(STANDARD_BUFFERSIZE);
}

MULTICHANNELBUFFER& YSE::DSP::complexOneZero::operator()(MULTICHANNELBUFFER& in1,
                                                         MULTICHANNELBUFFER& in2) {
  if (in1.size() != 2 || in2.size() != 2) {
    // every input should contain a real and imaginary part
    assert(false);
    out[0] = 0;
    out[1] = 0;
    return out;
  }

  UInt n = in1[0].getLength();
  if (out[0].getLength() != n) out[0].resize(n);
  if (out[1].getLength() != n) out[1].resize(n);

  if (in1[0].getLength() != in1[1].getLength() || in1[0].getLength() != in2[0].getLength() ||
      in1[0].getLength() != in2[1].getLength()) {
    // every input should have the same length
    assert(false);
    out[0] = 0;
    out[1] = 0;
    return out;
  }

  Flt* realIn1 = in1[0].getPtr();
  Flt* realIn2 = in2[0].getPtr();
  Flt* realOut = out[0].getPtr();
  Flt* imaginaryIn1 = in1[1].getPtr();
  Flt* imaginaryIn2 = in2[1].getPtr();
  Flt* imaginaryOut = out[1].getPtr();

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

MULTICHANNELBUFFER& YSE::DSP::complexOneZeroReversed::operator()(MULTICHANNELBUFFER& in1,
                                                                 MULTICHANNELBUFFER& in2) {
  if (in1.size() != 2 || in2.size() != 2) {
    // every input should contain a real and imaginary part
    assert(false);
    out[0] = 0;
    out[1] = 0;
    return out;
  }

  UInt n = in1[0].getLength();
  if (out[0].getLength() != n) out[0].resize(n);
  if (out[1].getLength() != n) out[1].resize(n);

  if (in1[0].getLength() != in1[1].getLength() || in1[0].getLength() != in2[0].getLength() ||
      in1[0].getLength() != in2[1].getLength()) {
    // every input should have the same length
    assert(false);
    out[0] = 0;
    out[1] = 0;
    return out;
  }

  Flt* realIn1 = in1[0].getPtr();
  Flt* realIn2 = in2[0].getPtr();
  Flt* realOut = out[0].getPtr();
  Flt* imaginaryIn1 = in1[1].getPtr();
  Flt* imaginaryIn2 = in2[1].getPtr();
  Flt* imaginaryOut = out[1].getPtr();

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
