#include "stdafx.h"
#include "math_functions.h"
#include <math.h>

#define LOGTEN 2.302585092994



Flt* FM::DSP::maximum(Flt *in1, Flt *in2, Flt *out, UInt length) {
	while (length--) {
		*out++ = (*in1 > *in2 ? *in1 : *in2);
		in1++; in2++;
	}
	return out;
}

Flt* FM::DSP::maximum(Flt *in, Flt f, Flt *out, UInt length) {
	while (length--) {
		*out++ = ( *in > f ? *in : f );
		*in++;
	}
	return out;
}

Flt* FM::DSP::minimum(Flt *in1, Flt *in2, Flt *out, UInt length) {
	while (length--) {
		*out++ = (*in1 < *in2 ? *in1 : *in2);
		in1++; in2++;
	}
	return out;
}

Flt* FM::DSP::minimum(Flt *in, Flt f, Flt *out, UInt length) {
	while (length--) {
		*out++ = ( *in < f ? *in : f );
		*in++;
	}
	return out;
}

Flt FM::DSP::powToDb(Flt f) {
	if (f <= 0) return 0;
	
	Flt result = 100 + 10./LOGTEN * log(f);
	return (result < 0 ? 0 : result);
}

Flt FM::DSP::rmsToDb(Flt f) {
	if (f <= 0) return 0;

	Flt result = 100 + 20./LOGTEN * log(f);
	return (result < 0 ? 0 : result);
}

Flt FM::DSP::dbToPow(Flt f) {
	if (f <= 0) return 0;
	
	if (f > 870) f = 870;
	return (exp((LOGTEN * 0.1) * (f-100.)));
}

Flt FM::DSP::dbToRms(Flt f) {
	if (f <= 0) return 0;

	if (f > 485) f= 485;
	return (exp((LOGTEN * 0.05) * (f-100.)));
}