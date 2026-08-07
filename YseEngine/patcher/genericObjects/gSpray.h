#pragma once
#include "../../headers/types.hpp"
#include "../pObject.h"
#include <cstddef>
#include <string>
#include <vector>

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief Distribute the values of a list to numbered outlets — ``.spray``
     *         (issue #479).
     *
     *  Max's ``spray``: "Accepts lists as input, where the first number is taken
     *  as the outlet number, and one or more values that follow are sent out that
     *  outlet and those to its right, in right-to-left order."
     *
     *  A demultiplexer whose destination is **data**. The patch sends
     *  ``2 60``, and 60 leaves by outlet 2 — the outlet is not a property of the
     *  patch cord but of the message, so a single stream can address a bank of
     *  destinations without a box per destination. That is the inverse of
     *  Max's ``funnel``, which tags an incoming value with the *inlet* it
     *  arrived on; send such an output straight into a ``.spray`` of the same
     *  width and you have a numbered bus that survives being carried through one
     *  cord. (``funnel`` itself is not ported yet.)
     *
     *  ### What it is not
     *
     *  ``.gate`` also routes by an index the patch supplies, but the index is a
     *  *separate* inlet and therefore a separate message: the patch has to set
     *  the destination, then send the value, and every inlet-ordering mistake in
     *  between sends a value to last time's outlet. Here the pair arrives
     *  together and cannot be desynchronised, which is the whole reason to reach
     *  for it — and it is also why ``.spray`` can address a different outlet on
     *  every message where a ``.gate`` fed from the same stream would need the
     *  index to arrive first, always.
     *
     *  ``.cycle`` (#477) also sends one message out one of N outlets, but chooses
     *  the outlet *itself* by rotating; the patch cannot say where a value
     *  should land. ``.route`` and ``.sel`` choose by *matching* the value
     *  against constants fixed at creation. This is the one that takes the
     *  destination as a number at run time.
     *
     *  ``.trigger`` fans one value out *every* outlet. Here one list may well
     *  reach several outlets — that is the "and those to its right" clause — but
     *  each of them receives a *different* element, so it spreads rather than
     *  copies. Against ``.unpack``, which also spreads a list across outlets,
     *  the difference is that ``.unpack`` always starts at outlet 0: this starts
     *  wherever the message says.
     *
     *  ### The list, and where its elements land
     *
     *  Max, for ``list``: "The first number in the list is a number that
     *  specifies the outlet number starting at 0 for the leftmost outlet; the
     *  second is an int or float value to send out that outlet. If there are
     *  additional elements in the list, they are sent out the subsequent outlets
     *  to the right of the one specified by the first number in the list."
     *
     *  So element *k* of the remainder goes out outlet ``index + k``. Elements
     *  whose outlet does not exist are dropped rather than wrapped — an index
     *  the object has no outlet for is a miscount in the patch, and quietly
     *  folding it back onto a real outlet would hide it, which is ``.gate``'s
     *  and ``.cycle``'s discipline for the same situation.
     *
     *  The outlets fire **right to left**, Max's universal order, which the
     *  description states outright for this object. So a downstream collector
     *  sees the last element of the burst before the first, and the elements are
     *  captured into a stack buffer before the first send: the send path is
     *  synchronous and re-entrant, so a patch looping an outlet back into the
     *  inlet arrives here again *inside* the ``Send``, and the interrupted burst
     *  has to go on emitting the values it started with rather than the loop's.
     *
     *  ### ``-1``, the broadcast
     *
     *  Max: "If the first number is -1, the remaining elements of the list will
     *  be repeated to all outlets." Tested on the number as it was *written*,
     *  before the offset is applied — otherwise an ``offset -1`` would turn the
     *  broadcast into an ordinary index and a patch would lose it silently.
     *
     *  With one value ("the" case: ``-1 60`` puts 60 on every outlet) the reading
     *  is unambiguous. With several, "repeated" is taken as repeated *around* the
     *  outlets: element ``i % count`` goes out outlet ``i``, so ``-1 0 1`` sets
     *  alternate outlets on a bank and a single value still reaches all of them.
     *
     *  ### ``offset``
     *
     *  The second creation argument "sets an offset for the numbering of the
     *  outlets. If the second argument is not present, the outlets are numbered
     *  beginning with 0", and the ``offset`` message does the same at run time:
     *  "The word offset followed by a number will offset the output of the object
     *  by the number of outlets given shifted to the left (a negative number will
     *  specify the number of outlets offset to the right)." Both say the same
     *  thing from opposite ends — the outlet reached is ``index - offset`` — so
     *  the two are one value here rather than two mechanisms.
     *
     *  It exists because the numbers a patch is spraying usually come from
     *  somewhere else and start where that somewhere else starts: MIDI channels
     *  at 1, a hardware controller's buttons at 64. The alternative is a ``.-``
     *  in front of every stream, which is a box and a chance to forget.
     *
     *  Run-time state, as ``.cycle``'s ``thresh`` and ``.bucket``'s ``R2L`` are:
     *  the ``offset`` message does not write back to the parameters, so a saved
     *  patch carries the creation argument and not whatever the last message set.
     *
     *  ### List mode
     *
     *  The third creation argument, "if set to '1', sets the object to 'list
     *  mode.' In 'list mode,' an entire list is output through the indicated
     *  outlet (with the optional offset provided by the second object argument),
     *  instead of unpacking the list and sending the individual elements out
     *  sequential outlets."
     *
     *  The same object with the spreading turned off: ``2 60 100`` sends the
     *  *list* ``60 100`` out outlet 2, which is what a patch wants when the
     *  elements belong together — a note and its velocity are one message, and
     *  splitting them across two outlets makes two facts out of one. Under
     *  ``-1`` the whole remainder goes out every outlet.
     *
     *  ### Numbers, and what is not one
     *
     *  Each element leaves as the kind it was *spelled* as, the discipline
     *  ``.trigger``, ``.bondo``, ``.cycle`` and ``.bucket`` share: an object that
     *  forwards must not rewrite the type of what passes through it, or ``0 1 2``
     *  arrives downstream as ``1. 2.``.
     *
     *  Max: "The list may contain only ints or floats; symbols will be ignored."
     *  Ignored **in place** here — a symbol costs its outlet a send and the
     *  elements after it keep their positions. Closing the gap instead would move
     *  every later element one outlet to the left, and this is a positional
     *  object: a stray symbol should cost one destination, not silently rewrite
     *  the destination of all the rest.
     *
     *  ``int``, ``float`` and ``bang`` are not accepted at all. Max: an ``int``
     *  "posts an error-message in the Max Console stating that spray requires a
     *  list", and it is right that a bare number is meaningless — it is either an
     *  outlet with nothing to put in it or a value with nowhere to go. The
     *  patcher has no per-message console and a message handler must not log, so
     *  the inlet simply does not register the three methods; ``GetAcceptedTypes``
     *  then reports the object's real contract, which is more use to a patch than
     *  a handler that swallows the message. A one-element list is the same case
     *  and is ignored for the same reason, and a message whose first token is a
     *  symbol is Max's ``anything``, which ``spray`` has no method for.
     *
     *  ### Real-time behaviour
     *
     *  ``Calculate()`` does nothing. The object is driven by its inlet, and an
     *  emitting ``Calculate()`` would spray from a stimulus no patch sent — the
     *  rule ``.sel``, ``.trigger``, ``.bondo``, ``.cycle`` and ``.bucket``
     *  establish.
     *
     *  Nothing on any path allocates, locks or blocks. The outlets are built
     *  once, on the control thread, by the parameter callbacks before the object
     *  is wired or published — at most ``MAX_PORTS`` (256), the ceiling the
     *  family already uses. A burst is captured into a fixed ``MAX_PORTS``-entry
     *  array on the **stack** rather than into a member, because a member would
     *  be overwritten by a re-entrant burst mid-send. List mode's text goes
     *  through a buffer reserved to ``TEXT_CAPACITY`` at construction, refilled
     *  immediately before each send from the caller's own string so that a
     *  re-entrant send cannot leave a later outlet carrying the wrong list.
     */
    PATCHER_CLASS(gSpray, YSE::OBJ::G_SPRAY)
    _NO_MESSAGES
    _NO_CALCULATE

    _LIST_IN(SetList)

    _PARM_CLEAR
    _PARM_PARSE

    /**
     *  @brief Most outlets the object will build.
     *
     *  256, the ceiling ``.sel``, ``.trigger``, ``.bondo``, ``.buddy``,
     *  ``.cycle`` and ``.bucket`` already use. Max says of ``spray`` that "there
     *  is no set limit"; a larger creation argument is clamped rather than
     *  honoured, and the clamp is said once on the control thread. Also the size
     *  of the stack array a burst is captured into, which is why it is a
     *  compile-time constant.
     */
    static constexpr int MAX_PORTS = 256;

    /** @brief Fewest outlets the object will build. */
    static constexpr int MIN_PORTS = 1;

    /**
     *  @brief Outlets with no creation argument.
     *
     *  **Two**, not one — Max: "If there is no argument present, the object has
     *  two outlets", and "The default number of outlets is 2." The odd one out
     *  in this family (``.cycle`` and ``.bucket`` default to one) because a
     *  one-outlet ``spray`` cannot distribute anything: the index would have only
     *  one legal value.
     */
    static constexpr int DEFAULT_PORTS = 2;

    /**
     *  @brief Characters of a list-mode message the object can forward without
     *         allocating.
     *
     *  256, the same bound ``.cycle``, ``.bondo`` and ``.regexp`` accept. Only
     *  list mode uses it; the unpacking path sends numbers and touches no text.
     */
    static constexpr std::size_t TEXT_CAPACITY = 256;

    /** @brief How many outlets the object has. At least one. */
    int OutletCount() const {
      return (int)outputs.size();
    }

    /**
     *  @brief The number subtracted from an incoming index to reach an outlet.
     *
     *  Max's creation argument and ``offset`` message, which are the same value
     *  described from opposite ends. Run-time state once the object exists, so a
     *  saved patch carries the argument rather than the last message.
     */
    int Offset() const {
      return offset;
    }

    /**
     *  @brief Max's list mode: the whole remainder out one outlet, rather than
     *         one element per outlet.
     */
    bool ListMode() const {
      return listMode;
    }

  private:
    // One element of a captured burst. The spelling travels with the value
    // because the object forwards rather than computes, and `filled` is what
    // separates "outlet 2 gets 0" from "outlet 2 gets nothing" — the case a
    // symbol in the list, or an index past the last outlet, produces.
    struct Slot {
      float value = 0.f;
      bool isFloat = false;
      bool filled = false;
    };

    // Send one captured element out `outlet`, as the kind it was spelled as.
    void SendSlot(const Slot& slot, int outlet, YSE::THREAD thread);

    // The ordinary path: element k of the text at `at` goes out outlet
    // `start + k`, captured first and then sent right to left. `start` is 64-bit
    // because it is an incoming index minus an offset, either of which may be a
    // saturated `int`, and their difference need not be one.
    void Scatter(const std::string& text, std::size_t at, I64 start, YSE::THREAD thread);

    // Max's `-1`: the remaining elements repeated around every outlet.
    void Broadcast(const std::string& text, std::size_t at, YSE::THREAD thread);

    // List mode: the whole of the text at `at` out `outlet` (or, when
    // `broadcast`, out every outlet), right to left.
    void SendWhole(const std::string& text, std::size_t at, I64 outlet, bool broadcast,
                   YSE::THREAD thread);

    // Rebuild the outlets from the current creation arguments, docs included.
    // Control thread only: called from the constructor and from the parameter
    // callbacks, all of which run before the object is wired or published.
    void ShapePorts();

    // The creation arguments, as tokens. Control thread only: written by
    // Parameters::Set, read by ParseParams(), never by a message handler.
    std::vector<std::string> creationArgs;

    // Max's outlet-numbering offset. Seeded from the second creation argument
    // and then owned by the `offset` message, so it does not survive a save.
    int offset = 0;

    // Max's third creation argument. No message changes it — Max documents
    // none — so it is fixed for the life of the object.
    bool listMode = false;

    // The one allocation a list-mode send would otherwise need.
    std::string listText;
  };
}
}
