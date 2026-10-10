#pragma once
#include "../math/gExprEval.h"
#include "../pObject.h"
#include <atomic>
#include <cstddef>
#include <string>

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief Range control ``.rslider`` (issue #553).
     *
     *  Max's ``rslider`` — "display or change a range of numbers". Every other
     *  control in the patcher holds a *point*: ``.slider`` a position,
     *  ``.dial`` a value, ``.incdec`` a step. This one holds a **span**, which
     *  is the shape a velocity layer, a key split, a random spread and every
     *  min/max pair in ``music/`` actually have. Expressing one today takes two
     *  controls and the convention that they belong together; here it is one
     *  object, and the two ends cannot drift apart or arrive in the wrong
     *  order.
     *
     *  **State: two cells, and they are the GUI value.** This is the first
     *  object built on issue #551's structured GUI protocol —
     *  ``GetGuiValue()`` is the whole state ("low high"),
     *  ``GetGuiValueCount()`` is 2 and ``GetGuiValueAt()`` is one end at a
     *  time — and the object the protocol's ``set <index> <value>`` keyword was
     *  designed for: with two numeric cells, a bare "0 1" cannot be told from
     *  "cell 0 := 1", so the keyword is what disambiguates them. Read the
     *  protocol block in pObject.h before changing anything here.
     *
     *  Cells are **presented ordered**: cell 0 is always the low end. What the
     *  two inlets write is Max's model, and Max states it plainly — "in the
     *  right inlet, the number is taken as *one end* of the range; the left
     *  inlet sets *the other end*". Neither inlet is the low one. The two ends
     *  are stored as they were written and sorted on the way out, which is the
     *  other half of Max's rule — "drawing the range with the mouse always
     *  outputs the lowest value out the left outlet". So writing an end past
     *  the other one does not collide, clamp or collapse the span: it makes
     *  that end the top of the range and the untouched one the bottom. That is
     *  also what makes ``GetGuiValue()`` round-trip through inlet 0 unchanged,
     *  which the protocol requires of any object claiming
     *  ``GuiValueIsSettable()`` — the state it hands out is sorted, so feeding
     *  it back writes each end to the slot it came from.
     *
     *  **Bounds.** ``minimum`` and ``maximum`` are the span the slider covers,
     *  *not* the current range: they are the ends the handles can reach, Max's
     *  ``size`` attribute generalised from "0 to size-1" to an arbitrary float
     *  pair, and named to match ``.dial`` and ``.incdec``. They are taken as an
     *  ordered pair, so giving them the wrong way round still bounds against
     *  the right two numbers. The default 0-127 is Max's default size of 128.
     *
     *  Bounding, like ordering, happens on the way out as well as on the way
     *  in — ``.incdec``'s rule and for its reason: it makes a live re-range
     *  visible immediately, and it keeps the object's promise on a range that
     *  was never touched, so a ``.rslider 20 20000`` reads back as "20. 20."
     *  rather than as the 0 0 it was constructed with.
     *
     *  **The grammar**, and it is Max's, with one deliberate exception:
     *
     *   - inlet 0 (hot) takes a float or an int as **one end** of the range, a
     *     list as the whole range ("low high", the first two numbers),
     *     ``set <index> <value>`` for one end, and a bang. Anything that
     *     arrives there emits.
     *   - inlet 1 (cold) takes a float or an int as **the other end** and
     *     stores it silently, exactly as Max's right inlet does.
     *
     *  The exception is Max's ``set <min> <max>``, which stores both ends
     *  without output. It is not carried over, because the same keyword is
     *  spoken for: #551 gives ``set`` to the protocol's one-cell write, and one
     *  word cannot mean both "two numbers, no output" and "an index and a
     *  value". The bare list already stores both ends, so nothing is lost but
     *  the silence — and silence is not available on inlet 0 anyway: a hot
     *  inlet in this patcher calculates after every message it accepts, which
     *  the protocol explicitly leaves to each object ("whether a restore also
     *  emits on its outlet is a per-object decision").
     *
     *  **Outlets**, both forms the issue asks for, at once rather than behind a
     *  mode: outlet 0 is the low end and outlet 1 the high end (Max's two
     *  outlets, in Max's order), and outlet 2 is the pair as a two-element
     *  list, which is Max's ``listmode`` without the switch. They fire
     *  right to left — outlet 2, then 1, then 0 — the ordering guarantee
     *  ``.trigger`` and ``.bangbang`` document, so the low end lands last and a
     *  patch that wires the two ends separately sees them in Max's order. All
     *  three carry one sample of the state, taken once at the top of
     *  ``Calculate()``: a range that changed mid-send would otherwise report
     *  two different spans down two cords of the same event.
     *
     *  Both parameters are scalars and the object registers no clear/parse
     *  callbacks, so ``Parameters::NeedsRebuild()`` is false and a live
     *  re-range rides the wait-free scalar plan (issue #234) instead of
     *  replacing the object.
     *
     *  Real time: every path is a handful of compares, two ``ExprFormatValue``
     *  renders into stack buffers, an append into a string reserved at
     *  construction and three Sends — no allocation, no lock, no I/O. The list
     *  handler reads its command word in place and parses through
     *  ``ExprParseFloatList`` rather than a stream, because a list may well
     *  arrive on the audio thread down a cord.
     *
     *  The host-thread/audio-thread window on the two ends is ``.slider``'s:
     *  each is a plain ``std::atomic<float>``, so a GUI write and an
     *  audio-thread read never tear. The *pair* is not atomic — a host can read
     *  cell 0 from before an edit and cell 1 from after it — which is exactly
     *  the non-atomic multi-cell read the protocol names and permits for a
     *  repaint; a host that needs a coherent pair uses the bulk read, which
     *  samples both under one call.
     */
    PATCHER_CLASS(gRSlider, YSE::OBJ::G_RSLIDER)
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

    // The stored ends, bounded and sorted — see the class comment on why both
    // happen on the way out as well as on the way in. "Low" and "High" are
    // what comes *out*; the two slots below are not themselves ordered.
    float Low() const;
    float High() const;

    // Clamp `value` into the ordered [minimum, maximum] bounds.
    float Bound(float value) const;

    // Store the end inlet 0 / inlet 1 writes, bounded. Emits nothing: the
    // callers that emit do it by being on the hot inlet, and inlet 1 must not.
    void StoreFirst(float value);
    void StoreSecond(float value);

    // Render `value` into `out` (at least kExprValueTextMax bytes) the way the
    // list outlet spells it, so the GUI value and the message carry the same
    // text. Returns how many characters were written.
    static std::size_t Render(float value, char* out);

    // The two ends, each holding whatever its inlet last wrote rather than the
    // lower or the higher of the pair. Written by the host thread, read by the
    // audio thread — see the class comment.
    std::atomic<float> first;
    std::atomic<float> second;

    // Written by the parameter system (the scalar plan applies them on the
    // audio thread at the top of a block), read by every path. Plain fields,
    // exactly as .incdec holds its bounds.
    float minimum;
    float maximum;

    // The outlet-2 text, built into memory reserved at construction so the
    // send path never allocates.
    std::string listText;
  };
}
}
