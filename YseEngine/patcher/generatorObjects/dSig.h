#pragma once

#include <array>
#include <string>
#include <vector>
#include "../pObject.h"

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief ``~sig``: numbers in, constant signals out. Ported from Max's
     *         ``sig~`` (issue #1062).
     *
     *  There is one outlet per creation argument (one with none), each a
     *  constant signal holding that argument until a number changes it
     *  (default 0). The single inlet takes:
     *
     *  - a float or an int: the value of the leftmost outlet;
     *  - a list: the values of the outlets from the leftmost on, one number
     *    each, as in Max ("a list sets values for each output corresponding to
     *    the position of the number in the list");
     *  - ``set <index> <value>``: the value of one outlet, 0 the leftmost;
     *  - a signal, whose samples are ignored. As in Max the cord only orders
     *    the graph: ``~sig`` then runs after the object feeding it.
     *
     *  No DSP class: the sample work is a ``DSP::buffer`` fill
     *  (``buffer = value``), already public and usable without a patcher. The
     *  values are atomics, written by the control thread and read once per
     *  block by the audio thread; nothing on the audio path allocates or
     *  locks. Numbers sent after creation are inlet values, not parameters,
     *  so a save keeps the creation arguments only.
     */
    PATCHER_CLASS(dSig, YSE::OBJ::D_SIG)
    _NO_MESSAGES
    _DO_CALCULATE

    _FLOAT_IN(SetFloat)
    _INT_IN(SetInt)
    _LIST_IN(SetList)

    _PARM_CLEAR
    _PARM_PARSE

    /** @brief Most outlets an object builds; more arguments are ignored. */
    static constexpr int MAX_OUTLETS = 64;

    /** @brief How many outlets (constant signals) the object has. */
    int OutletCount() const {
      return static_cast<int>(signals.size());
    }

  private:
    // Rebuild the outlets from the creation arguments. Control thread,
    // before the object is wired or published: the constructor and the two
    // parameter callbacks (a live re-parse rebuilds the object, #234).
    void ShapePorts();

    std::vector<std::string> creationArgs;
    // The value of each outlet, written by the control thread.
    std::array<aFlt, MAX_OUTLETS> values;
    // One output buffer per outlet, sized in ShapePorts.
    std::vector<DSP::buffer> signals;
  };

} // namespace PATCHER
} // namespace YSE
