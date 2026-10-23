#include "dPink.h"

using namespace YSE::PATCHER;

#define className dPink

CONSTRUCT_DSP() {
  ADD_OUT_BUFFER;

  ADD_DESCRIPTION("Pink-noise generator: random samples with equal energy in every octave "
                  "(-3 dB per octave). Sends a new block on every audio block.");
  ADD_CATEGORY(pCategory::OSC);
  OUTLET_DOC(0, "out", "Pink-noise audio output, about -14 dBFS RMS.", "-1.0 to 1.0");
}

CALC() {
  outputs[0].SendBuffer(&pink(), thread);
}
