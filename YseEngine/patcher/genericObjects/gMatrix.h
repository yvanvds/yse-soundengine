#pragma once
#include "../math/gExprEval.h"
#include "../pListArgs.h"
#include "../pObject.h"
#include <cstddef>
#include <string>
#include <vector>

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief A message crossbar whose cells carry a **gain** — ``.matrix``
     *         (issue #484).
     *
     *  Max's ``matrix``, whose one-line summary is "Event routing matrix":
     *  "The matrix object controls the connections between inlets and outlets.
     *  You can route any inlet to any combination of outlets. Each connection
     *  has an associated gain factor and all values travelling through matrix
     *  can be scaled. It shares the same control protocol with the signal
     *  matrix~ object, but unlike matrix~, the outlets of the matrix object do
     *  not add the values of multiple inputs. matrix is best understood as a
     *  combination of gate and switch with many more features."
     *
     *  ### Why this exists next to ``.router``
     *
     *  Issue #484 asks the question outright — whether the patcher needs both
     *  or one object with a mode — and Max answers it by shipping both, with
     *  the same control protocol and one difference that runs all the way
     *  through: a ``.router`` cell is a **switch** and a ``.matrix`` cell is a
     *  **coefficient**.
     *
     *  That is not a spelling difference. A crossbar of switches can only ever
     *  answer "does this reach that", so a patch that wants to route *and*
     *  weight needs a multiplier per destination, wired behind the crossbar —
     *  which means the weights live in N×M separate objects, cannot be
     *  addressed as a matrix, and cannot be set by one message. Here the weight
     *  *is* the connection: ``connect 0 1 0.25`` and ``0 1 0.25`` say "inlet 0
     *  reaches outlet 1 at a quarter", and there is no state in which a cell is
     *  connected but unweighted. Which is exactly what the issue asks for —
     *  "the control-domain equivalent of a patchbay, the natural companion to
     *  YSE's channel send/return mixer for routing modulation rather than
     *  audio", where a send is a destination *and* an amount and always has
     *  been.
     *
     *  So the two are kept apart rather than merged behind a flag, because
     *  merging them would put a gain column on ``.router`` that its Max
     *  protocol has no message to reach, and a mode on ``.matrix`` that Max
     *  does not document. A patch that wants switching writes ``.router``; a
     *  patch that wants a weighted patchbay writes ``.matrix``; and at unity
     *  gain the second *is* the first, which is the useful thing to be able to
     *  say about them.
     *
     *  ### Connection and gain are one value
     *
     *  Max: "A non-zero gain adds a connection with the designated gain; a gain
     *  of 0 deletes the connection if it exists." So the matrix is a table of
     *  gains and nothing else: a cell is connected exactly when its gain is
     *  non-zero, and there is no "connected at zero" to disambiguate from
     *  "disconnected". That is worth stating because it removes a whole class
     *  of question — ``disconnect`` and a gain of 0 are the same operation, and
     *  ``ConnectionCount()`` is the count of non-zero cells rather than a
     *  separately maintained fact that could drift out of step with them.
     *
     *  ### What the gain does
     *
     *  It multiplies. **Unity is transparent**: a gain of exactly 1 forwards
     *  the message untouched, so an int stays an int, a bang stays a bang and a
     *  list arrives spelled character for character — which is what makes
     *  ``.matrix`` at its default gain interchangeable with ``.router``, and
     *  which a patch depends on wherever the value is a note number rather than
     *  a quantity.
     *
     *  Any **other** gain is arithmetic, and its result leaves as a float even
     *  when the input was an int. Truncating it back would discard exactly the
     *  scaling the patch asked for — an int of 1 at a gain of 0.5 would arrive
     *  as 0, and a modulation matrix whose fine settings all read zero is worse
     *  than no matrix at all.
     *
     *  A **bang** carries no value, so there is nothing for a gain to act on:
     *  it is forwarded as a bang at any gain the cell holds. (Max keeps the
     *  same rule behind its ``inhibit`` attribute, which is off by default and
     *  whose whole job is to stop passing bangs.)
     *
     *  A **list** is scaled element-wise when every one of its items is a
     *  number, and forwarded unchanged otherwise. That is Max's ``scalemode``
     *  rule read from its default position: scaling applies to numbers, and
     *  ``scalemode`` exists to *extend* it to "numeric arguments to messages
     *  that do not start with a number", which is off unless asked for. It is
     *  also the only reading that keeps ``note 60 100`` from having its channel
     *  quietly rescaled while its selector stays put.
     *
     *  ### The outlets do not sum
     *
     *  Max says so explicitly, against ``matrix~``, and in an event domain it
     *  falls out of what a message is: two inlets reaching one outlet produce
     *  two messages out of that outlet, one per input event, not one message
     *  carrying their total. There is no moment at which two events are
     *  simultaneous and so nothing to add them at. Worth pinning anyway,
     *  because it is the one place where reading across from ``matrix~`` would
     *  give the wrong answer.
     *
     *  ### The leftmost inlet, and the rightmost outlet
     *
     *  Max scopes every connection message to "In left inlet", and numbers the
     *  routed inlets "starting at 0 for the object's second inlet from left".
     *  So the object has ``inputs`` + 1 inlets, the extra one is inlet 0, and
     *  the numbering in the messages counts the **routable** inlets from 0 —
     *  ``connect 0 0`` names the inlet immediately to its right. ``.router``
     *  reached the same shape from the same wording, and for the same reason:
     *  numbering from the physical inlet 0 would make ``connect 0 x`` address
     *  an inlet no message can pass through.
     *
     *  Inlet 0 accepts a ``list`` and nothing else. ``bang``, ``int`` and
     *  ``float`` are declined outright rather than swallowed by a handler with
     *  nothing to do — ``.decode``'s, ``.spray``'s and ``.router``'s discipline,
     *  which also leaves ``GetAcceptedTypes()`` reporting the real contract.
     *
     *  ``dumpconnections`` gets its own rightmost outlet, which is where
     *  ``.matrix`` parts company with Max's letter. Max sends its answer as a
     *  dictionary out the **left** outlet, and can, because a dictionary is a
     *  distinguishable kind of message that a downstream ``route`` can filter
     *  out of the data stream. This patcher is headless and has no dictionaries:
     *  the answer has to be a list, routed data is also lists, and a dump line
     *  arriving on a data outlet would be indistinguishable from a value that
     *  had been routed there. So it leaves by a channel nothing else uses —
     *  the arrangement ``.router`` already established for the same question.
     *
     *  ### ``dumpconnections`` is replayable
     *
     *  Max: "outputs a Max dictionary message listing all current connections".
     *  **Current** connections, so unconnected cells are not reported — which
     *  differs from ``.router``, whose Max wording is "the state of the object's
     *  switching matrix" and which therefore dumps every cell including the
     *  zeros. The difference is not cosmetic at this size: a full 256×256 dump
     *  is 65536 lines where the connections are usually a handful.
     *
     *  Each line is ``<inlet> <outlet> <gain>``, which is deliberately the same
     *  three items in the same order as the bare list that *sets* a cell. A
     *  dump line fed back into a control inlet re-creates the connection it
     *  describes, so storing a routing is storing the dump and restoring one is
     *  ``clear`` followed by replaying it — the whole point of being able to
     *  read back state that lives in messages rather than in the saved file.
     *
     *  The dump is guarded against re-entering itself, as ``.router``'s is: its
     *  outlet is an ordinary outlet and a patch can wire it back to the control
     *  inlet in one cord, at which point every line of a running dump would
     *  start a dump of its own.
     *
     *  ### What is deliberately absent
     *
     *  ``dictionary <name>`` names a Max dictionary object, of which this
     *  patcher has none. It is accepted and does nothing rather than being read
     *  as data, since a patch that sends it means a method call;
     *  ``dumpconnections`` is the headless form of the same conversation and
     *  the one a patch can act on.
     *
     *  Max's attributes — ``defaultgain``, ``exclusive``, ``inhibit``,
     *  ``inrange``, ``outscale``, ``scalemode`` — are not modelled, because the
     *  patcher has no attribute mechanism (the reason ``.vexpr`` treats Max's
     *  ``maxsize`` default as the whole story). The one of them a patch would
     *  miss is ``defaultgain``, and it is reachable as the third creation
     *  argument, which is where Max puts it too.
     *
     *  ### Real-time behaviour
     *
     *  ``Calculate()`` does nothing. The object is driven by its inlets, and an
     *  emitting ``Calculate()`` would route a message no patch sent — the rule
     *  ``.sel``, ``.trigger``, ``.bondo``, ``.cycle``, ``.bucket``, ``.spray``,
     *  ``.decode`` and ``.router`` establish.
     *
     *  Nothing on any path allocates, locks or blocks. The ports and the gain
     *  table are built once, on the control thread, by the parameter callbacks
     *  before the object is wired or published — at most ``MAX_PORTS`` (256) of
     *  each, the ceiling the family already uses, with a larger creation
     *  argument clamped. Routing reads one row and sends; setting a cell writes
     *  one float. Both text buffers are reserved at construction and refilled
     *  per send, and a list is parsed into a bounded stack array once per
     *  message rather than once per destination.
     *
     *  A row is snapshotted onto the stack before the first send of a fan-out —
     *  the settle-first discipline ``.onebang``, ``.next``, ``.buddy``,
     *  ``.cycle``, ``.bucket``, ``.spray``, ``.decode`` and ``.router`` share.
     *  Sends are synchronous, so a patch that loops an outlet back into the
     *  control inlet re-enters here *inside* the send and can rewrite the very
     *  row being walked; without the snapshot one message would be delivered
     *  under two different sets of gains.
     */
    PATCHER_CLASS(gMatrix, YSE::OBJ::G_MATRIX)
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
     *  ``.cycle``, ``.bucket``, ``.spray``, ``.funnel``, ``.decode`` and
     *  ``.router`` already use. Max documents no limit; a larger creation
     *  argument is clamped rather than honoured, which is what bounds the gain
     *  table and the stack row a fan-out is snapshotted into, and the clamp is
     *  said once on the control thread.
     */
    static constexpr int MAX_PORTS = 256;

    /** @brief Fewest routable inlets, and fewest routable outlets. */
    static constexpr int MIN_PORTS = 1;

    /**
     *  @brief Routable inlets, and routable outlets, with no creation argument.
     *
     *  **Two** of each, which unlike most of this family Max states outright:
     *  "Sets the number of inputs; if not present the default is 2", and the
     *  same for the outputs.
     */
    static constexpr int DEFAULT_PORTS = 2;

    /**
     *  @brief Gain a bare ``connect`` uses with no third creation argument.
     *
     *  **Unity.** Max documents the third argument as setting "a default gain
     *  to be used for the connect message when a gain argument to connect is
     *  not supplied" and states no default for it. One is the only answer that
     *  makes a bare ``connect 0 1`` mean what the word says: 0 would make
     *  ``connect`` a disconnect, and anything else would silently rescale every
     *  patch that never mentioned gain.
     */
    static constexpr float DEFAULT_GAIN = 1.f;

    /**
     *  @brief Most items of a list the object will scale.
     *
     *  256 — ``.vexpr``'s ``kVexprMaxList``, which is Max's own ``maxsize``
     *  default, and the same reasoning: the buffer a scaled list is built into
     *  is sized once on the control thread rather than grown while a message is
     *  being forwarded. A **longer** list is forwarded unscaled rather than
     *  truncated, since a crossbar that shortened what passed through it would
     *  be a worse failure than one that did not weight it.
     */
    static constexpr int MAX_LIST_VALUES = 256;

    /**
     *  @brief Characters one ``dumpconnections`` line can need without
     *         reallocating.
     *
     *  Two ints, a formatted float and two separators.
     */
    static constexpr std::size_t DUMP_CAPACITY =
        (2 * FORMAT_INT_WIDTH) + (std::size_t)kExprValueTextMax + 2;

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
     *  @brief The gain of cell (@p inlet, @p outlet), or 0 for either index out
     *         of range — which is also what a disconnected cell holds.
     */
    float Gain(int inlet, int outlet) const;

    /** @brief True when cell (@p inlet, @p outlet) has a non-zero gain. */
    bool Connected(int inlet, int outlet) const {
      return Gain(inlet, outlet) != 0.f;
    }

    /** @brief How many cells of the matrix currently carry a non-zero gain. */
    int ConnectionCount() const;

    /** @brief The gain a bare ``connect`` uses — the third creation argument. */
    float DefaultGain() const {
      return defaultGain;
    }

  private:
    // Copy routable inlet `inlet`'s row of gains into `row`, which must have
    // room for MAX_PORTS entries, and return how many outlets were written. 0
    // for an index the object has no inlet for, which is also what a handler
    // reached with a bad inlet index does: nothing. Callers zero-initialise the
    // array, the discipline `.spray` and `.router` already apply to their own
    // captured bursts, which keeps "was this entry written?" from being an
    // invariant held across a call boundary.
    //
    // Taken before the first send of a fan-out rather than read per outlet: the
    // send path is synchronous, so a patch looping an outlet back into the
    // control inlet re-enters inside the Send and would otherwise deliver the
    // first half of one message under the old gains and the rest under the new.
    int SnapshotRow(int inlet, float* row) const;

    // Set one cell's gain. Out of range on either axis is ignored. A gain of 0
    // is Max's disconnect, so this is the whole of `connect`, `disconnect` and
    // the bare list.
    void SetGain(int inlet, int outlet, float gain);

    // Max's `clear`: every cell to 0.
    void Clear();

    // Max's `dumpconnections`: every *connected* cell out the rightmost outlet
    // as `<in> <out> <gain>`, the same shape that sets one.
    void DumpConnections(YSE::THREAD thread);

    // Rebuild the inlets, outlets and gain table from the current creation
    // arguments, docs included. Control thread only: called from the
    // constructor and from the parameter callbacks, all of which run before the
    // object is wired or published.
    void ShapePorts();

    // Send `text` out `outlet` with `gain` applied. `values`/`count` are the
    // list already parsed as numbers, and `numeric` says whether that parse
    // succeeded — done once per message rather than once per destination, since
    // a fan-out would otherwise re-read the same characters up to 256 times.
    void SendScaled(int outlet, const std::string& text, const float* values, int count,
                    bool numeric, float gain, YSE::THREAD thread);

    // The creation arguments, as tokens. Control thread only: written by
    // Parameters::Set, read by ParseParams(), never by a message handler.
    std::vector<std::string> creationArgs;

    // Routable inlets and outlets, excluding the control inlet and the dump
    // outlet. Both are also the dimensions of `gains`.
    int inletCount = 0;
    int outletCount = 0;

    // The gain a bare `connect` uses. Read on the message path, written only by
    // the parameter callbacks.
    float defaultGain = DEFAULT_GAIN;

    // The gain table, row-major: cell (in, out) is at `in * outletCount + out`,
    // and 0 means disconnected. Sized by the parameter callbacks before the
    // object is published and never resized by a message handler, so a fan-out
    // walking it cannot race a structural change.
    std::vector<float> gains;

    // The one allocation a dump line would otherwise need.
    std::string dumpText;

    // The one allocation a scaled list would otherwise need, reserved for the
    // longest MAX_LIST_VALUES values can print as.
    std::string listText;

    // True while DumpConnections() is emitting. A dump line fed back into the
    // control inlet would otherwise start a full dump inside every line of the
    // one already running.
    bool dumping = false;
  };
} // namespace PATCHER
} // namespace YSE
