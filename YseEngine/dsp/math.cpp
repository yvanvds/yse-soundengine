/*
  ==============================================================================

    math.cpp
    Created: 31 Jan 2014 2:54:26pm
    Author:  yvan

  ==============================================================================
*/

#include "math.hpp"
#include <cmath>
#include <limits>

// Both scalar forms clamp like the buffer forms (midiToFreq / freqToMidi below)
// so they never return a non-finite value (#1017): notes below -1500 (and NaN)
// are 0 Hz, notes above 1499 are clamped to 1499, and a frequency of 0 or
// below (and NaN) is note -1500.
Flt YSE::DSP::MidiToFreq(Flt note) {
  if (!(note >= -1500.0f)) return 0.0f;
  if (note > 1499.0f) note = 1499.0f;
  return 440.0f * std::pow(2.0f, (note - 69.0f) / 12.0f);
}

Flt YSE::DSP::FreqToMidi(Flt freq) {
  if (!(freq > 0.0f)) return -1500.0f;
  if (freq > std::numeric_limits<Flt>::max()) freq = std::numeric_limits<Flt>::max();
  return 69.0f + 12.0f * std::log(freq / 440.0f) / std::log(2.0f);
}

/*******************************************************************************************/

YSE::DSP::clip& YSE::DSP::clip::set(Flt low, Flt high) {
  this->low = low;
  this->high = high;
  return (*this);
}

YSE::DSP::clip& YSE::DSP::clip::setLow(Flt low) {
  this->low = low;
  return (*this);
}

YSE::DSP::clip& YSE::DSP::clip::setHigh(Flt high) {
  this->high = high;
  return (*this);
}

YSE::DSP::buffer& YSE::DSP::clip::operator()(YSE::DSP::buffer& in) {
  if (in.getLength() != buffer.getLength()) buffer.resize(in.getLength());
  Flt* inPtr = in.getPtr();
  Flt* outPtr = buffer.getPtr();
  UInt length = in.getLength();

  Flt min = low;
  Flt max = high;
  while (length--) {
    Flt f = *inPtr++;
    if (f < min) f = min;
    if (f > max) f = max;
    *outPtr++ = f;
  }

  return buffer;
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
    Int l = (i ? (i == DUMTAB1SIZE - 1 ? DUMTAB1SIZE - 2 : i) : 1) << 23;
    *(Int*)(&f) = l;
    rsqrt_exptab[i] = 1.0f / sqrt(f);
  }

  for (Int i = 0; i < DUMTAB2SIZE; i++) {
    Flt f = 1 + (1.0f / DUMTAB2SIZE) * i;
    rsqrt_mantissatab[i] = 1.0f / sqrt(f);
  }

  rSqrtInitOK = true;
}

YSE::DSP::rSqrt::rSqrt() {
  setupSqrt();
}

