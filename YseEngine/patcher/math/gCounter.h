#pragma once
#include "../pObject.h"
#include <atomic>

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief Step counter — ``.counter``.
     *
     *  - inlet 0 (hot) — a bang adds ``step`` to the count and sends it; an
     *    int sets the count and sends it; the list ``reset`` puts the count
     *    back at ``startValue`` without sending.
     *  - inlet 1 (cold) — an int sets ``step``.
     *
     *  ``startValue`` is where the count starts: it is loaded when the
     *  creation arguments are parsed, so ``.counter -1`` sends 0 on its first
     *  bang. An int on inlet 0 moves the count but not ``startValue``, so
     *  ``reset`` always returns to the creation argument (issue #956).
     */
    PATCHER_CLASS(gCounter, YSE::OBJ::G_COUNTER)
    _NO_MESSAGES
    _NO_CALCULATE

    _BANG_IN(Bang)
    _LIST_IN(SetListValue)
    _INT_IN(SetIntValue)

    _PARM_PARSE

    _HAS_GUI

  private:
    aInt step{1};
    aInt startValue{0};
    aInt currentValue{0};
  };
}
}
