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