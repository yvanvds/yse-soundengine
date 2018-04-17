#include "dVcf.h"
#include "../pObjectList.hpp"

using namespace YSE::PATCHER;
#define className dVcf

CONSTRUCT_DSP() {
  // in 0: audio buffer
  // in 1: center buffer
  // in 2: Q / sharpness (float)

  // out 0: audio out
  // out 2: audio out

  ADD_IN_0;
  REG_BUFFER_IN(SetInput);

  ADD_IN_1;
  REG_BUFFER_IN(SetCenter);

  ADD_IN_2;
  REG_FLOAT_IN(SetSharpness);

  ADD_OUT_BUFFER;
  ADD_OUT_BUFFER;

  ADD_PARAM(sharpness);

  in = nullptr;
  center = nullptr;
  sharpness = 0.f;
}

RESET() // {
  in = nullptr;
  center = nullptr;
}

BUFFER_IN(SetInput) {
  this->in = buffer;
}

BUFFER_IN(SetCenter) {
  this->center = buffer;
}

FLOAT_IN(SetSharpness) {
  this->sharpness = value;
}

CALC() {
  if (in == nullptr) return;
  if (center == nullptr) return;

  vcf.sharpness(sharpness);
  DSP::buffer * out2 = nullptr;

  DSP::buffer & out1 = vcf(*in, *center, *out2);
  outputs[0].SendBuffer(&out1, thread);
  outputs[1].SendBuffer(out2, thread);
}
