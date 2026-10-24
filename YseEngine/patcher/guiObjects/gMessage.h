#pragma once
#include "../pObject.h"
#include <string>
#include <vector>

namespace YSE {
  namespace PATCHER {

    PATCHER_CLASS(gMessage, YSE::OBJ::G_MESSAGE)
    _DO_MESSAGES
    _NO_CALCULATE

    _BANG_IN(Bang)
    _LIST_IN(SetValue)

    _PARM_CLEAR
    _PARM_PARSE

    _HAS_GUI
  private:
    std::string message;

    // The creation text, as tokens (issue #1194): a LIST parameter takes every
    // word, where a STRING one kept only the first. Control thread only —
    // written by Parameters::Set, joined into `message` by ParseParams().
    std::vector<std::string> words;
  };
}
}
