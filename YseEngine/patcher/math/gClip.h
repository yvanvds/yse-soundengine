#pragma once
#include "../pObject.h"

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief Control-rate range limiting ``.clip`` (issue #445).
     *
     *  The control-rate counterpart of ``~clip`` (``dClip``): pins a value into
     *  the ``[low, high]`` range and passes anything already inside it through
     *  untouched. The object to reach for when a value must never leave the
     *  legal bounds of the parameter it drives.
     *
     *  Three inlets, in Max's order: inlet 0 is the hot one and carries the
     *  value, inlets 1 and 2 store the lower and upper limit. Float in, float
     *  out — an int on any inlet is widened, the convention the ``.+`` family
     *  already uses.
     *
     *  Distinct from ``.zmap``, which *maps* one range onto another before
     *  clipping, and from a folding/wrapping object, which reflects an
     *  out-of-range value back rather than pinning it.
     *
     *  Two behaviours are choices rather than ports of Max, both shared with
     *  the range-mapping pair so the math family stays consistent:
     *
     *  - The limits are used as an *ordered* pair, so a range given high-to-low
     *    (``low`` above ``high``) still clips against the right two numbers
     *    rather than collapsing onto one of them.
     *  - A NaN on the hot inlet emits the clipped 0 rather than propagating,
     *    the convention ``./``, ``.sqrt`` and ``.zmap`` already use. The output
     *    is therefore always finite and always inside the range.
     *
     *  Defaults are ``~clip``'s -1 and 1, so the two halves of the pair behave
     *  alike out of the box.
     *
     *  Calculate() is a handful of compares and one Send: no allocation, no
     *  lock, no I/O.
     */
    PATCHER_CLASS(gClip, YSE::OBJ::G_CLIP)
    _NO_MESSAGES
    _DO_CALCULATE

    _FLOAT_IN(SetFloat)
    _INT_IN(SetInt)

  private:
    // Shared by both inlet handlers: inlet 0 stores the value, 1 and 2 the
    // limits. Keeping the dispatch in one place means the int path is exactly
    // the float path with a widening cast.
    void Store(float value, int inlet);

    float input;
    float low;
    float high;
  };
}
}
