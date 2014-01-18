#pragma once
#include "../filters.hpp"

namespace YSE {
	namespace DSP {

		struct API hilbert {
			biQuad L1, L2;
			biQuad R1, R2;

			hilbert();
			void operator()(SAMPLE in, sample& out1, sample& out2);
		};

	}
}
