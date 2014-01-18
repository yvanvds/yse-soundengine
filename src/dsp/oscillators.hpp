#pragma once
#include "sample.hpp"

/* Constructor aside, all these objects should be used in dsp mode only */

namespace YSE {
	namespace DSP {

		struct API saw {
			SAMPLE operator()(Flt frequency, UInt length = BUFFERSIZE);
			SAMPLE operator()(SAMPLE in);
			saw();

		private:
			Dbl phase;
			Flt conv;
			Flt frequency;
			sample buffer;

			Flt *inPtr;
			void calc(Bool useFrequency);
		};

		struct API cosine {
			SAMPLE operator()(SAMPLE in);
			cosine();

		private:
			sample buffer;
		};

		struct API sine {
			SAMPLE operator()(Flt frequency, UInt length = BUFFERSIZE);
			SAMPLE operator()(SAMPLE in);
			sine();

		private:
			sample buffer;
			Dbl phase;
			Flt conv;
			Flt frequency;

			Flt *inPtr;
			void calc(Bool useFrequency);
		};


		struct API noise {
			SAMPLE operator()(UInt length = BUFFERSIZE);
			noise();

		private:
			sample buffer;
			Int value;
		};

		struct API vcf {
			vcf& sharpness(Flt q);
      // TODO a bit awkward: first output is function out, second output sent to 3th argument
			SAMPLE operator()(SAMPLE in, SAMPLE center, sample& out2); 
			vcf();

		private:
			Flt re;
			Flt im;
			Flt q;
			Flt isr;
			sample buffer;
		};

	}
}