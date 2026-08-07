#pragma once
#include "../pObject.h"
#include <cstddef>
#include <string>
#include <vector>

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief Send one input to many outlets in a defined right-to-left order —
     *         ``.trigger`` (issue #466).
     *
     *  Max's ``trigger``, and the only object in the patcher whose *point* is
     *  ordering. Everywhere else in a message graph the order in which two
     *  branches run is an accident of how the patch was wired; ``.trigger``
     *  makes it a statement. Max: "Outputs any input received in order from
     *  right to left and formatted according to the object-argument specified."
     *
     *  The name is ``.trigger`` only. Max's short spelling is ``t``, and ``.t``
     *  is already this patcher's toggle.
     *
     *  ### The ordering guarantee, and where it comes from
     *
     *  Outlet *n-1* is sent first and outlet 0 last, and each send **completes
     *  in full** — the whole subgraph hanging off that outlet, depth first —
     *  before the next one starts. That is not an accident of this object: it
     *  falls out of the patcher's synchronous send path, where
     *  ``outlet::Send*`` walks its target list calling ``inlet::Set*``
     *  directly, with no queue in between. The pinned-``GraphState`` route of
     *  #226 changes *which* adjacency list is walked on the audio thread, not
     *  that it is walked in order, so the guarantee holds on the audio thread
     *  and the control thread alike.
     *
     *  Which is why it is worth spelling out what the guarantee buys: a patch
     *  that has to set a value *and then* trigger something reads
     *  ``.trigger b i`` — the int leaves the right outlet and lands in the
     *  cold inlet, and only then does the bang leave the left one. Reverse the
     *  order and the bang fires the old value. This is the object that makes
     *  that expressible, and the reason it is worth a whole box.
     *
     *  Not guaranteed, and deliberately so: the order in which *several patch
     *  cords from the same outlet* are served. That is Max's position too, and
     *  the answer to needing it is another ``.trigger``.
     *
     *  ### The arguments
     *
     *  One outlet per argument, in argument order, and each argument is either
     *  a **format letter** or a **constant**.
     *
     *  The five format letters convert whatever arrives:
     *
     *  | arg | outlet | int in | float in | list in | bang in |
     *  |-----|--------|--------|----------|---------|---------|
     *  | ``i`` | INT   | the int | truncated | 0 | 0 |
     *  | ``f`` | FLOAT | widened | the float | 0. | 0. |
     *  | ``b`` | BANG  | bang | bang | bang | bang |
     *  | ``l`` | LIST  | ``0`` | ``0`` | unchanged | ``0`` |
     *  | ``s`` | LIST  | ``""`` | ``""`` | unchanged | ``""`` |
     *
     *  which is Max's table: "A symbol, list, or bang received in the inlet
     *  will be converted to integer 0 by an i outlet, and to float 0. by an f
     *  argument"; "A list received in the inlet will be sent out unchanged by
     *  an l outlet. Anything else will be converted to the single-item list 0";
     *  "A symbol received in the inlet will be sent out unchanged by an s
     *  outlet. Anything else will be converted to the null symbol".
     *
     *  Anything that is not one of those five letters is a **constant**, and a
     *  constant outlet emits its own value on every input, ignoring what
     *  arrived. Max: "When an int, float, or symbol is specified, the value is
     *  output as a constant." A token that reads as a whole finite number is a
     *  numeric constant — an **int** constant when it is spelled as one and a
     *  **float** constant when it carries a ``.`` or an exponent, which is how
     *  Max's own parser tells the two atoms apart — and anything else is a
     *  symbol constant that goes out as its own text.
     *
     *  With no arguments there are **two** outlets, both ``i``. Max's default,
     *  and the same shape as ``.trigger i i``.
     *
     *  ### The one deviation: ``s`` and ``l`` on a list
     *
     *  This patcher has no symbol message. Text travels as a **list** message
     *  carrying a string, which is how ``.sel``, ``.route`` and ``.regexp``
     *  already read symbols, so an ``s`` outlet cannot ask whether what arrived
     *  was a symbol or a list — there is one message type behind both. Rather
     *  than invent a rule (single token = symbol, several = list) that no other
     *  object in the patcher follows, a list message passes through **both**
     *  ``l`` and ``s`` unchanged.
     *
     *  What survives is exactly the half of Max's distinction this patcher can
     *  represent, and it is the useful half: what the two do with everything
     *  *else*. A number or a bang leaves an ``l`` outlet as the single-item
     *  list ``0`` and an ``s`` outlet as the empty symbol. So ``.trigger l``
     *  and ``.trigger s`` still differ, and differ in the way Max says they do.
     *
     *  Note that this makes ``i`` and ``f`` behave on list input the way Max
     *  says they behave on *symbol* input too: the list ``5 6`` leaves an ``i``
     *  outlet as 0, not as 5. That is deliberate. ``.trigger`` is not a list
     *  reader — ``.route``, ``.sel`` and ``.unpack``-shaped objects are — and
     *  Max is explicit that a list reaching an ``i`` outlet becomes 0.
     *
     *  ### Real-time behaviour
     *
     *  ``Calculate()`` does nothing. The object is driven by its inlet, and a
     *  ``Calculate()`` that emitted would re-fire the whole fan-out on every
     *  DSP tick after the inlet last ran — the rule ``.sel``, ``.route`` and
     *  ``.past`` establish.
     *
     *  No path allocates, locks or blocks. The slot table is built on the
     *  control thread by the parameter callbacks, before the object is wired or
     *  published, and is never resized afterwards; no message handler writes to
     *  it at all. Emitting is a reverse walk over that table doing one
     *  ``Send`` per slot, with the two substituted texts (``"0"`` and the empty
     *  symbol) held as members so that even the fallback path hands over an
     *  existing string rather than building one. Float-to-int truncation goes
     *  through the shared ``ExprToInt``, which answers the inputs C leaves
     *  undefined — a NaN, an infinity, anything outside the int range — with 0
     *  rather than with whatever the hardware does.
     */
    PATCHER_CLASS(gTrigger, YSE::OBJ::G_TRIGGER)
    _NO_MESSAGES
    _NO_CALCULATE

    _BANG_IN(SetBang)
    _INT_IN(SetInt)
    _FLOAT_IN(SetFloat)
    _LIST_IN(SetList)

    _PARM_CLEAR
    _PARM_PARSE

    /**
     *  @brief Most outlets the object will build.
     *
     *  256, the ceiling ``.sel``, ``.past``, ``.mean`` and ``.vexpr`` already
     *  use for a list argument. Max documents no limit; arguments past the
     *  ceiling are dropped.
     */
    static constexpr int MAX_OUTLETS = 256;

    /** @brief What one outlet does with the input. */
    enum class Kind {
      INT, ///< ``i`` — the number as an int, or 0
      FLOAT, ///< ``f`` — the number as a float, or 0.
      BANG, ///< ``b`` — a bang, whatever arrived
      LIST, ///< ``l`` — the list unchanged, or the single-item list ``0``
      SYMBOL, ///< ``s`` — the list unchanged, or the empty symbol
      CONST_INT, ///< an int constant, emitted on every input
      CONST_FLOAT, ///< a float constant, emitted on every input
      CONST_SYMBOL, ///< a symbol constant, emitted on every input
    };

    /** @brief How many outlets the object has. At least one. */
    int SlotCount() const {
      return (int)slots.size();
    }

    /** @brief What outlet @p index does. ``BANG`` for an index out of range. */
    Kind SlotKind(int index) const;

    /** @brief The value of an ``CONST_INT`` slot, else 0. */
    int SlotInt(int index) const;

    /** @brief The value of a ``CONST_FLOAT`` slot, else 0. */
    float SlotFloat(int index) const;

    /** @brief The text of a ``CONST_SYMBOL`` slot, else "". */
    std::string SlotText(int index) const;

  private:
    // One creation argument, resolved once. `kind` decides which of the value
    // fields means anything; a format slot uses none of them.
    struct Slot {
      Kind kind = Kind::INT;
      int intValue = 0;
      float floatValue = 0.f;
      std::string text;
    };

    // What arrived at the inlet, in the one shape the emitter understands.
    // Built in the caller's frame, so nothing here outlives the send.
    struct Incoming {
      enum Type { BANG, INT, FLOAT, LIST };
      Type type = BANG;
      int intValue = 0;
      float floatValue = 0.f;
      const std::string* text = nullptr; // LIST only
    };

    // The whole object: walk the slots right to left and send one message per
    // slot. Shared by all four inlet handlers, so there is exactly one place
    // the firing order is decided.
    void EmitAll(const Incoming& in, THREAD thread);

    // Rebuild the outlets from the current slot table, docs included. Control
    // thread only: called from the constructor and from the parameter
    // callbacks, all of which run before the object is wired or published.
    void ShapePorts();

    // Max's no-argument case: two `i` outlets.
    void ResetToDefaultSlots();

    // The creation argument, as tokens. Control thread only: written by
    // Parameters::Set, read by ParseParams(), never by a message handler.
    std::vector<std::string> formatArgs;

    // The resolved outlets. Sized by ParseParams() / ClearParams() before the
    // object is published and never touched by a message handler, so the
    // emitter's walk over it cannot race anything.
    std::vector<Slot> slots;

    // The two substitutions, held rather than built: an `l` outlet fed
    // anything but a list sends the single-item list "0", and an `s` outlet
    // fed anything but a list sends the empty symbol. Members so the send path
    // hands over an existing string instead of constructing one.
    std::string zeroList;
    std::string nullSymbol;
  };
}
}
