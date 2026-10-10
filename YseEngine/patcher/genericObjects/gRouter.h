#pragma once
#include "../pListArgs.h"
#include "../pObject.h"
#include <cstddef>
#include <string>
#include <vector>

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief A message crossbar whose connections are set by messages rather
     *         than by patch cords — ``.router`` (issue #482).
     *
     *  Max's ``router``, whose one-line summary is "Route messages to multiple
     *  locations": "Any message received in any but the leftmost inlet will be
     *  routed to the outlet to which the inlet is currently connected."
     *
     *  A switching matrix, and the point of it is *when* the switching happens.
     *  Every other way of getting a message from source *i* to destination *j*
     *  fixes the answer at edit time: a patch cord is a structural fact, and in
     *  this patcher changing one means a GraphState swap (#226/#228) — a new
     *  graph built on the control thread and published to the audio thread
     *  between blocks. A ``.router`` connection change is a *message*. It costs
     *  one byte written into a table the object already owns, it takes effect on
     *  the next message through the crossbar, and the graph never changes shape
     *  at all. That is what makes "rewire this while it runs" a thing a patch can
     *  do to itself, from a ``.metro``, a ``.sel`` or a preset, rather than a
     *  thing only an editor can do to a patch.
     *
     *  And it is *many-to-many*. Several inlets may reach the same outlet, one
     *  inlet may reach several outlets, and both facts hold at once: the state is
     *  a full inlet×outlet matrix, not a selection. That is the difference from
     *  everything else in the routing family.
     *
     *  ### What it is not
     *
     *  ``.gate`` routes one inlet to one of N outlets, chosen by a number. One
     *  destination at a time, no memory of the others, and the choice arrives as
     *  data on the same event that is being routed. ``.switch`` is its mirror,
     *  N inlets onto one outlet. Neither can express "inlet 1 reaches outlets 0
     *  and 3, while inlet 2 also reaches outlet 3", which is one message here.
     *
     *  ``.spray`` also addresses numbered outlets, but the address rides *in the
     *  message*: ``2 60`` means "60 to outlet 2", and the object holds no routing
     *  state between messages. Here the message carries nothing but itself and
     *  the object holds the whole wiring, so a source needs to know nothing about
     *  where it is going — which is precisely what lets the destination be
     *  changed without touching the source.
     *
     *  ``.decode`` also sends on every outlet at once, but what it sends is its
     *  own 1s and 0s describing a selection. Nothing the patch sent passes
     *  through it. This forwards; the values are the patch's.
     *
     *  ``.funnel`` merges N inlets onto one outlet and stamps the origin. That is
     *  the degenerate one-column case of this matrix, plus a tag — and the tag is
     *  exactly what this object does *not* add, because a crossbar that rewrote
     *  what passed through it would not be transparent to swap in.
     *
     *  ``.s``/``.r`` also decouple a source from its destination, but by *name*
     *  and at creation time. A patch cannot move a ``.s`` to another name from
     *  inside itself; it can move a router connection with one message.
     *
     *  ### The leftmost inlet
     *
     *  Not routable. Max scopes every routed method to "any but the leftmost
     *  inlet", and the leftmost is where the connection messages go — so the
     *  object has ``inlets`` + 1 inlets, and the extra one is inlet 0. It accepts
     *  a ``list`` and nothing else: ``bang``, ``int`` and ``float`` are declined
     *  outright rather than swallowed by a handler with nothing to do, the
     *  ``.decode`` and ``.spray`` reading of the same situation, which also
     *  leaves ``GetAcceptedTypes()`` reporting the real contract.
     *
     *  Inlet numbers in the connection messages count the **routable** inlets from
     *  0, so ``connect 0 0`` connects the first inlet after the control inlet.
     *  The alternative — numbering from the physical inlet 0, which is not a
     *  routable inlet — would make ``connect 0 x`` name an inlet no message can
     *  ever pass through. The documentation labels follow the message numbering
     *  for the same reason: the control inlet is ``connections`` and the routable
     *  inlets are ``in0``, ``in1``, ...
     *
     *  Symmetrically the object has ``outlets`` + 1 outlets, the extra one being
     *  the rightmost, which carries nothing but the answer to ``dump``.
     *
     *  ### Setting a connection
     *
     *  Max gives four spellings, and all four are here.
     *
     *  ``<inlet> <outlet> <state>`` — "A list of three numbers received in the
     *  left inlet is interpreted as specifying an inlet number, an outlet number,
     *  and a 0 or 1 specifying the state of a connection." The form a patch
     *  computes: three numbers a ``.pack`` or a message box can assemble, so the
     *  routing can be driven by the same arithmetic that drives anything else.
     *
     *  ``connect <inlet> <outlet>`` and ``disconnect <inlet> <outlet>`` — the
     *  same two cells named in words, for a patch that writes its routing out
     *  literally. "Multiple inlets can be connected to multiple outlets, and vice
     *  versa", Max says of ``connect``, which is the many-to-many promise stated
     *  outright.
     *
     *  ``patch <inlet> <outlet>`` — "connects an inlet to an outlet and
     *  disconnects all other inlets that are currently connected to that outlet".
     *  A *column* operation, and the only one: it makes an outlet's source
     *  exclusive in one message, which is what a patch means by "now take the
     *  signal from over there instead" and which two messages could not do
     *  without a moment in between where the outlet has two sources or none.
     *
     *  ``clear`` — "All inlets are disconnected from all outlets."
     *
     *  A number that names an inlet or an outlet the object does not have is
     *  ignored, and so is a malformed message: ``.gate``'s, ``.spray``'s and
     *  ``.decode``'s discipline for an index out of range, since a crossbar that
     *  folded a bad index onto a real port would hide a miscount at the object
     *  best placed to expose it. ``state`` is read as on/off rather than as
     *  literally 1: any non-zero connects, the reading ``.decode`` takes of its
     *  own on/off inlets.
     *
     *  ### ``dump``, and the outlet it uses
     *
     *  "Sends the state of the object's switching matrix out the right outlet as
     *  a series of single line lists in the form ``inlet-number outlet-number
     *  state``." Every cell, in ascending inlet then ascending outlet order, so
     *  the whole matrix is recoverable by a patch and not merely by a person —
     *  a preset system can read the routing back out, which is the only way a
     *  routing that lives in messages rather than in the file can be saved at
     *  all.
     *
     *  The dump is guarded against re-entering itself. A dump line reaching the
     *  control inlet again — trivially arranged, since the dump outlet is an
     *  ordinary outlet — would otherwise start a second full dump inside the
     *  first, and each of *its* lines a third: the send-depth guard in
     *  ``outlet.cpp`` bounds the *depth* of that, but the breadth is
     *  ``inlets × outlets`` per level and multiplies. A dump requested while one
     *  is running is therefore ignored rather than nested.
     *
     *  ### ``print``
     *
     *  Max's ``print`` "prints the state of the switching matrix in the Max
     *  Console" — the application's log window, which this patcher does not have
     *  and, being headless, is not going to grow. It is accepted and does
     *  nothing rather than being treated as data, since a patch that sends it
     *  means a method call and not a symbol to route; ``dump`` is the headless
     *  form of the same question and the one a patch can actually read. The
     *  engine's own log is deliberately not used: no patcher object logs from a
     *  message handler, because a handler runs on whichever thread sent the
     *  message and that may be the audio thread.
     *
     *  ### Real-time behaviour
     *
     *  ``Calculate()`` does nothing. The object is driven by its inlets, and an
     *  emitting ``Calculate()`` would route a message no patch sent — the rule
     *  ``.sel``, ``.trigger``, ``.bondo``, ``.cycle``, ``.bucket``, ``.spray``
     *  and ``.decode`` establish.
     *
     *  Nothing on any path allocates, locks or blocks. The ports and the matrix
     *  are built once, on the control thread, by the parameter callbacks before
     *  the object is wired or published — at most ``MAX_PORTS`` (256) of each,
     *  the ceiling the family already uses, with a larger creation argument
     *  clamped. Routing a message reads one row of the matrix and sends; setting
     *  a connection writes one byte, and ``patch`` and ``clear`` write a bounded
     *  number of them. The one text buffer ``dump`` fills is reserved at
     *  construction and refilled per line, so a dump of any size touches no
     *  allocator.
     *
     *  A row is snapshotted onto the stack before the first send of a fan-out —
     *  the settle-first discipline ``.onebang``, ``.next``, ``.buddy``,
     *  ``.cycle``, ``.bucket``, ``.spray`` and ``.decode`` share. Sends are
     *  synchronous, so a patch that loops an outlet back into the control inlet
     *  re-enters here *inside* the send and can rewrite the very row being
     *  walked; without the snapshot one message would be delivered under two
     *  different routings.
     */
    PATCHER_CLASS(gRouter, YSE::OBJ::G_ROUTER)
    _NO_MESSAGES
    _NO_CALCULATE

    _BANG_IN(SetBang)
    _INT_IN(SetInt)
    _FLOAT_IN(SetFloat)
    _LIST_IN(SetList)

    _PARM_CLEAR
    _PARM_PARSE

    /**
     *  @brief Most routable inlets, and most routable outlets, the object will
     *         build.
     *
     *  256, the ceiling ``.sel``, ``.trigger``, ``.bondo``, ``.buddy``,
     *  ``.cycle``, ``.bucket``, ``.spray``, ``.funnel`` and ``.decode`` already
     *  use. Max documents no limit; a larger creation argument is clamped rather
     *  than honoured, which is what bounds the matrix and the stack row a
     *  fan-out is snapshotted into, and the clamp is said once on the control
     *  thread.
     */
    static constexpr int MAX_PORTS = 256;

    /** @brief Fewest routable inlets, and fewest routable outlets. */
    static constexpr int MIN_PORTS = 1;

    /**
     *  @brief Routable inlets, and routable outlets, with no creation argument.
     *
     *  **Two** of each. Max documents both arguments as optional and states no
     *  default, so this follows the family — ``.spray``, ``.funnel``, ``.bondo``
     *  and ``.buddy`` all default to two — and picks the smallest shape in which
     *  the object is itself: with one inlet and one outlet a crossbar has a
     *  single cell and nothing to route *between*.
     */
    static constexpr int DEFAULT_PORTS = 2;

    /**
     *  @brief Characters one ``dump`` line can need without reallocating.
     *
     *  Three ints and two separators, so ``3 * FORMAT_INT_WIDTH + 2`` covers the
     *  widest line the object can produce whatever its size.
     */
    static constexpr std::size_t TEXT_CAPACITY = (3 * FORMAT_INT_WIDTH) + 2;

    /**
     *  @brief How many **routable** inlets the object has — one fewer than
     *         ``NumInputs()``, which counts the control inlet too.
     */
    int InletCount() const {
      return inletCount;
    }

    /**
     *  @brief How many **routable** outlets the object has — one fewer than
     *         ``NumOutputs()``, which counts the dump outlet too.
     */
    int OutletCount() const {
      return outletCount;
    }

    /**
     *  @brief True when routable inlet @p inlet currently reaches routable
     *         outlet @p outlet. False for either index out of range.
     */
    bool Connected(int inlet, int outlet) const;

    /** @brief How many cells of the matrix are currently connected. */
    int ConnectionCount() const;

  private:
    // Copy routable inlet `inlet`'s row of the matrix into `row`, which must
    // have room for MAX_PORTS entries, and return how many outlets were
    // written. 0 for an index the object has no inlet for, which is also what a
    // handler reached with a bad inlet index does: nothing. Callers
    // zero-initialise the array — the fixed 256 bytes cost less than one of the
    // sends that follow, and it is the discipline `.spray` already applies to
    // its own captured burst, which keeps "was this entry written?" from being
    // an invariant held across a call boundary.
    //
    // Taken before the first send of a fan-out rather than read per outlet: the
    // send path is synchronous, so a patch looping an outlet back into the
    // control inlet re-enters inside the Send and would otherwise deliver the
    // first half of one message under the old routing and the rest under the
    // new one.
    int SnapshotRow(int inlet, unsigned char* row) const;

    // Set one cell. Out of range on either axis is ignored.
    void SetConnection(int inlet, int outlet, bool on);

    // Max's `patch`: make `inlet` the only routable inlet reaching `outlet`.
    void Patch(int inlet, int outlet);

    // Max's `clear`: every cell off.
    void Clear();

    // Max's `dump`: every cell out the rightmost outlet as `<in> <out> <state>`.
    void Dump(YSE::THREAD thread);

    // Append `value` as decimal to `dumpText`, through the patcher's one int
    // formatter rather than std::to_string — no allocation and no locale, since
    // this runs on whichever thread sent the `dump`.
    void AppendInt(int value);

    // Rebuild the inlets, outlets and matrix from the current creation
    // arguments, docs included. Control thread only: called from the
    // constructor and from the parameter callbacks, all of which run before the
    // object is wired or published.
    void ShapePorts();

    // The creation arguments, as tokens. Control thread only: written by
    // Parameters::Set, read by ParseParams(), never by a message handler.
    std::vector<std::string> creationArgs;

    // Routable inlets and outlets, excluding the control inlet and the dump
    // outlet. Both are also the dimensions of `matrix`.
    int inletCount = 0;
    int outletCount = 0;

    // The switching matrix, row-major: cell (in, out) is at
    // `in * outletCount + out`. Sized by the parameter callbacks before the
    // object is published and never resized by a message handler, so a fan-out
    // walking it cannot race a structural change — a connection message writes
    // bytes into it and nothing more.
    std::vector<unsigned char> matrix;

    // The one allocation a dump line would otherwise need.
    std::string dumpText;

    // True while Dump() is emitting. A dump line fed back into the control
    // inlet would otherwise start a full dump inside every line of the one
    // already running.
    bool dumping = false;
  };
}
}
