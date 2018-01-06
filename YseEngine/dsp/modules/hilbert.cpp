/*
  ==============================================================================

    hilbert.cpp
    Created: 31 Jan 2014 2:56:08pm
    Author:  yvan

  ==============================================================================
*/

#include "hilbert.hpp"

YSE::DSP::hilbert::hilbert() {
  L1.setRaw(1.94632f, -0.94657f, 0.94657f, -1.94632f, 1.f);
  L2.setRaw(0.83774f, -0.06338f, 0.06338f, -0.83774f, 1.f);
  R1.setRaw(-0.02569f, 0.260502f, -0.260502f, 0.02569f, 1.f);
  R2.setRaw(1.8685f, -0.870686f, 0.870686f, -1.8685f, 1.f);
}

void YSE::DSP::hilbert::operator()(YSE::DSP::buffer & in, YSE::DSP::buffer & out1, YSE::DSP::buffer & out2) {
  out1 = L2(L1(in));
  out2 = R2(R1(in));
}
