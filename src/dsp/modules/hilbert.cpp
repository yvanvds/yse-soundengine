#include "stdafx.h"
#include "hilbert.hpp"

YSE::DSP::hilbert::hilbert() {
	L1.setRaw(1.94632, -0.94657, 0.94657, -1.94632, 1);
	L2.setRaw(0.83774, -0.06338, 0.06338, -0.83774, 1);
	R1.setRaw(-0.02569, 0.260502, -0.260502, 0.02569, 1);
	R2.setRaw(1.8685, -0.870686, 0.870686, -1.8685, 1);
}

void YSE::DSP::hilbert::operator()(SAMPLE in, sample& out1, sample& out2) {
	out1 = L2(L1(in));
	out2 = R2(R1(in));
}

