/*
  ==============================================================================

    math_functions.cpp
    Created: 31 Jan 2014 2:54:48pm
    Author:  yvan

  ==============================================================================
*/

#include "math_functions.h"
#include <math.h>

#define LOGTEN 2.302585092994f


#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-value"
Flt* YSE::DSP::maximum(Flt *in1, Flt *in2, Flt *out, UInt length) {
  while (length--) {
    *out++ = (*in1 > *in2 ? *in1 : *in2);
    in1++; in2++;
  }
  return out;
}

Flt* YSE::DSP::maximum(Flt *in, Flt f, Flt *out, UInt length) {
  while (length--) {
    *out++ = (*in > f ? *in : f);
    *in++;
  }
  return out;
}

Flt* YSE::DSP::minimum(Flt *in1, Flt *in2, Flt *out, UInt length) {
  while (length--) {
    *out++ = (*in1 < *in2 ? *in1 : *in2);
    in1++; in2++;
  }
  return out;
}

Flt* YSE::DSP::minimum(Flt *in, Flt f, Flt *out, UInt length) {
  while (length--) {
    *out++ = (*in < f ? *in : f);
    *in++;
  }
  return out;
}
#pragma clang diagnostic pop

Flt YSE::DSP::powToDb(Flt f) {
  if (f <= 0) return 0;

  Flt result = 100 + 10.f / LOGTEN * log(f);
  return (result < 0 ? 0 : result);
}

Flt YSE::DSP::rmsToDb(Flt f) {
  if (f <= 0) return 0;

  Flt result = 100 + 20.f / LOGTEN * log(f);
  return (result < 0 ? 0 : result);
}

Flt YSE::DSP::dbToPow(Flt f) {
  if (f <= 0) return 0;

  if (f > 870) f = 870;
  return (exp((LOGTEN * 0.1f) * (f - 100.f)));
}

Flt YSE::DSP::dbToRms(Flt f) {
  if (f <= 0) return 0;

  if (f > 485) f = 485;
  return (exp((LOGTEN * 0.05f) * (f - 100.f)));
}

Flt YSE::DSP::getMaxAmplitude(buffer & source) {
  return getMaxAmplitude(source.getPtr(), source.getLength());
}

Flt YSE::DSP::getMaxAmplitude(Flt * pos, UInt windowSize) {
  Flt * p = pos; // W don't want to change pos because the user might not expect that
  Flt result = 0.f;
  for (UInt i = 0; i < windowSize; i++) {
    if (*p > result) {
      result = *p;
    }
    p++;
  }
  return result;
}

/* sigrsqrt - reciprocal square root good to 8 mantissa bits  */

#define DUMTAB1SIZE 256
#define DUMTAB2SIZE 1024

struct sqrtTable {
  sqrtTable() {
    int i;
    for (i = 0; i < DUMTAB1SIZE; i++)
    {
      union {
        float f;
        long l;
      } u;
      int32_t l = (i ? (i == DUMTAB1SIZE - 1 ? DUMTAB1SIZE - 2 : i) : 1) << 23;
      u.l = l;
      exptab[i] = 1. / sqrt(u.f);
    }
    for (i = 0; i < DUMTAB2SIZE; i++)
    {
      float f = 1 + (1. / DUMTAB2SIZE) * i;
      mantissatab[i] = 1. / sqrt(f);
    }
  }

  float exptab     [DUMTAB1SIZE];
  float mantissatab[DUMTAB2SIZE];

};

sqrtTable & SqrtTable() {
  static sqrtTable t;
  return t;
}


void YSE::DSP::sqrtFunc(Flt * in, Flt * out, UInt length) {
  sqrtTable & s = SqrtTable();
  while (length--) {
    float f = *in;
    long l = *(long *)(in++);
    if (f < 0) *out++ = 0;
    else {
      float g = s.exptab[(l >> 23) & 0xff] *
        s.mantissatab[(l >> 13) & 0x3ff];
      *out++ = f * (1.5 * g - 0.5 * g * g * g * f);
    }
  }
}