#include "gMessage.h"
#include "../pObjectList.hpp"

using namespace YSE::PATCHER;

#define className gMessage

CONSTRUCT() {
  ADD_IN_0;
  REG_BANG_IN(Bang);
  REG_LIST_IN(SetValue);

  // ANY: the text leaves as whatever message it spells (issue #933).
  ADD_OUT_ANY;

  ADD_PARAM(words);
  REG_PARM_CLEAR;
  REG_PARM_PARSE;

  ADD_DESCRIPTION("A message box. A bang sends the stored text as the message it spells: a number, "
                  "a bang, a list or a command. A message arriving on the inlet replaces the text "
                  "without sending it.");
  ADD_CATEGORY(pCategory::GUI);
  INLET_DOC(0, "trigger/set",
            "A bang sends the stored message; a list or a word message replaces it silently.", "");
  OUTLET_DOC(0, "out",
             "The stored message, as the message it spells: one number is an int or a float, the "
             "word 'bang' is a bang, a list starting with a number is a list, and text starting "
             "with any other word is a command to objects that take one ('stop' to '~line', "
             "'allnotesoff' to '.midiout') and a list to every other object.",
             "");
  PARAM_DOC("message", "", "The text the box starts with.", "any text");
}

// Parameters::Set calls this before parsing, so a re-parse replaces the text
// rather than appending to it, and SetParams("") leaves an empty box.
PARM_CLEAR() {
  words.clear();
  message.clear();
}

PARM_PARSE() {
  // Control thread, on an object the audio thread cannot see yet (a live
  // SetParams on a published object is a #234 rebuild), so joining here may
  // allocate. Single spaces, as a list message is tokenized: `.m set 440 0.5`
  // sends `set 440 0.5`, as in Max.
  for (std::size_t i = 0; i < words.size(); i++) {
    if (i > 0) message += ' ';
    message += words[i];
  }
}

BANG_IN(Bang) {
  // Each receiving inlet reads the text as the typed message it spells; see
  // inlet::SetMessage (issue #933).
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