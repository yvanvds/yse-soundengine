#pragma once
#include "../pObject.h"

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief Control-rate two-way routing by range ``.split`` (issue #448).
     *
     *  Tests the value on inlet 0 against the ``[low, high]`` range and passes
     *  it on **unchanged**: out of outlet 0 when it falls inside the range, out
     *  of outlet 1 when it does not. Exactly one outlet fires per evaluation,
     *  which is what separates ``.split`` from every other object in the range
     *  family — ``.clip``, ``.pong``, ``.scale`` and ``.zmap`` all *reshape* the
     *  value and always emit on their single outlet, where ``.split`` never
     *  touches the value and instead decides *where* it goes.
     *
     *  That is the building block for keyboard splits, velocity layers and
     *  zone-based dispatch: the in-range branch drives the zone's voice, the
     *  out-of-range branch chains into the next ``.split`` for the next zone.
     *
     *  Three inlets, in Max's order: inlet 0 is the hot one and carries the
     *  value, inlets 1 and 2 store the lower and upper limit. Float in, float
     *  out — an int on any inlet is widened, the convention the ``.+`` family
     *  already uses. Both bounds are **inclusive**, as in Max: a value equal to
     *  either limit leaves the left outlet.
     *
     *  Three behaviours are choices rather than ports of Max:
     *
     *  - Max preserves the type of its first typed-in argument, so an int
     *    argument truncates float input. ``.split`` emits a float whatever
     *    arrives, the whole ``g`` family's convention, and routes the value
     *    bit-for-bit as it came in.
     *  - The limits are used as an *ordered* pair, so a range given
     *    high-to-low still splits on the right two numbers — the same choice
     *    ``.clip``, ``.pong``, ``.scale`` and ``.zmap`` make. Max's undefined
     *    range would send everything right.
     *  - The defaults are 0 and 127 rather than Max's zero-initialised pair.
     *    A ``[0, 0]`` range makes a fresh object route all but one value to the
     *    same outlet; 0-127 is the MIDI range the object's own use cases live
     *    in, and matches ``.linedrive``'s default input range.
     *
     *  Nothing is computed, so nothing is sanitised: a non-finite value is
     *  routed rather than substituted. A NaN fails both comparisons and
     *  therefore leaves the *out-of-range* outlet, which is the right answer
     *  for a router — a value that cannot be shown to be inside the range is
     *  outside it — and it keeps the reject branch as the single place a patch
     *  has to guard.
     *
     *  Calculate() is two compares and one Send: no allocation, no lock, no
     *  I/O.
     */
    PATCHER_CLASS(gSplit, YSE::OBJ::G_SPLIT)
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
