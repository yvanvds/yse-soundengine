#pragma once
#include "../pObject.h"

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief Control-rate range limiting by folding or wrapping ``.pong``
     *         (issue #446).
     *
     *  Where ``.clip`` flattens an out-of-range value against the boundary,
     *  ``.pong`` reflects it back into the range (*fold*) or carries it around
     *  to the other side (*wrap*). That is what keeps generative pitch material
     *  inside a register without collapsing every excursion onto the same
     *  boundary note, and what makes a free-running counter behave like a
     *  phase.
     *
     *  Three inlets, in Max's order: inlet 0 is the hot one and carries the
     *  value, inlets 1 and 2 store the lower and upper limit. Float in, float
     *  out — an int on any inlet is widened, the convention the ``.+`` family
     *  already uses. The mode is a *parameter* and has no inlet, because in Max
     *  it is the ``@mode`` attribute rather than a signal.
     *
     *  The four modes carry Max's numbering, in the order its ``@mode``
     *  attribute lists them:
     *
     *  | value | mode | out-of-range behaviour                              |
     *  |-------|------|----------------------------------------------------|
     *  | 0     | none | passed through unchanged                           |
     *  | 1     | clip | pinned to the nearest limit, exactly like ``.clip`` |
     *  | 2     | wrap | carried around to the other side of the range      |
     *  | 3     | fold | reflected back into the range                      |
     *
     *  Three behaviours are choices rather than ports of Max:
     *
     *  - The default mode is **fold**, not Max's ``none``. An object whose
     *    default makes it a no-op is a trap; ``.pong`` defaults to the
     *    behaviour it is named for. ``mode 0`` still gives the pass-through.
     *  - The limits are used as an *ordered* pair, so a range given
     *    high-to-low still folds against the right two numbers — the same
     *    choice ``.clip``, ``.scale`` and ``.zmap`` make.
     *  - A NaN on the hot inlet emits 0 folded into the range and an infinity
     *    emits the matching limit, so in every mode but ``none`` the output is
     *    always finite and always inside the range.
     *
     *  Defaults are ``.clip``'s (and ``~clip``'s) -1 and 1, so the two range
     *  limiters behave alike out of the box.
     *
     *  Calculate() is a couple of compares and at most one ``std::fmod``: no
     *  allocation, no lock, no I/O.
     */
    PATCHER_CLASS(gPong, YSE::OBJ::G_PONG)
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
    // 0 none, 1 clip, 2 wrap, 3 fold — Max's @mode ordering. Parameter only,
    // no inlet: it mirrors a Max attribute, not a signal.
    int mode;
  };
}
}
