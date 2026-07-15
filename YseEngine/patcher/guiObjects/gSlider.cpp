#include "gSlider.h"

using namespace YSE::PATCHER;

#define className gSlider

CONSTRUCT() {
  ADD_IN_0;
  REG_INT_IN(SetInt);
  REG_FLOAT_IN(SetFloat);
  REG_BANG_IN(SetBang);

  ADD_OUT_FLOAT;

  value = 0.f;

  ADD_DESCRIPTION("Slider control. Stores a normalized float in [0, 1]; ints/floats are clamped on "
                  "input. Emits the stored value on every calculate tick.");
  ADD_CATEGORY(pCategory::GUI);
  INLET_DOC(0, "set/bang",
            "Sets the slider position; bang is a no-op trigger that still fires the output.",
            "0.0-1.0");
  OUTLET_DOC(0, "out", "Current slider value (clamped to [0, 1]).", "0.0-1.0");
}

INT_IN(SetInt) {
  this->value = (float)value;
  if (this->value < 0) this->value = 0;
  if (this->value > 1) this->value = 1;
}

FLOAT_IN(SetFloat) {
  this->value = value;
  if (this->value < 0) this->value = 0;
  if (this->value > 1) this->value = 1;
}

BANG_IN(SetBang) {}

GUI_VALUE() {
  return std::to_string(value.load());
}

CALC() {
  outputs[0].SendFloat(value, thread);
}