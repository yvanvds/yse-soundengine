#include "gMessage.h"
#include "../pObjectList.hpp"

using namespace YSE::PATCHER;

#define className gMessage

CONSTRUCT() {
  ADD_IN_0;
  REG_BANG_IN(Bang);
  REG_LIST_IN(SetValue);

  ADD_OUT_LIST;

  ADD_PARAM(message);

  ADD_DESCRIPTION(
      "Static message. Bang re-sends the stored message; list updates the stored payload.");
  ADD_CATEGORY(pCategory::GUI);
  INLET_DOC(0, "trigger/set", "Bang re-sends the stored message; list replaces it.", "");
  OUTLET_DOC(0, "out", "Stored message broadcast as a SetMessage.", "");
  PARAM_DOC("message", "", "Initial message payload.", "any string");
}

BANG_IN(Bang) {
  outputs[0].SendMessage(message, thread);
}

LIST_IN(SetValue) {
  message = value;
}

GUI_VALUE() {
  return message;
}

MESSAGES() {
  this->message = message;
}