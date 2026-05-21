#include "gRoute.h"
#include "../pObjectList.hpp"

using namespace YSE::PATCHER;
#define className gRoute

CONSTRUCT() {
  ADD_IN_0;
  REG_BANG_IN(SetBangValue);
  REG_INT_IN(SetIntValue);
  REG_FLOAT_IN(SetFloatValue);
  REG_LIST_IN(SetListValue);

  REG_PARM_CLEAR;
  REG_PARM_PARSE;

  ADD_PARAM(list);

  ADD_DESCRIPTION("Match-and-route. Compares the incoming value to each token in the list parameter and forwards to the matching outlet; unmatched values go to the final fall-through outlet. Outlets are created when the list parameter is set.");
  ADD_CATEGORY(pCategory::GENERIC);
  INLET_DOC(0, "in", "Value inlet — accepts bang / int / float / list.", "");
  PARAM_DOC("list", "", "Space-separated list of match tokens; one outlet is created per token plus one fall-through outlet.", "any tokens");
}

PARM_CLEAR() {
  outputs.clear();
}

PARM_PARSE() {
  while (outputs.size() < list.size() + 1) {
    ADD_OUT_ANY;
  }
}

BANG_IN(SetBangValue) {
  for (unsigned int i = 0; i < list.size(); i++) {
    if (list[i].compare("bang") == 0) {
      outputs[i].SendBang(thread);
      return;
    }
  }
  outputs.back().SendBang(thread);
}

INT_IN(SetIntValue) {
  std::string s = std::to_string(value);
  for (unsigned int i = 0; i < list.size(); i++) {
    if (list[i].compare(s) == 0) {
      outputs[i].SendInt(value, thread);
      return;
    }
  }
  outputs.back().SendInt(value, thread);
}

FLOAT_IN(SetFloatValue) {
  std::string s = std::to_string(value);
  for (unsigned int i = 0; i < list.size(); i++) {
    if (list[i].compare(s) == 0) {
      outputs[i].SendFloat(value, thread);
      return;
    }
  }
  outputs.back().SendFloat(value, thread);
}

LIST_IN(SetListValue) {
  // only compare first list item (string until first space)
  std::string token;
  size_t pos = 0;
  if ((pos = value.find(" ")) != std::string::npos) {
    token = value.substr(0, pos);
  }
  else {
    token = value;
  }

  for (unsigned int i = 0; i < list.size(); i++) {
    if (list[i].compare(token) == 0) {
      outputs[i].SendList(value, thread);
      return;
    }
  }
  outputs.back().SendList(value, thread);
}