#pragma once
#include "../pObject.h"
#include <cstddef>
#include <string>
#include <vector>

namespace YSE {
  namespace PATCHER {

    PATCHER_CLASS(gRoute, YSE::OBJ::G_ROUTE)
    _NO_MESSAGES
    _NO_CALCULATE

    _INT_IN(SetIntValue)
    _FLOAT_IN(SetFloatValue)
    _BANG_IN(SetBangValue)
    _LIST_IN(SetListValue)

    _PARM_CLEAR
    _PARM_PARSE

  private:
    static constexpr int MAX_SELECTORS = 256;
    static constexpr std::size_t TEXT_CAPACITY = 256;

    struct Selector {
      std::string text;
      float value;
      bool numeric;
    };

    int MatchNumber(float value) const;
    int MatchSymbol(const char* text, std::size_t length) const;
    void ShapePorts();

    std::vector<std::string> selectorArgs;
    std::vector<Selector> selectors;
    std::string remainder;
  };
}
}
