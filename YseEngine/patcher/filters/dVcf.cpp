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

  ADD_DESCRIPTION("Voltage-controlled (heterodyne) filter. Mixes the input with a center-frequency buffer; sharpness controls resonance. Emits real and imaginary components on two buffer outlets.");
  ADD_CATEGORY(pCategory::FILTER);
  INLET_DOC(0, "in", "Audio input buffer.", "-1.0 to 1.0");
  INLET_DOC(1, "center", "Center-frequency buffer (typically a sine at the target frequency).", "-1.0 to 1.0");
  INLET_DOC(2, "sharpness", "Resonance / sharpness control.", "0.0-1.0");
  OUTLET_DOC(0, "real", "Real component of the filtered signal.", "-1.0 to 1.0");
  OUTLET_DOC(1, "imag", "Imaginary component of the filtered signal.", "-1.0 to 1.0");
  PARAM_DOC("sharpness", "0", "Initial resonance / sharpness.", "0.0-1.0");
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
  vcf(*in, *center);
  outputs[0].SendBuffer(&vcf.real(), thread);
  outputs[1].SendBuffer(&vcf.imag(), thread);
}
