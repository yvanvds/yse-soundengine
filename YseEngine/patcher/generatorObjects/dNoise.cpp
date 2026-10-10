#include "dNoise.h"

using namespace YSE::PATCHER;

#define className dNoise

CONSTRUCT_DSP() {
  ADD_OUT_BUFFER;

  ADD_DESCRIPTION("White-noise generator. Sends a new block of random samples on every audio "
                  "block.");
  ADD_CATEGORY(pCategory::OSC);
  OUTLET_DOC(0, "out", "White-noise audio output.", "-1.0 to 1.0");
}

CALC() {
  outputs[0].SendBuffer(&noise(), thread);
}
