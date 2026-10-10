#include "dClip.h"

using namespace YSE::PATCHER;

#define className dClip

CONSTRUCT_DSP() {
  // in 0: audio buffer
  ADD_IN_0;
  REG_BUFFER_IN(SetBuffer);

  // in 1: low value (float)
  ADD_IN_1;
  REG_FLOAT_IN(SetLow);

  // in 2: high value (float)
  ADD_IN_2;
  REG_FLOAT_IN(SetHigh);

  // out 0: audio output
  ADD_OUT_BUFFER;

  ADD_PARAM(low);
  ADD_PARAM(high);

  buffer = nullptr;
  low = -1.0f;
  high = 1.0f;

  ADD_DESCRIPTION("Hard clipper: limits every sample of an audio signal to the range low to "
                  "high. Use it to tame stray peaks, or as a distortion stage.");
  ADD_CATEGORY(pCategory::MATH);
  INLET_DOC(0, "in", "Audio signal.", "any float");
  INLET_DOC(1, "low", "Lower limit, as a float.", "any float");
  INLET_DOC(2, "high", "Upper limit, as a float.", "any float");
  OUTLET_DOC(0, "out", "The signal limited to low to high.", "low to high");
  PARAM_DOC("low", "-1.0", "Starting lower limit.", "any float");
  PARAM_DOC("high", "1.0", "Starting upper limit.", "any float");
}

RESET() // {
buffer = nullptr;
}

BUFFER_IN(SetBuffer) {
  this->buffer = buffer;
}

FLOAT_IN(SetLow) {
  low = value;
}

FLOAT_IN(SetHigh) {
  high = value;
}

CALC() {
  if (buffer == nullptr) return;

  clip.set(low, high);
  outputs[0].SendBuffer(&clip(*buffer), thread);
}