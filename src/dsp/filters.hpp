#pragma once
#include "sample.hpp"
#include "../headers/enums.hpp"

namespace YSE {
	namespace DSP {

    // base class for all filters - don't use
    class filterImpl;
    class API filter {
    public:
      filter();
     ~filter();
      virtual SAMPLE operator()(SAMPLE in) = 0;
      filterImpl * impl;
    };

/*******************************************************************************************/

    // highpass filter
		class API highPass : public filter {
    public:
			highPass& setFrequency(Flt f);
			SAMPLE operator()(SAMPLE in);
		};

/*******************************************************************************************/

    // lowpass filter
		class API lowPass : public filter {
    public:
			lowPass& setFrequency(Flt f);
			SAMPLE operator()(SAMPLE in);
		};

/*******************************************************************************************/

    // bandpass filter
		class API bandPass : public filter {
    public:
			bandPass& set(Flt freq, Flt q);
			bandPass& setFrequency(Flt freq);
			bandPass& setQ(Flt q);
			SAMPLE operator()(SAMPLE in);
			bandPass();

		private:
			void calcCoef();
			static float qCos(Flt omega);
		};

/*******************************************************************************************/

		// for visualizing biquad parameters, see: http://www.earlevel.com/main/2010/12/20/biquad-calculator/
		class API biQuad : public filter {
    public:
			biQuad& set(BQ_TYPE type, Flt frequency, Flt Q, Flt peakGain = 4);
			biQuad& setType	(BQ_TYPE type	);
			biQuad& setFreq	(Flt frequency);
			biQuad& setQ		(Flt Q				);
			biQuad& setPeak	(Flt peakGain	);

			// use this for masochism
			biQuad& setRaw	(Flt fb1, Flt fb2, Flt ff1, Flt ff2, Flt ff3);
			SAMPLE operator()(SAMPLE in);
      biQuad();
		private:
			void calc();
			BQ_TYPE type;
		};

/*******************************************************************************************/
    class sHoldImpl;
		class API sampleHold {
    public:
			sampleHold& reset	(Flt value = 1e20	);
			sampleHold& set		(Flt value				);
			SAMPLE operator()(SAMPLE in, SAMPLE signal);
			sampleHold();
     ~sampleHold();

		private:
      sHoldImpl * impl;
		};

		// looking for vcf? It is in oscillators because it shares a lot of that code



	}
}