YSE::DSP::buffer& YSE::DSP::rSqrt::operator()(YSE::DSP::buffer& in) {
  if (in.getLength() != buffer.getLength()) buffer.resize(in.getLength());
  Flt* inPtr = in.getPtr();
  Flt* outPtr = buffer.getPtr();
  UInt length = in.getLength();

  while (length--) {
    Flt f = *inPtr;
    Int l = *(Int*)(inPtr++);
    if (f < 0)
      *outPtr++ = 0;
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

YSE::DSP::buffer& YSE::DSP::sqrt::operator()(YSE::DSP::buffer& in) {
  if (in.getLength() != buffer.getLength()) buffer.resize(in.getLength());
  Flt* inPtr = in.getPtr();
  Flt* outPtr = buffer.getPtr();
  UInt length = in.getLength();

  while (length--) {
    Flt f = *inPtr;
    Int l = *(Int*)(inPtr++);
    // Negative, NaN and infinite input give 0, so no NaN or infinity leaves
    // (#1050).
    if (!(f >= 0) || !std::isfinite(f))
      *outPtr++ = 0;
    else {
      Flt g = rsqrt_exptab[(l >> 23) & 0xff] * rsqrt_mantissatab[(l >> 13) & 0x3ff];
      *outPtr++ = f * (1.5f * g - 0.5f * g * g * g * f);
    }
  }

  return buffer;
}

/*******************************************************************************************/

YSE::DSP::buffer& YSE::DSP::wrap::operator()(YSE::DSP::buffer& in) {
  if (in.getLength() != buffer.getLength()) buffer.resize(in.getLength());
  Flt* inPtr = in.getPtr();
  Flt* outPtr = buffer.getPtr();
  UInt length = in.getLength();

  while (length--) {
    Flt f = *inPtr++;
    Int k = (Int)f;
    if (f > 0)
      *outPtr++ = f - k;
    else
      *outPtr++ = f - (k - 1);
  }

  return buffer;
}

/*******************************************************************************************/

namespace {
  // The tuning as used for one block (#1052): a base of 0 or below, or not
  // finite, is 440 Hz; a reference that is not finite is note 69.
  inline Flt TuningBase(Flt base) {
    return (base > 0.f && std::isfinite(base)) ? base : 440.f;
  }

  inline Flt TuningRef(Flt ref) {
    return std::isfinite(ref) ? ref : 69.f;
  }

  constexpr Flt kSemitone = 0.0577622650467f; // ln(2) / 12
  constexpr Flt kSemitonesPerLn = 17.3123404907f; // 12 / ln(2)
} // namespace

YSE::DSP::midiToFreq& YSE::DSP::midiToFreq::setBase(Flt value) {
  base = value;
  return *this;
}

YSE::DSP::midiToFreq& YSE::DSP::midiToFreq::setRef(Flt value) {
  ref = value;
  return *this;
}

YSE::DSP::buffer& YSE::DSP::midiToFreq::operator()(YSE::DSP::buffer& in) {
  if (in.getLength() != buffer.getLength()) buffer.resize(in.getLength());
  const Flt* inPtr = in.getPtr();
  Flt* outPtr = buffer.getPtr();
  const UInt length = in.getLength();
  const Flt b = TuningBase(base);
  const Flt r = TuningRef(ref);

  for (UInt i = 0; i < length; ++i) {
    Flt d = inPtr[i] - r;
    // -1569 / +1430 semitones around note 69 is the -1500..1499 clamp the
    // class always had. NaN fails the first compare and gives 0.
    if (!(d >= -1569.f)) {
      outPtr[i] = 0.f;
      continue;
    }
    if (d > 1430.f) d = 1430.f;
    const Flt f = b * std::exp(kSemitone * d);
    outPtr[i] = std::isfinite(f) ? f : 0.f;
  }

  return buffer;
}

/*******************************************************************************************/

YSE::DSP::freqToMidi& YSE::DSP::freqToMidi::setBase(Flt value) {
  base = value;
  return *this;
}

YSE::DSP::freqToMidi& YSE::DSP::freqToMidi::setRef(Flt value) {
  ref = value;
  return *this;
}

YSE::DSP::buffer& YSE::DSP::freqToMidi::operator()(YSE::DSP::buffer& in) {
  if (in.getLength() != buffer.getLength()) buffer.resize(in.getLength());
  const Flt* inPtr = in.getPtr();
  Flt* outPtr = buffer.getPtr();
  const UInt length = in.getLength();
  const Flt logBase = std::log(TuningBase(base));
  const Flt r = TuningRef(ref);

  for (UInt i = 0; i < length; ++i) {
    Flt f = inPtr[i];
    // 0, below 0 and NaN have no pitch (#1017); infinity is clamped so the
    // logarithm stays finite.
    if (!(f > 0.f)) {
      outPtr[i] = -1500.f;
      continue;
    }
    if (f > std::numeric_limits<Flt>::max()) f = std::numeric_limits<Flt>::max();
    outPtr[i] = r + kSemitonesPerLn * (std::log(f) - logBase);
  }
  return buffer;
}

/*******************************************************************************************/

#define LOGTEN 2.302585092994f

YSE::DSP::buffer& YSE::DSP::dbToRms::operator()(YSE::DSP::buffer& in) {
  if (in.getLength() != buffer.getLength()) buffer.resize(in.getLength());
  Flt* inPtr = in.getPtr();
  Flt* outPtr = buffer.getPtr();
  UInt length = in.getLength();

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-value"
  for (; length--; *inPtr++, *outPtr++) {
    Flt f = *inPtr;
    if (f <= 0)
      *outPtr = 0;
    else {
      if (f > 485) f = 485;
      *outPtr = ::exp((LOGTEN * 0.05f) * (f - 100.0f));
    }
  }
#pragma clang diagnostic pop
  return buffer;
}

/*******************************************************************************************/

YSE::DSP::buffer& YSE::DSP::rmsToDb::operator()(YSE::DSP::buffer& in) {
  if (in.getLength() != buffer.getLength()) buffer.resize(in.getLength());
  Flt* inPtr = in.getPtr();
  Flt* outPtr = buffer.getPtr();
  UInt length = in.getLength();

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-value"
  for (; length--; *inPtr++, *outPtr++) {
    Flt f = *inPtr;
    if (f <= 0)
      *outPtr = 0;
    else {
      Flt g = 100 + 20.0f / LOGTEN * ::log(f);
      *outPtr = (g < 0 ? 0 : g);
    }
  }
#pragma clang diagnostic pop
  return buffer;
}

/*******************************************************************************************/

YSE::DSP::buffer& YSE::DSP::dbToPow::operator()(YSE::DSP::buffer& in) {
  if (in.getLength() != buffer.getLength()) buffer.resize(in.getLength());
  Flt* inPtr = in.getPtr();
  Flt* outPtr = buffer.getPtr();
  UInt length = in.getLength();

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-value"
  for (; length--; *inPtr++, *outPtr++) {
    Flt f = *inPtr;
    if (f <= 0)
      *outPtr = 0;
    else {
      if (f > 870) f = 870;
      *outPtr = ::exp((LOGTEN * 0.1f) * (f - 100.0f));
    }
  }
#pragma clang diagnostic pop
  return buffer;
}

/*******************************************************************************************/

YSE::DSP::buffer& YSE::DSP::powToDb::operator()(YSE::DSP::buffer& in) {
  if (in.getLength() != buffer.getLength()) buffer.resize(in.getLength());
  Flt* inPtr = in.getPtr();
  Flt* outPtr = buffer.getPtr();
  UInt length = in.getLength();

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-value"
  for (; length--; *inPtr++, *outPtr++) {
    Flt f = *inPtr;
    if (f <= 0)
      *outPtr = 0;
    else {
      Flt g = 100 + 10.0f / LOGTEN * ::log(f);
      *outPtr = (g < 0 ? 0 : g);
    }
  }
#pragma clang diagnostic pop
  return buffer;
}

/*******************************************************************************************/

YSE::DSP::buffer& YSE::DSP::pow::operator()(YSE::DSP::buffer& in1, YSE::DSP::buffer& in2) {
  if (in1.getLength() != buffer.getLength()) buffer.resize(in1.getLength());
  Flt* in1Ptr = in1.getPtr();
  Flt* in2Ptr = in2.getPtr();
  Flt* outPtr = buffer.getPtr();
  UInt length = in1.getLength();

  // A base of 0 or below gives 0, and so does a result past the float range
  // (or a NaN operand): no NaN or infinity leaves (#1050).
  while (length--) {
    Flt f = *in1Ptr++;
    Flt r = f > 0 ? ::pow(f, *in2Ptr) : 0;
    *outPtr = std::isfinite(r) ? r : 0;
    outPtr++;
    in2Ptr++;
  }

  return buffer;
}

/*******************************************************************************************/

YSE::DSP::buffer& YSE::DSP::exp::operator()(YSE::DSP::buffer& in) {
  if (in.getLength() != buffer.getLength()) buffer.resize(in.getLength());
  Flt* inPtr = in.getPtr();
  Flt* outPtr = buffer.getPtr();
  UInt length = in.getLength();

  while (length--)
    *outPtr++ = ::exp(*inPtr++);

  return buffer;
}

/*******************************************************************************************/

YSE::DSP::buffer& YSE::DSP::log::operator()(YSE::DSP::buffer& in1, YSE::DSP::buffer& in2) {
  if (in1.getLength() != buffer.getLength()) buffer.resize(in1.getLength());
  Flt* in1Ptr = in1.getPtr();
  Flt* in2Ptr = in2.getPtr();
  Flt* outPtr = buffer.getPtr();
  UInt length = in1.getLength();

  // An input of 0 or below has no logarithm: it gives 0. A base of 0 or
  // below, or of 1, has none either: the natural logarithm is used instead.
  // Anything still not finite (a NaN, an infinite input) gives 0, so no NaN
  // or infinity leaves (#1050).
  while (length--) {
    Flt f = *in1Ptr++, g = *in2Ptr++;
    Flt r = 0;
    if (f > 0) r = (g > 0 && g != 1) ? ::log(f) / ::log(g) : ::log(f);
    *outPtr = std::isfinite(r) ? r : 0;
    outPtr++;
  }

  return buffer;
}

/*******************************************************************************************/

YSE::DSP::buffer& YSE::DSP::abs::operator()(YSE::DSP::buffer& in) {
  if (in.getLength() != buffer.getLength()) buffer.resize(in.getLength());
  Flt* inPtr = in.getPtr();
  Flt* outPtr = buffer.getPtr();
  UInt length = in.getLength();

  while (length--) {
    Flt f = *inPtr++;
    *outPtr++ = (f >= 0 ? f : -f);
  }

  return buffer;
}

YSE::DSP::buffer& YSE::DSP::inverter::operator()(YSE::DSP::buffer& in, bool zeroToOne) {
  if (in.getLength() != buffer.getLength()) buffer.resize(in.getLength());
  UInt l = buffer.getLength();
  Flt* inPtr = in.getPtr();
  Flt* outPtr = buffer.getPtr();

  if (zeroToOne) {
    for (; l > 7; l -= 8, inPtr += 8, outPtr += 8) {
      outPtr[0] = 1 - inPtr[0];
      outPtr[1] = 1 - inPtr[1];
      outPtr[2] = 1 - inPtr[2];
      outPtr[3] = 1 - inPtr[3];
      outPtr[4] = 1 - inPtr[4];
      outPtr[5] = 1 - inPtr[5];
      outPtr[6] = 1 - inPtr[6];
      outPtr[7] = 1 - inPtr[7];
    }

    while (l--) {
      *outPtr++ = 1 - *inPtr++;
    }
  } else {
    for (; l > 7; l -= 8, inPtr += 8, outPtr += 8) {
      outPtr[0] = -inPtr[0];
      outPtr[1] = -inPtr[1];
      outPtr[2] = -inPtr[2];
      outPtr[3] = -inPtr[3];
      outPtr[4] = -inPtr[4];
      outPtr[5] = -inPtr[5];
      outPtr[6] = -inPtr[6];
      outPtr[7] = -inPtr[7];
    }

    while (l--) {
      *outPtr++ = -(*inPtr++);
    }
  }

  return buffer;
}