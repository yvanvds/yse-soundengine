#include "gInt.h"
#include "..\pObjectList.hpp"
#include "implementations\logImplementation.h"

using namespace YSE::PATCHER;

#define className gInt

CONSTRUCT() {
  ADD_IN_0;
  REG_INT_IN(SetInt);
  REG_FLOAT_IN(SetFloat);
  REG_BANG_IN(Bang);

  ADD_IN_1;
  REG_INT_IN(SetInt);
  REG_FLOAT_IN(SetFloat);
  
  ADD_OUT_INT;

  ADD_PARAM(value);

  value = 0;
}

INT_IN(SetInt) {
  this->value = value;
  //INTERNAL::LogImpl().emit(E_DEBUG, "Patcher: gInt input " + std::to_string(value));
}

FLOAT_IN(SetFloat) {
  this->value = (int)value;
  //INTERNAL::LogImpl().emit(E_DEBUG, "Patcher: gInt input " + std::to_string(value));
}

BANG_IN(Bang) {
  // nothing to do here, but needed to trigger output of value
}

GUI_VALUE(){
  return std::to_string(value.load());
}

CALC() {
  outputs[0].SendInt(value, thread);
  //INTERNAL::LogImpl().emit(E_DEBUG, "Patcher: gInt output " + std::to_string(value));
}