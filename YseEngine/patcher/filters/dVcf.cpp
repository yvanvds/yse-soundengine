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

  // A one-pole complex resonator: per sample, the pole sits at angle
  // 2*pi*center/SAMPLERATE with radius 1 - angle/sharpness (clamped to 0-1), so
  // a higher sharpness narrows the band. The real part is the bandpass output.
  ADD_DESCRIPTION(
      "Resonant bandpass filter whose center frequency is a signal, so it can sweep at audio "
      "rate. Sharpness sets how narrow the band is.");
  ADD_CATEGORY(pCategory::FILTER);
  INLET_DOC(0, "in", "Audio input.", "-1.0 to 1.0");
  INLET_DOC(1, "center",
            "Center frequency in Hz, as a signal. Both signal inlets must be "
            "connected for the filter to run.",
            "0-20000 Hz");
  INLET_DOC(2, "sharpness", "How narrow the band is: higher is narrower. 0 turns filtering off.",
            "0+");
  OUTLET_DOC(0, "real", "The bandpass output.", "-1.0 to 1.0");
  OUTLET_DOC(1, "imag", "The imaginary part of the resonator, a lowpass-like response.",
             "-1.0 to 1.0");
  PARAM_DOC("sharpness", "0", "Initial sharpness.", "0+");
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
