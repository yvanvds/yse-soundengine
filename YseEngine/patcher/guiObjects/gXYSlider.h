#pragma once
#include "../math/gExprEval.h"
#include "../pObject.h"
#include <atomic>
#include <cstddef>
#include <string>

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief Two-dimensional control pad ``.xyslider`` (issue #563).
     *
     *  Max's ``pictslider`` — "output numbers by moving a 2-dimensional
     *  slider" — under an honest name: Max's comes from its image-based
     *  rendering, which is a host concern the engine should not carry in an
     *  object name. What the object *is* is one control holding two correlated
     *  axes: filter cutoff against resonance, two FM ratios, a stereo
     *  position, a morph coordinate feeding ``.nodes``. Two independent
     *  ``.slider``s cannot express that the axes move together as one gesture;
     *  here the pair leaves as one message, so nothing downstream can ever see
     *  half a gesture.
     *
     *  **A point, not a span.** The shape is ``.rslider``'s — two float cells,
     *  bounded, on the #551 protocol — but where ``.rslider`` holds two
     *  interchangeable ends of one range and sorts them on the way out, this
     *  object holds two *independent axes* of one position. Cell 0 is always
     *  x and cell 1 always y, whatever their values: sorting a coordinate
     *  pair would fold the plane in half along its diagonal. The only thing
     *  the axes share is the gesture.
     *
     *  **State: two cells, and they are the GUI value** (issue #551's
     *  structured protocol). ``GetGuiValue()`` is the whole position ("x y"),
     *  ``GetGuiValueCount()`` is 2 and ``GetGuiValueAt()`` is one axis at a
     *  time. With two numeric cells a bare "0 1" cannot be told from
     *  "cell 0 := 1", so the protocol's ``set <index> <value>`` keyword is
     *  what disambiguates them — read the protocol block in pObject.h before
     *  changing anything here.
     *
     *  **Bounds, one pair per axis.** ``xminimum``/``xmaximum`` and
     *  ``yminimum``/``ymaximum`` are the field the pad covers — Max's
     *  per-axis range, generalised from "0 to size-1" to arbitrary float
     *  pairs and named to match ``.dial`` and ``.rslider``. Each pair is
     *  taken as ordered, so giving one the wrong way round still bounds
     *  against the right two numbers. The default 0-127 per axis is Max's
     *  default size of 128. Bounding happens on the way out as well as on the
     *  way in — ``.incdec``'s rule, for its reason: a live re-range is
     *  visible immediately, and an untouched ``.xyslider 20 20000 0 1``
     *  reads back as "20. 0." rather than as the 0 0 it was constructed
     *  with.
     *
     *  **The grammar**, ``.rslider``'s exactly, with x on the hot inlet:
     *
     *   - inlet 0 (hot) takes a float or an int as **x**, a list as the whole
     *     position ("x y", the first two numbers), ``set <index> <value>``
     *     for one axis (0 is x, 1 is y), and a bang. Anything that arrives
     *     there emits.
     *   - inlet 1 (cold) takes a float or an int as **y** and stores it
     *     silently, the patcher's hot/cold rule.
     *
     *  **Outlets**, both forms the issue asks for, at once rather than behind
     *  a mode: outlet 0 is x and outlet 1 is y (Max's two outlets, in Max's
     *  order), and outlet 2 is the position as a two-element list — which is
     *  exactly the cursor message ``.nodes`` takes, so the issue's morph use
     *  case is one cord. They fire right to left — outlet 2, then 1, then 0,
     *  the ordering guarantee ``.trigger`` and ``.bangbang`` document — and
     *  all three carry one sample of the state, taken once at the top of
     *  ``Calculate()``: a position that moved mid-send would otherwise report
     *  two different points down two cords of the same event.
     *
     *  All four parameters are scalars and the object registers no
     *  clear/parse callbacks, so ``Parameters::NeedsRebuild()`` is false and
     *  a live re-range rides the wait-free scalar plan (issue #234) instead
     *  of replacing the object.
     *
     *  Real time: every path is a handful of compares, two
     *  ``ExprFormatValue`` renders into stack buffers, an append into a
     *  string reserved at construction and three Sends — no allocation, no
     *  lock, no I/O. The list handler reads its command word in place and
     *  parses through ``ExprParseFloatList`` rather than a stream, because a
     *  list may well arrive on the audio thread down a cord.
     *
     *  The host-thread/audio-thread window on the two axes is ``.slider``'s:
     *  each is a plain ``std::atomic<float>``, so a GUI write and an
     *  audio-thread read never tear. The *pair* is not atomic — a host can
     *  read x from before an edit and y from after it — which is exactly the
     *  non-atomic multi-cell read the protocol names and permits for a
     *  repaint; a host that needs a coherent position uses the bulk read,
     *  which samples both under one call.
     */
    PATCHER_CLASS(gXYSlider, YSE::OBJ::G_XYSLIDER)
    _NO_MESSAGES
    _DO_CALCULATE

    _BANG_IN(BangIn)
    _INT_IN(IntIn)
    _FLOAT_IN(FloatIn)
    _LIST_IN(ListIn)

    _HAS_GUI_CELLS

  private:
    // Characters the two-element list can need without reallocating: two
    // rendered floats and the separator between them.
    static constexpr std::size_t LIST_CAPACITY = (2 * (std::size_t)kExprValueTextMax) + 1;

    // The stored axes, bounded on the way out as well as in — see the class
    // comment. No sorting: x is x however it compares to y.
    float X() const;
    float Y() const;

    // Clamp `value` into the ordered [a, b] pair; a NaN answers the low
    // bound rather than passing through.
    static float Bound(float value, float boundA, float boundB);
    float BoundX(float value) const;
    float BoundY(float value) const;

    // Store one axis, bounded. Emits nothing: the callers that emit do it by
    // being on the hot inlet, and inlet 1 must not.
    void StoreX(float value);
    void StoreY(float value);

    // Render `value` into `out` (at least kExprValueTextMax bytes) the way
    // the list outlet spells it, so the GUI value and the message carry the
    // same text. Returns how many characters were written.
    static std::size_t Render(float value, char* out);

    // The two axes. Written by the host thread, read by the audio thread —
    // see the class comment.
    std::atomic<float> x;
    std::atomic<float> y;

    // Written by the parameter system (the scalar plan applies them on the
    // audio thread at the top of a block), read by every path. Plain fields,
    // exactly as .rslider holds its bounds.
    float xminimum;
    float xmaximum;
    float yminimum;
    float ymaximum;

    // The outlet-2 text, built into memory reserved at construction so the
    // send path never allocates.
    std::string listText;
  };
}
}
