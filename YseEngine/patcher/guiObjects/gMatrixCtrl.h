#pragma once
#include "../math/gExprEval.h"
#include "../pListArgs.h"
#include "../pObject.h"
#include <atomic>
#include <cstddef>
#include <memory>
#include <string>
#include <vector>

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief A grid of cell states as one object — ``.matrixctrl`` (issue
     *         #559).
     *
     *  Max's ``matrixctrl``, the control whose cells are addressed as
     *  ``<column> <row> <value>`` and whose left outlet speaks the same three
     *  numbers. A step sequencer's grid, a drum pattern and a routing patchbay
     *  are all the same shape: an N×M table of small values, driven and drawn as
     *  one control but addressed one cell at a time.
     *
     *  ### Why it is worth having next to ``.matrix`` and ``.router``
     *
     *  Issue #559 names the pairing outright, and it is the reason this object
     *  earns its place rather than being a two-dimensional ``.multislider``:
     *  ``.matrix`` sets a cell with the bare list ``<inlet> <outlet> <gain>``
     *  and ``.router`` with ``<inlet> <outlet> <on>``, which is exactly the
     *  three numbers this object emits. Wire outlet 0 into either crossbar's
     *  control inlet and the control *is* the routing: one object holds the
     *  state and repaints, the other switches the messages, and no glue is
     *  needed between them because Max chose the same grammar for both ends
     *  thirty years ago. That correspondence is also why the flat index below
     *  is laid out the way it is.
     *
     *  ### A 2-D address on a 1-D protocol
     *
     *  Issue #551's GUI state is a *list* of cells, so the grid has to be
     *  flattened, and the only real question is which coordinate moves fastest.
     *  Cell (``column``, ``row``) is at
     *
     *      index = column * rows + row
     *
     *  — **column-major**, so ``row`` moves fastest. Two facts pick it:
     *
     *   - it is the lexicographic rank of the address *as Max writes it*. The
     *     grammar is ``<column> <row> <value>``, the last coordinate written is
     *     the one that varies quickest, and a reader converting between the two
     *     forms never has to remember that the object reverses them. Row-major
     *     would make the *first* coordinate written the fast one, which is the
     *     kind of quiet transposition that only shows up as a mirrored grid.
     *   - it is the layout ``.matrix`` already uses. Its gain table puts cell
     *     (in, out) at ``in * outletCount + out``, and in the pairing above the
     *     column is the inlet, so a ``.matrixctrl`` cell index and the offset of
     *     the ``.matrix`` cell it drives are the same number.
     *
     *  A host that draws the grid row by row therefore strides by ``rows``
     *  rather than reading the state left to right, which is one multiplication
     *  in the one place — the host — that already knows both dimensions.
     *
     *  ### Two grammars, one ``set``
     *
     *  ``.rslider`` dropped Max's ``set <min> <max>`` because the protocol had
     *  already claimed the keyword; ``.multislider`` found Max's ``set`` and the
     *  protocol's cell write to *be* the same message. This object is the third
     *  answer, and the collision is sharper: Max's ``set`` takes the same
     *  ``<column> <row> <value>`` its list method takes, so the keyword would
     *  carry a **two-dimensional** address while the protocol's carries a **flat
     *  index**, and only the count of numbers after it would say which. Arity
     *  does separate them — two numbers against three — but a mistyped
     *  ``set 2 3 1`` that loses its value would then silently write flat cell 2
     *  instead of being ignored, and one keyword meaning two addressing schemes
     *  is a thing a patch has to know rather than read.
     *
     *  So ``set`` is the protocol's, alone: ``set <index> <value>``. Nothing is
     *  lost, because Max's ``set`` differs from its list method only by being
     *  *silent* — "changes the state of matrixctrl without echoing the values to
     *  the output" — and the bare ``<column> <row> <value>`` list carries the
     *  addressing over unchanged. Silence is what is not portable: Max can
     *  afford a quiet write because its cells are *drawn*, so the state still
     *  arrives somewhere a person can see. Here the outlet is the only way a
     *  change reaches anything at all, and an object whose whole purpose is to
     *  drive a crossbar that learns of a change only by being told cannot have a
     *  form that tells nothing. Every write emits — the same conclusion
     *  ``.rslider`` and ``.multislider`` reached from their hot inlet, reached
     *  here from what the object is *for*.
     *
     *  ### The one ambiguity, and which side wins
     *
     *  A bare list is either the whole state (issue #551's round trip: exactly
     *  ``columns * rows`` numbers) or Max's cell triple (exactly 3). The two
     *  collide on precisely one shape — a grid of three cells, 1×3 or 3×1 — and
     *  there the **whole state wins**, because ``GuiValueIsSettable()`` is an
     *  unconditional promise that inlet 0 takes back the exact string
     *  ``GetGuiValue()`` produced, while the triple is a convenience with
     *  ``set <index> <value>`` standing behind it. On a three-cell grid the
     *  triple form is therefore unreachable; a grid that degenerate is a
     *  ``.multislider`` with extra ceremony anyway.
     *
     *  Any other length is neither form and is ignored, which is the family's
     *  reading of a message with nothing to address.
     *
     *  ### Dimensions are creation parameters, and that decides the storage
     *
     *  ``.multislider`` takes its length from any list it is given (Max's
     *  ``listresize``), so its count is a live *bound* into an array allocated
     *  at the ceiling on every instance — which is why its ceiling had to come
     *  down to 1024. **Nothing of that applies here.** One list length cannot
     *  recover two dimensions (twelve numbers are 3×4 or 4×3, and the object
     *  cannot guess), so there is no ``listresize`` analogue: the shape comes
     *  from the creation arguments and nowhere else, exactly as issue #559 asks
     *  — "dimensions from the creation arguments, storage allocated once at
     *  SetParams time".
     *
     *  That is what the storage is built on. The cells are allocated **exactly**
     *  ``columns * rows`` wide by the parameter callbacks, which run on the
     *  control thread before the object is wired or published (``.matrix``'s
     *  arrangement, and the reason registering them makes
     *  ``Parameters::NeedsRebuild()`` true, so a live ``SetParams`` on a
     *  published object replaces it through the graph swap rather than
     *  reallocating under the audio thread). A re-dimension is a genuine
     *  structural change — it changes what every index *means* — so replacing
     *  the object is the honest answer rather than a loss.
     *
     *  Which in turn is why the ceiling can stay where the family put it:
     *  ``MAX_AXIS`` = 256 columns and 256 rows, ``.matrix``'s and ``.router``'s
     *  ``MAX_PORTS``, so every crossbar the patcher can build has a control able
     *  to address all of it. An 8×8 grid costs 256 bytes, not the ceiling.
     *
     *  ``.multislider``'s worst-case-send reasoning was applied here and lands
     *  elsewhere: it reserved a send buffer for its *whole* state because it
     *  emits the bank as one list, and 4096 cells of that is ~132 KB per object.
     *  This object emits one ``<column> <row> <value>`` list **per cell**, so
     *  its send buffer is one triple wide whatever the grid is, and no send path
     *  scales with the cell count at all. The one place the ceiling is felt is
     *  ``GetGuiValue()``, which the protocol defines as one call and one
     *  allocation and which at 256×256 is a ~130 KB string — and that is exactly
     *  the case ``GetGuiValueCount()`` / ``GetGuiValueAt()`` exist for, so a
     *  host repainting one cell of a large grid never builds it.
     *
     *  ### The grammar
     *
     *  One inlet, taking a **bang** and a **list** and nothing else. Max
     *  documents no int or float method — a matrixctrl is driven by lists and by
     *  the mouse, and there is no mouse here — so those two are declined
     *  outright rather than swallowed by a handler with nothing to do, which is
     *  ``.matrix``'s, ``.router``'s and ``.decode``'s discipline and keeps
     *  ``GetAcceptedTypes()`` reporting the real contract.
     *
     *   - ``<column> <row> <value>`` sets one cell — Max's list method;
     *   - the whole state (``columns * rows`` numbers) sets every cell, which is
     *     issue #551's round trip;
     *   - ``set <index> <value>`` sets one cell by flat index, #551's cell
     *     write;
     *   - ``clear`` sets every cell to 0, Max's message;
     *   - ``getrow <row>`` and ``getcolumn <column>`` send that row's or that
     *     column's values out outlet 1 as one list, Max's messages and Max's
     *     right outlet;
     *   - a **bang** dumps the whole grid, which Max words as "dumps its current
     *     state in lists of three values for each cell pair".
     *
     *  **Outlets.** Outlet 0 carries ``<column> <row> <value>``: one list for a
     *  one-cell write, and every cell in index order for a whole-state write,
     *  a ``clear`` or a bang. That the echo of a restore is a full replay is the
     *  point rather than a cost — it is what carries the restored state on into
     *  the ``.matrix`` the control drives. Outlet 1 carries a row or a column as
     *  one list of values, in answer to a query and never otherwise; the index
     *  is not echoed back with it, since the patch that asked already knows it.
     *
     *  A dump is guarded against re-entering itself, as ``.matrix``'s is: outlet
     *  0's triples are exactly what inlet 0 accepts, so wiring the control back
     *  into itself is one cord away, and without the guard every line of a
     *  running dump would start a dump of its own.
     *
     *  **Bounds.** ``minimum`` and ``maximum`` are the range every cell is
     *  clamped into, defaulting to **0-1**: Max's default ``range`` of 2 is two
     *  states per cell, read here as off and on. Max's ``range`` is an integer
     *  count because its cells are *clicked* up through their states one at a
     *  time; headless there is no click, so the cells take the family's float
     *  bounds instead (``.dial``'s, ``.incdec``'s, ``.rslider``'s and
     *  ``.multislider``'s ``minimum`` / ``maximum``, ordered so a reversed pair
     *  still bounds against the right two numbers). That also makes the control
     *  able to hold a fractional ``.matrix`` gain, which an integer state count
     *  could not — the pairing #559 names, at full resolution. Clamping happens
     *  on the way out as well as in (``.incdec``'s rule), so a
     *  ``.matrixctrl 4 4 1 8`` that has never been touched reads back as
     *  sixteen 1s rather than the zeros it was constructed with.
     *
     *  ### Deliberately not here
     *
     *  Everything about drawing and the mouse: ``bkgndpicture``, ``cellpicture``,
     *  ``readanybkgnd``, ``readanycell``, ``disable`` / ``disablecell`` /
     *  ``enablecell``, and every colour, image, spacing and margin attribute.
     *  The YSE patcher is headless and issue #559 names any editor
     *  representation a non-goal.
     *
     *  ``dictionary <name>`` names a Max dictionary object, of which this
     *  patcher has none — ``.matrix``'s answer to the same message, for the same
     *  reason.
     *
     *  Max's attributes generally, including the exclusivity modes
     *  ``one/column``, ``one/row`` and ``one/matrix`` and the ``dialmode``
     *  family: the patcher has no attribute mechanism, so what it models it
     *  models as creation arguments, and an exclusivity rule that silently
     *  cleared cells a patch had just set would need one to be turned off again.
     *  A patch that wants one-per-column drives the control with the cell writes
     *  it already has.
     *
     *  ### What persists
     *
     *  The parameters, and nothing else — no ``DumpState`` override. pObject.h's
     *  rule: "a parameter is what the object was *created* with, not what it has
     *  since been told", so a reload brings back an empty grid of the saved
     *  shape. The form a host stores live cells in is ``GetGuiValue()``, which
     *  is what ``.preset`` is for and what inlet 0 takes back.
     *
     *  ### Real time
     *
     *  No path allocates, locks or blocks. The grid, the two send buffers and
     *  every port are built on the control thread before the object is
     *  published; a message handler only ever reads or writes cells. The list
     *  handler compares its keywords in place and walks its numbers token by
     *  token rather than into a stack buffer the size of the grid, because a
     *  whole-state list of 65536 numbers may well arrive down a cord on the
     *  audio thread. Every send fills a string reserved at construction
     *  immediately before the send rather than keeping it between sends
     *  (``.funnel``'s re-entrancy lesson). Cells are read and written
     *  ``relaxed``: they are independent scalars, and the protocol explicitly
     *  permits a host to see a torn *frame*.
     */
    PATCHER_CLASS(gMatrixCtrl, YSE::OBJ::G_MATRIXCTRL)
    _NO_MESSAGES
    _NO_CALCULATE

    _BANG_IN(BangIn)
    _LIST_IN(ListIn)

    _PARM_CLEAR
    _PARM_PARSE

    _HAS_GUI_CELLS

    /**
     *  @brief Most columns, and most rows, the grid can be built with.
     *
     *  256 on each axis — ``.matrix``'s and ``.router``'s ``MAX_PORTS``, so
     *  every crossbar the patcher can build has a ``.matrixctrl`` able to
     *  address all of it. Unlike ``.multislider``'s ceiling this one costs
     *  nothing until it is asked for: the dimensions are fixed at ``SetParams``
     *  time on the control thread, so the storage is exactly ``columns * rows``
     *  rather than the ceiling. See the class comment for where the ceiling
     *  *is* felt, which is the bulk GUI read and not any send.
     */
    static constexpr int MAX_AXIS = 256;

    /** @brief Fewest columns, and fewest rows. A grid with a zero axis has no
     *         cell to hold or draw. */
    static constexpr int MIN_AXIS = 1;

    /**
     *  @brief Columns, and rows, with no creation argument — **8** each.
     *
     *  Max documents no default for its ``rows`` / ``columns`` attributes. 8 is
     *  ``.multislider``'s reasoning on both axes: the smallest grid at which the
     *  object is doing something a single control could not, and the size a step
     *  sequencer is written at.
     */
    static constexpr int DEFAULT_AXIS = 8;

    /** @brief Lowest value a cell holds with no creation argument. */
    static constexpr float DEFAULT_MINIMUM = 0.f;

    /**
     *  @brief Highest value a cell holds with no creation argument — **1**.
     *
     *  Max's default ``range`` of 2 is two states per cell; read as bounds that
     *  is off and on, which is the crossbar-switch shape ``.router`` wants.
     */
    static constexpr float DEFAULT_MAXIMUM = 1.f;

    /** @brief How many columns the grid has. */
    int Columns() const {
      return columns;
    }

    /** @brief How many rows the grid has. */
    int Rows() const {
      return rows;
    }

    /** @brief How many cells the grid has — ``columns * rows``, and the GUI
     *         value's cell count. */
    std::size_t Cells() const {
      return cellCount;
    }

    /**
     *  @brief The flat GUI index of cell (@p column, @p row), or ``Cells()``
     *         when either coordinate is outside the grid.
     *
     *  ``column * rows + row``: column-major, so the row moves fastest and the
     *  index is the lexicographic rank of the address as Max writes it. The
     *  class comment carries the whole argument.
     */
    std::size_t IndexOf(int column, int row) const;

    /** @brief Cell (@p column, @p row), bounded, or 0 for a cell the grid does
     *         not have. */
    float Value(int column, int row) const;

  private:
    // Clamp `value` into the ordered [minimum, maximum] bounds.
    float Bound(float value) const;

    // Cell `index`, bounded — what every read hands out. The caller has already
    // checked it against Cells().
    float CellAt(std::size_t index) const;

    // Store into cell `index`, bounded. Ignores an index the grid does not have,
    // which is the family's reading of a message with nothing to address.
    void StoreCell(std::size_t index, float value);

    // Render `value` into `out` (at least kExprValueTextMax bytes) the way both
    // outlets and the GUI value spell it. Returns how many characters were
    // written.
    static std::size_t Render(float value, char* out);

    // Send `<column> <row> <value>` for one cell out outlet 0. Built into
    // `tripleText`, which is reserved at construction.
    void SendCell(int column, int row, YSE::THREAD thread);

    // Every cell out outlet 0, in index order — Max's bang, and the echo of a
    // whole-state write or a `clear`. Guarded against re-entering itself.
    void SendAll(YSE::THREAD thread);

    // One row's or one column's values out outlet 1 as a single list — Max's
    // `getrow` / `getcolumn`. `along` is the axis walked: rows when a column was
    // asked for, columns when a row was.
    void SendLine(bool wholeColumn, int index, YSE::THREAD thread);

    // Rebuild the grid and the send buffers from the current creation
    // arguments. Control thread only: called from the constructor and from the
    // parameter callbacks, all of which run before the object is wired or
    // published. The ports do not depend on the arguments and are built once, in
    // the constructor.
    void ShapeGrid();

    // The creation arguments, as tokens. Control thread only: written by
    // Parameters::Set, read by ParseParams(), never by a message handler.
    std::vector<std::string> creationArgs;

    // The grid. Allocated exactly `columns * rows` wide by ShapeGrid() on the
    // control thread and never touched structurally afterwards, so a message
    // handler and a host poll can both walk it without a lock. A
    // unique_ptr array rather than a vector because a vector of atomics can be
    // neither resized nor element-assigned; this one is replaced whole, and only
    // where nothing is yet reading it.
    std::unique_ptr<std::atomic<float>[]> cells;

    // The shape. Written only by ShapeGrid(), read by every path — plain ints
    // for the reason `.matrix`'s port counts are: the callbacks that write them
    // run before the object is published, and a live re-parse replaces the
    // object instead.
    int columns = 0;
    int rows = 0;
    std::size_t cellCount = 0;

    // The bounds every cell is clamped into, on the way in and on the way out.
    float minimum = DEFAULT_MINIMUM;
    float maximum = DEFAULT_MAXIMUM;

    // The one allocation a `<column> <row> <value>` send would otherwise need.
    std::string tripleText;

    // The one allocation a `getrow` / `getcolumn` answer would otherwise need,
    // reserved for the longer of the two axes.
    std::string lineText;

    // True while SendAll() is emitting. A triple fed back into inlet 0 would
    // otherwise start a second full dump inside every line of the one already
    // running — `.matrix`'s guard, for `.matrix`'s reason.
    bool dumping = false;
  };
} // namespace PATCHER
} // namespace YSE
