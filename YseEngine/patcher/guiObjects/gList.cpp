#include "gList.h"
#include "../pObjectList.hpp"

using namespace YSE::PATCHER;

#define className gList

CONSTRUCT() {
  ADD_IN_0;
  REG_BANG_IN(Bang);
  REG_LIST_IN(SetValue);

  ADD_OUT_LIST;

  ADD_PARAM(words);
  REG_PARM_CLEAR;
  REG_PARM_PARSE;

  ADD_DESCRIPTION("A list box. A bang sends the stored list; a list arriving on the inlet replaces "
                  "it without sending it.");
  ADD_CATEGORY(pCategory::GUI);
  INLET_DOC(0, "trigger/set", "A bang sends the stored list; a list replaces it silently.", "");
  OUTLET_DOC(0, "out", "The stored list.", "");
  PARAM_DOC("message", "", "The list the box starts with.", "any text");
}

// Parameters::Set calls this before parsing, so a re-parse replaces the list
// rather than appending to it, and SetParams("") leaves an empty box.
PARM_CLEAR() {
  words.clear();
  message.clear();
}

PARM_PARSE() {
  // Control thread, on an object the audio thread cannot see yet (a live
  // SetParams on a published object is a #234 rebuild), so joining here may
  // allocate. Single spaces, as a list message is tokenized: `.l a b c` starts
  // out holding `a b c`, the same as sending it that list.
  for (std::size_t i = 0; i < words.size(); i++) {
    if (i > 0) message += ' ';
    message += words[i];
  }
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