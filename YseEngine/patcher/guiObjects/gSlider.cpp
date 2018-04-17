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

BANG_IN(SetBang) {

}

GUI_VALUE() {
  return std::to_string(value.load());
}

CALC() {
  outputs[0].SendFloat(value, thread);
}