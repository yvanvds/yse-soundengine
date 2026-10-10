#pragma once
#include "../pObject.h"
#include <cstddef>
#include <string>
#include <vector>

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief Join several items, each held in its own inlet, into one symbol —
     *         ``.combine`` (issue #491).
     *
     *  Max's ``combine``, which "combines a list of items into a single symbol"
     *  and "works similar to ``pack`` and ``sprintf``". "The number of inlets is
     *  determined by arguments provided": each creation argument becomes one
     *  inlet *and* that inlet's starting value, every inlet remembers the last
     *  item it was given, and the output is those items written out one after
     *  another with nothing in between.
     *
     *  ### Why this is not ``.tosymbol`` under another name
     *
     *  The two do look alike — both produce a single whitespace-free token, the
     *  only thing "a symbol" can honestly mean in a patcher whose messages are
     *  text and whose readers all split on whitespace (see ``gSymbolBase``). The
     *  difference is not cosmetic, and it is not merely *how* the items arrive:
     *
     *  - **``.combine`` has a memory, one cell per inlet.** ``.tosymbol``
     *    collapses one message that has already arrived whole; it has a single
     *    inlet and keeps nothing between messages. ``.combine`` assembles a
     *    symbol out of parts that arrive at *different times from different
     *    sources* — the address ``synth.lead.note`` built from a ``.route``
     *    branch, a ``.counter`` and a constant, each updating on its own
     *    schedule. No object in this patcher could do that: ``.bondo`` and
     *    ``.buddy`` synchronise arrivals but hand them on through separate
     *    outlets, and nothing joined them back into one token.
     *  - **there is no separator, because every separator is an item.**
     *    ``.tosymbol`` applies one uniform separator between every pair of
     *    tokens. ``.combine`` concatenates exactly what it holds, so a patch
     *    spells the punctuation as arguments — ``.combine synth . lead . note``
     *    is six inlets, three of them constants — and the separators may all
     *    differ. Expressed in ``.tosymbol``'s terms, ``.combine`` is the *empty*
     *    separator applied *across* inlets rather than *within* a message.
     *  - **which inlets emit is configurable.** ``.tosymbol`` has one inlet and
     *    it is hot. Here the hot inlets are Max's ``triggers``, below.
     *
     *  The nearest object is really ``.sprintf`` (#489), and Max says so too: a
     *  ``.combine`` is a ``.sprintf`` whose format is ``%s%s…`` with the literal
     *  text supplied as arguments instead of typed into a format. It is kept as
     *  its own object for the reason Max keeps both — the format is the thing
     *  that has to be got right in ``.sprintf``, and here there is no format to
     *  get wrong, only items. What ``.sprintf`` still does that this cannot is
     *  spell a number *into* a field (widths, precisions, zero padding); Max's
     *  ``@padding`` attribute is that one corner of ``sprintf`` folded back into
     *  ``combine``, and it is deliberately not reproduced — ``%03ld`` in a
     *  ``.sprintf`` already says it, and a second spelling of the same idea in
     *  an object that otherwise has no notion of a number would earn its keep
     *  only by making the argument list ambiguous.
     *
     *  ### Items, and what an inlet stores
     *
     *  Every item is text. A number arriving at an inlet is stored as the text
     *  that spells it, through ``ExprFormatValue`` — the same writer the rest of
     *  the family uses, so a value a ``.counter`` computed reads back as the
     *  number a patch author would have typed and the decimal separator is
     *  always ``.`` whatever another thread has done to the locale. A **list**
     *  is spread one token per item from the receiving inlet rightwards, Max's
     *  and ``.sprintf``'s "each item in the list is treated as if it had been
     *  received in a separate inlet, up to the number of inlets"; tokens past
     *  the last item are dropped. An **empty** message on an inlet clears that
     *  item — the ``.prepend`` reading, where empty is a real request rather
     *  than a malformed one, and the only way a patch can take a component back
     *  out of an address it is building.
     *
     *  An item never set, or cleared, renders as **nothing**: Max's "if no value
     *  has been received for a ``%s`` argument, that argument will be left
     *  blank", which is also the only rendering that keeps the result a single
     *  token. A ``.combine`` holding nothing at all sends **nothing** rather
     *  than an empty message — the ``.sprintf`` / ``.prepend`` rule that makes
     *  an unconfigured object safe to drop into a working patch.
     *
     *  ### ``triggers``
     *
     *  Max spells it as the ``@triggers`` attribute, "a list of inputs that will
     *  automatically trigger output", with -1 meaning every inlet. This patcher
     *  has no attribute syntax, so it is a **leading creation argument** —
     *  ``.combine triggers -1 a b c`` — the way ``.sprintf`` reads and consumes
     *  Max's leading ``symout``. It is deliberately *not* a reserved word on
     *  inlet 0: that is the ``.prepend`` discipline, and it matters more here
     *  than almost anywhere, since this object exists to carry arbitrary words
     *  and a reserved one would swallow the very items it is being asked to
     *  join. A creation argument is a different thing — the patch author typed
     *  it, on the control thread, before the object had inlets at all.
     *
     *  ``triggers`` takes exactly **one** token: ``-1`` for every inlet hot
     *  (Max's ``pak``-like mode), or an inlet number for that one alone. Max
     *  allows an arbitrary subset; that is narrowed here because the argument
     *  list has no bracket to end a sublist with, and every token after
     *  ``triggers`` would otherwise be ambiguous between a further trigger and
     *  the first item. Without the flag the hot inlet is inlet 0, Max's default.
     *
     *  A **bang** on inlet 0 sends the current items whatever ``triggers`` says
     *  — Max's "bang: sends stored items as combined symbol" — so a patch always
     *  has an explicit way to ask for the result. Cold inlets decline a bang,
     *  since they have nothing to do with one, which also leaves
     *  ``inlet::GetAcceptedTypes()`` reporting the real contract.
     *
     *  ### Real-time behaviour
     *
     *  ``Calculate()`` does nothing: the object is driven by its inlets, and one
     *  that emitted would re-send on every DSP tick after an inlet fired — the
     *  rule ``.route``, ``.sel``, ``.prepend``, ``.substitute`` and ``.sprintf``
     *  establish.
     *
     *  No message path allocates, locks or blocks. The items and the output
     *  buffer are sized by ``ShapePorts()`` on the control thread before the
     *  object is wired or published; an item holds ``ITEM_CAPACITY`` characters
     *  reserved there and refuses a longer token rather than truncating it
     *  (silently on the inlet, which may be the audio thread; loudly on a
     *  creation argument, which is control-thread only), and the output buffer
     *  is reserved for every item at full length. So a join is one walk of the
     *  items and a series of ``append``s into memory the object already owns.
     */
    PATCHER_CLASS(gCombine, YSE::OBJ::G_COMBINE)
    _NO_MESSAGES
    _NO_CALCULATE

    _BANG_IN(SetBang)
    _INT_IN(SetInt)
    _FLOAT_IN(SetFloat)
    _LIST_IN(SetList)

    _PARM_CLEAR
    _PARM_PARSE

    /** @brief Most items — and so most inlets — the object will build. 32,
     *         which is also the width of the trigger mask. Arguments past that
     *         are reported and dropped. */
    static constexpr std::size_t MAX_ITEMS = 32;

    /** @brief Longest single item, in characters. Refused rather than
     *         truncated past that: half an item is a different item, and the
     *         point of the object is a token the boxes downstream still
     *         recognise. */
    static constexpr std::size_t ITEM_CAPACITY = 64;

    /** @brief How many items the object holds — one per inlet. At least one,
     *         even with no creation arguments at all. */
    int ItemCount() const {
      return (int)items.size();
    }

    /** @brief What inlet @p index is currently holding, or empty when it has
     *         never been set (or was cleared), in which case it renders as
     *         nothing. Out of range reads back as empty. */
    const std::string& Item(int index) const;

    /** @brief Which inlets emit when they receive, as a bit per inlet. Bit 0
     *         alone unless a leading ``triggers`` argument said otherwise. */
    unsigned int Triggers() const {
      return triggerMask;
    }

    /** @brief Whether inlet @p index emits when it receives. */
    bool IsHot(int index) const {
      return index >= 0 && index < (int)items.size() && ((triggerMask >> index) & 1u) != 0u;
    }

    /** @brief The last text this object sent, for tests and for a host reading
     *         the object's state. Empty until it has sent once. */
    const std::string& LastOutput() const {
      return outText;
    }

  private:
    // Rebuild the items, the trigger mask and the ports from the creation
    // arguments. Control thread only, and only before the object is wired or
    // published — see the note in the definition.
    void ShapePorts();

    // Store the `length` characters at `text` as item `index`. Out-of-range
    // indices and over-long tokens are ignored, silently: this runs on
    // whichever thread sent the message.
    void StoreToken(std::size_t index, const char* text, std::size_t length);

    // Spread the whitespace-separated tokens of `text` over the items from
    // `first` rightwards — Max's "each item in the list is treated as if it had
    // been received in a separate inlet, up to the number of inlets".
    void Distribute(const std::string& text, std::size_t first);

    // Write every item into `outText`, one after another with nothing between
    // them, and send the result. Sends nothing at all when the items are all
    // empty.
    void Emit(YSE::THREAD thread);

    // One cell per inlet: what that inlet last held, empty when unset. Sized
    // and reserved by ShapePorts(), so a store never allocates.
    std::vector<std::string> items;

    // Which inlets emit on receipt, one bit each. Read by every message
    // handler, written only by ShapePorts() before publication.
    unsigned int triggerMask = 1u;

    // The creation arguments. Control thread only: written by Parameters::Set,
    // read by ShapePorts(), never by a message handler.
    std::vector<std::string> creationArgs;

    // Where the joined symbol is built. Reserved by ShapePorts() for every item
    // at full length, so no join allocates.
    std::string outText;
  };

} // namespace PATCHER
} // namespace YSE
