#pragma once

// math functions for dsp routines

namespace FM {
	namespace DSP {

		Flt* maximum(Flt *in1, Flt *in2, Flt *out, UInt length);
		Flt* maximum(Flt *in , Flt  f  , Flt *out, UInt length);

		Flt* minimum(Flt *in1, Flt *in2, Flt *out, UInt length);
		Flt* minimum(Flt *in , Flt  f  , Flt *out, UInt length);

		// utility functions
		Flt powToDb(Flt f);
		Flt rmsToDb(Flt f);
		Flt dbToPow(Flt f);
		Flt dbToRms(Flt f);
	}
}