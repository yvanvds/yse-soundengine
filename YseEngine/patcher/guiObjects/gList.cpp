#include "gList.h"
#include "../pObjectList.hpp"

using namespace YSE::PATCHER;

#define className gList

CONSTRUCT() {
  ADD_IN_0;
  REG_BANG_IN(Bang);
  REG_LIST_IN(SetValue);

  ADD_OUT_LIST;

  ADD_PARAM(message);

  ADD_DESCRIPTION("A list box. A bang sends the stored list; a list arriving on the inlet replaces "
                  "it without sending it.");
  ADD_CATEGORY(pCategory::GUI);
  INLET_DOC(0, "trigger/set", "A bang sends the stored list; a list replaces it silently.", "");
  OUTLET_DOC(0, "out", "The stored list.", "");
  PARAM_DOC("message", "", "The list the box starts with.", "any text");
}

BANG_IN(Bang) {
  outputs[0].SendList(message, thread);
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