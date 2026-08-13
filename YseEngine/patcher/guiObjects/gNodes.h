#pragma once
#include "../math/gExprEval.h"
#include "../pObject.h"
#include <atomic>
#include <cstddef>
#include <string>
#include <vector>

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief A field of circular nodes a cursor is weighed against —
     *         ``.nodes`` (issue #562).
     *
     *  Max's ``nodes``. Place points in a 2D field, move a cursor through it,
     *  and every point reports how close the cursor is to it. That is a **morph
     *  controller**: one XY position crossfading four synth patches, one gesture
     *  blending a set of spatial configurations, one hand position mixing a bank
     *  of gains. The patcher has had no way to express it — ``.scale`` and
     *  ``.zmap`` map one number to one number, ``.multislider`` holds N numbers
     *  but computes nothing, and a hand-built patch would need one distance
     *  expression, one clamp and one divide *per node* plus a running sum that
     *  cannot be written without a feedback loop.
     *
     *  ### The algorithm, which is the object
     *
     *  Node *i* has a position (x, y) and a radius r. For a cursor at (cx, cy):
     *
     *      d(i) = sqrt((cx - x)² + (cy - y)²)          the distance
     *      p(i) = max(0, 1 - d(i) / r)                 the proximity, 0 outside
     *      w(i) = p(i) / Σp                            the weight
     *
     *  ``d`` leaves on outlet 1 and ``w`` on outlet 0, both as one list in node
     *  order. The proximity in between is not published: it is the raw shape of
     *  a node's falloff — 1 at the centre, 0 at the rim and beyond — and what
     *  makes it useful is precisely the normalisation, which is what turns "how
     *  near is each of these" into "how much of each".
     *
     *  **The weights sum to 1 wherever the cursor is covered at all**, and are
     *  all 0 where it is covered by nothing. That is the whole contract, and
     *  both halves matter. A crossfade whose gains sum to 1 holds its loudness
     *  as the cursor moves; the all-zero answer is what lets a patch tell
     *  "between the nodes" from "outside the field", which a renormalised
     *  nearest-node answer could not. There is deliberately no fallback to the
     *  closest node: a field with holes in it is a legitimate design, and
     *  inventing coverage would make the object unable to express one.
     *
     *  A node with a radius of 0 or less covers nothing and contributes nothing.
     *  That is how a node is muted without deleting it, and it is also what
     *  makes every comparison here NaN-safe by construction: ``r > 0`` and
     *  ``d < r`` both read false for a NaN, so a nonsense coordinate produces a
     *  zero weight rather than a NaN that would poison Σp and with it every
     *  *other* node's weight.
     *
     *  ### The one inlet, and how two numbers differ from three
     *
     *  Everything arrives on inlet 0, which is hot — the patcher's rule, so
     *  every message it accepts is followed by a send:
     *
     *   - a list of **exactly two** numbers is the **cursor**;
     *   - a list of **three or more** is the **node field**: one (x, y, r)
     *     triple per node, resizing the field to how many complete triples there
     *     were, with a trailing partial one ignored;
     *   - ``set <index> <x> <y> [<radius>]`` moves **one** node, keeping its
     *     radius when none is given — the drag a host performs on one node;
     *   - a **bang** re-sends against the cursor as it stands.
     *
     *  Two-versus-three is not a heuristic, it is arithmetic: a cursor is two
     *  numbers and a node is three, so the shortest node field is three numbers
     *  and no cursor can ever be mistaken for one. That is what lets the object
     *  keep Max's single inlet while still satisfying the GUI value protocol's
     *  write half, which requires the string ``GetGuiValue()`` produced to be
     *  accepted back on **inlet 0** — here it is 3N numbers with N ≥ 1, so it
     *  lands on the node-field branch every time.
     *
     *  Deliberately **no int or float handler**. A single number is not a
     *  position, and the patcher's other option — treating it as one coordinate
     *  and waiting for its partner — would make the object's output depend on
     *  message history in a way nothing downstream could see. A patch feeds it
     *  from a ``.pack 0. 0.``, which is how every other two-number value in this
     *  patcher is assembled.
     *
     *  ### N, and why the field is not reallocated
     *
     *  ``count`` is a **bound, not a capacity**: the three coordinate banks are
     *  allocated whole at ``MAX_NODES`` on the control thread and never resized,
     *  exactly as ``.multislider``'s cells are, and for the same reason. A walk
     *  samples the count once and indexes below it; the count changing mid-walk
     *  can then only change what the *next* walk sees, never free or move a node
     *  under this one. Nothing structural depends on N either — one inlet and
     *  two outlets whatever the count — so a resize is not a graph edit, needs
     *  no ``GraphState`` swap, and (``count`` being the object's only parameter
     *  and a scalar) a live ``SetParams`` rides the wait-free scalar plan of
     *  issue #234 with every node position surviving it.
     *
     *  Growing reveals nodes rather than re-initialising them, and shrinking
     *  hides them rather than clearing them, which is ``.multislider``'s rule:
     *  clearing would be an O(N) write on whichever thread the resize arrived
     *  on, and a resize arrives on the audio thread by both routes.
     *
     *  Issue #562 asks for the node count to be "allocated at SetParams time".
     *  This is that requirement met more strongly: allocating at ``SetParams``
     *  would put a ``new`` on the path a live re-parse takes, and the bound-not-
     *  capacity form removes the allocation from every path instead of moving it
     *  to a colder one.
     *
     *  ### The radius is not a creation argument
     *
     *  A fresh ``.nodes 4`` is four unit-radius nodes at the origin, so a cursor
     *  at the origin reads back four equal weights of 0.25 — the neutral blend,
     *  which is the honest starting state for a morph controller and the one a
     *  host can draw before the user has placed anything.
     *
     *  There is no ``radius`` parameter, and that is a decision rather than an
     *  omission. A radius belongs to a node, not to the object: the field's
     *  whole expressive range is nodes of *different* reach, and a single
     *  parameter could only ever set them all alike. Making it a parameter would
     *  also cost the property above — writing it into every node needs either a
     *  parse callback (which makes ``NeedsRebuild()`` true and turns every live
     *  edit of the count into an object replacement that discards the field) or
     *  a fallback rule that quietly reinterprets a node's own radius. Radii
     *  arrive the way positions do, through the inlet and through the GUI value.
     *
     *  ### What persists
     *
     *  The ``count`` parameter, and nothing else — no ``DumpState`` override.
     *  ``.multislider``'s answer, and pObject.h's rule behind it: "a parameter
     *  is what the object was *created* with, not what it has since been told".
     *  The positions and radii are live control state, and the form a host
     *  stores live state in is ``GetGuiValue()`` — one string that carries the
     *  node count as well as the field, so pushing it back into a fresh object
     *  restores both. That is what ``.preset`` is for.
     *
     *  ### Real time
     *
     *  No path allocates, locks or blocks. The list handler walks its numbers in
     *  place rather than into a stack buffer. ``Calculate()`` samples the count
     *  and the cursor once, computes the distances and proximities into two
     *  ``MAX_NODES``-wide stack arrays (half a kilobyte, which is why the
     *  ceiling is 64 rather than ``.multislider``'s 1024 — every node here costs
     *  a square root and two renders, and a field a person places is tens of
     *  points), and renders each list into a string reserved at construction.
     *  The two sends share that one buffer, refilled immediately before each of
     *  them rather than both up front: the send path is synchronous, so a patch
     *  wiring an outlet back round re-enters ``Calculate()`` inside the first
     *  ``SendList`` and a buffer filled any earlier would be the inner message's
     *  by the time the outer one read it (``.funnel``'s lesson). The stack
     *  arrays are per-invocation, so the second list is still built from the
     *  same sample of the field as the first.
     *
     *  Coordinates are read and written ``relaxed`` — they are independent
     *  scalars and the GUI protocol explicitly permits a torn *frame*. Only the
     *  count carries ordering, and only in the one direction that matters: a
     *  whole-field write fills the nodes before it publishes the count that
     *  makes them live, so a reader seeing a larger field sees the nodes that
     *  made it larger.
     */
    PATCHER_CLASS(gNodes, YSE::OBJ::G_NODES)
    _NO_MESSAGES
    _DO_CALCULATE

    _BANG_IN(BangIn)
    _LIST_IN(ListIn)

    _HAS_GUI_CELLS

    /**
     *  @brief Most nodes the field can hold — 64.
     *
     *  The ceiling, not the default: the banks are allocated at this size once
     *  and ``count`` only decides how many of them are live. Lower than
     *  ``.multislider``'s 1024 on purpose — a node costs a square root and two
     *  renders per evaluation where a cell costs one render, ``Calculate()``
     *  keeps two ``MAX_NODES``-wide stack arrays, and a field a person places
     *  and a host draws is tens of points. The dense store for more numbers than
     *  that is ``.table``.
     */
    static constexpr std::size_t MAX_NODES = 64;

    /** @brief Nodes with no ``count`` creation argument. Four: the smallest
     *         field in which a 2D morph is a morph rather than a fade. */
    static constexpr std::size_t DEFAULT_NODES = 4;

    /** @brief Fewest nodes the field can be bounded to. A zero-node field could
     *         be neither weighed nor drawn. */
    static constexpr std::size_t MIN_NODES = 1;

    /** @brief The radius every node starts with, so a bare object is a neutral
     *         blend rather than a field that answers nothing. */
    static constexpr float DEFAULT_RADIUS = 1.f;

    /** @brief How many nodes are live right now. Clamped into
     *         [MIN_NODES, MAX_NODES] on the way out, so a hostile ``count``
     *         argument bounds rather than indexes. */
    unsigned int Nodes() const;

    /** @brief The x of node @p index, or 0 past the live count. Diagnostics and
     *         tests; a host reads the field through ``GetGuiValueAt``. */
    float NodeX(unsigned int index) const;

    /** @brief The y of node @p index, or 0 past the live count. */
    float NodeY(unsigned int index) const;

    /** @brief The radius of node @p index, or 0 past the live count. */
    float NodeRadius(unsigned int index) const;

    /** @brief Where the cursor is — the last two-number list, or the origin. */
    float CursorX() const;

    /** @brief The other half of ``CursorX``. */
    float CursorY() const;

    /** @brief The distance from the cursor to node @p index, or 0 past the live
     *         count — outlet 1's value for that node, without the send. */
    float DistanceTo(unsigned int index) const;

    /** @brief The normalised weight of node @p index, or 0 past the live count
     *         — outlet 0's value for that node, without the send. O(N), since
     *         a weight is only a weight relative to the whole field. */
    float WeightOf(unsigned int index) const;

  private:
    // Characters the longest of the two lists can need without reallocating:
    // every node rendered at full width plus one separator each. The weights and
    // the distances are the same shape, so one bound covers both.
    static constexpr std::size_t LIST_CAPACITY = MAX_NODES * ((std::size_t)kExprValueTextMax + 1);

    // Distance from the cursor to node `index`, and the falloff derived from it.
    // Both take a live index; the callers range-check against Nodes() or walk
    // below it.
    float RawDistance(unsigned int index, float cx, float cy) const;
    static float Proximity(float distance, float radius);

    // Store one node, bounded against the *capacity* rather than the count,
    // because the whole-field write fills nodes before it publishes the count
    // that makes them live.
    void StoreNode(unsigned int index, float x, float y, float radius);

    // Render `value` into `out` (at least kExprValueTextMax bytes) the way the
    // list outlets spell it, so the GUI value and the messages carry the same
    // text. Returns how many characters were written.
    static std::size_t Render(float value, char* out);

    // The field: three parallel banks rather than a vector of structs, because
    // a struct of atomics can be neither assigned nor resized and the banks have
    // to be constructed in place. Allocated whole on the control thread and
    // never resized — see the class comment's third section.
    std::vector<std::atomic<float>> nodeX = std::vector<std::atomic<float>>(MAX_NODES);
    std::vector<std::atomic<float>> nodeY = std::vector<std::atomic<float>>(MAX_NODES);
    std::vector<std::atomic<float>> nodeR = std::vector<std::atomic<float>>(MAX_NODES);

    // The cursor. Two independent scalars: a torn read gives a position between
    // the last two the patch sent, which is a frame of a movement rather than a
    // coordinate that never existed.
    std::atomic<float> cursorX{0.f};
    std::atomic<float> cursorY{0.f};

    // How many nodes are live. A registered parameter *and* live state: the
    // creation argument sets it through the scalar plan (issue #234) and a
    // whole-field list changes it afterwards, both being this one store. Held
    // raw as written and clamped in Nodes().
    std::atomic<int> count;

    // The outlet text, built into memory reserved at construction so neither
    // send allocates. One buffer for both lists — refilled immediately before
    // each send, which is what makes it re-entrancy-safe. See the class comment.
    std::string listText;
  };
}
}
