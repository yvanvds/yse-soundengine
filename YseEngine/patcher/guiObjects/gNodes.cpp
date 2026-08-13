#include "gNodes.h"
#include "../pListArgs.h"
#include "../pSelector.h"
#include <cmath>

using namespace YSE::PATCHER;

#define className gNodes

namespace {

  // The bounds of the token starting at or after `from`, or false when there is
  // none. Walked in place rather than through substr: this runs on whichever
  // thread the message arrived on.
  bool NextToken(const char* text, std::size_t length, std::size_t from, std::size_t& begin,
                 std::size_t& end) {
    begin = from;
    while (begin < length && IsSelectorSeparator(text[begin]))
      begin++;
    end = begin;
    while (end < length && !IsSelectorSeparator(text[end]))
      end++;
    return end > begin;
  }

  // The next token that reads as a number, advancing `from` past it. Tokens that
  // are not numbers are *skipped* rather than ending the walk, which is
  // ExprParseFloatList's policy and the one .multislider's list handler follows.
  bool NextNumber(const char* text, std::size_t length, std::size_t& from, float& out) {
    std::size_t begin = 0;
    std::size_t end = 0;
    while (NextToken(text, length, from, begin, end)) {
      from = end;
      if (ReadNumericToken(text + begin, end - begin, out)) return true;
    }
    return false;
  }

  // Whether the `length` characters at `text` are exactly `word`. Compared in
  // place for the reason the tokens are walked in place: a list may arrive on
  // the audio thread.
  bool TokenIs(const char* text, std::size_t length, const char* word, std::size_t wordLength) {
    if (length != wordLength) return false;
    for (std::size_t i = 0; i < length; i++) {
      if (text[i] != word[i]) return false;
    }
    return true;
  }

  constexpr char kInDoc[] =
      "The field and the cursor both, and the only inlet — everything here emits, which is what a "
      "hot inlet in this patcher does. A list of *exactly two* numbers is the cursor position, and "
      "moving it is the object's whole purpose. A list of *three or more* is the node field "
      "itself: "
      "one x,y,radius triple per node, resizing the field to how many complete triples arrived and "
      "ignoring a trailing partial one, which is Max's list method with .multislider's listresize. "
      "Two-versus-three is arithmetic rather than a heuristic — a cursor is two numbers and a node "
      "is three, so the shortest possible field is three numbers and no cursor can be mistaken for "
      "one. That is also what lets the object satisfy issue #551's write half on a single inlet: "
      "the "
      "string GetGuiValue() produces is 3N numbers with N at least 1, so it always lands on the "
      "field branch. 'set <index> <x> <y>' moves one node and keeps its radius — the drag a host "
      "performs — and 'set <index> <x> <y> <radius>' sets the radius too; an index outside the "
      "live "
      "nodes is dropped rather than folded onto a real one. A bang re-sends against the cursor as "
      "it "
      "stands. There is deliberately no int or float method: one number is not a position, and an "
      "object that remembered a half-typed coordinate would make its output depend on message "
      "history in a way nothing downstream could see — a patch assembles the pair with a '.pack 0. "
      "0.', as every other two-number value in this patcher is assembled.";

  constexpr char kWeightDoc[] =
      "The normalised weights, one per node in node order, as one list. Node i's weight is its "
      "proximity divided by the sum of every node's proximity, where proximity is 1 at a node's "
      "centre, falls linearly to 0 at its rim and is 0 beyond it. So the weights **sum to 1 "
      "wherever "
      "the cursor is inside at least one node**, which is what makes this a crossfade that holds "
      "its "
      "loudness as the cursor moves, and they are **all 0 where it is inside none** — the answer "
      "that lets a patch tell 'between the nodes' from 'outside the field'. There is deliberately "
      "no "
      "fallback to the nearest node: a field with gaps in it is a legitimate design, and inventing "
      "coverage would make the object unable to express one. A node whose radius is 0 or less "
      "covers "
      "nothing and weighs nothing, which is how a node is muted without being deleted, and it is "
      "also what makes a nonsense coordinate produce a zero here rather than a NaN that would "
      "poison "
      "every *other* node's weight through the shared sum. Sent after the distances, which is the "
      "patcher's right-to-left outlet order.";

  constexpr char kDistanceDoc[] =
      "The raw Euclidean distances from the cursor to each node, one per node in node order, as "
      "one "
      "list — in whatever units the positions were given in, unnormalised and unclamped, so a "
      "patch "
      "can scale, threshold or sort on them itself. This is the geometry; outlet 0 is the "
      "interpretation of it. A node the cursor sits exactly on reads 0, and a node outside every "
      "radius still reports its distance here even though its weight on outlet 0 is 0 — which is "
      "the "
      "point of publishing both, since the weights alone cannot say how far away an uncovered node "
      "is. Sent *before* the weights: the patcher's right-to-left ordering, so a patch has the "
      "geometry in hand before the blend that was computed from it arrives.";

} // namespace

CONSTRUCT() {
  // Every node explicitly at the origin with unit reach. The vectors' own
  // construction value-initialises them, but an atomic that is merely
  // default-constructed holds no defined value under C++17, and "every node
  // always has a radius" is the premise of a field that can grow back into
  // nodes it once hid.
  for (std::size_t i = 0; i < MAX_NODES; i++) {
    nodeX[i].store(0.f, std::memory_order_relaxed);
    nodeY[i].store(0.f, std::memory_order_relaxed);
    nodeR[i].store(DEFAULT_RADIUS, std::memory_order_relaxed);
  }

  // One inlet, Max's, and hot. No int/float handler — see the class comment.
  ADD_IN_0;
  REG_LIST_IN(ListIn);
  REG_BANG_IN(BangIn);

  ADD_OUT_LIST; // 0: the weights
  ADD_OUT_LIST; // 1: the distances

  // In place before Register() hands the field to the parameter system: a
  // creation argument overwrites it, no argument leaves it alone.
  count = (int)DEFAULT_NODES;
  ADD_PARAM(count);

  listText.reserve(LIST_CAPACITY);

  ADD_DESCRIPTION(
      "Interpolates between positioned nodes: place circular points in a 2D field, move a cursor "
      "through it, and every point reports how near the cursor is to it - Max's nodes. Node i has "
      "a "
      "position and a radius; for a cursor at (cx, cy) the object sends the Euclidean distance to "
      "each node out outlet 1 and, out outlet 0, the normalised weight max(0, 1 - distance / "
      "radius) "
      "divided by the sum of every node's such proximity. That makes it a morph controller - one "
      "XY "
      "position crossfading four synth patches, one gesture blending a set of spatial "
      "configurations, one hand position mixing a bank of gains - which the patcher could not "
      "express before: .scale and .zmap map one number to one number, .multislider holds N numbers "
      "but computes nothing, and building it by hand would need a distance expression, a clamp and "
      "a "
      "divide per node plus a running sum that cannot be written without a feedback loop. The "
      "weights sum to 1 wherever the cursor is inside at least one node, which is what holds a "
      "crossfade's loudness as the cursor moves, and are all 0 where it is inside none, which is "
      "what lets a patch tell 'between the nodes' from 'outside the field'; there is deliberately "
      "no "
      "fallback to the nearest node, since a field with gaps is a legitimate design. A node with a "
      "radius of 0 or less covers nothing, which is how one is muted without being deleted and "
      "also "
      "what makes every comparison NaN-safe, a nonsense coordinate producing a zero weight rather "
      "than a NaN that would poison every other node's weight through the shared sum. Everything "
      "arrives on one hot inlet: a list of exactly two numbers is the cursor, a list of three or "
      "more is the whole field as x,y,radius triples (resizing it to the number of complete "
      "triples, .multislider's listresize), 'set <index> <x> <y> [<radius>]' moves one node while "
      "keeping its radius when none is given, and a bang re-sends. Two-versus-three is arithmetic "
      "rather than a heuristic - a cursor is two numbers and a node three - which is what lets a "
      "single inlet also satisfy the GUI value protocol's write half, the state string being 3N "
      "numbers with N at least 1. There is no int or float method, since one number is not a "
      "position and an object remembering a half-typed coordinate would make its output depend on "
      "invisible history; a patch assembles the pair with a '.pack 0. 0.'. The node count is the "
      "one creation argument and is a bound rather than a capacity: the three coordinate banks are "
      "allocated whole at 64 nodes on the control thread and never resized, so a walk samples the "
      "count once and can never index outside memory that exists, nothing structural depends on N, "
      "and a live SetParams rides the wait-free scalar plan of issue #234 with every node position "
      "surviving it. Growing reveals nodes and shrinking hides them rather than clearing them, "
      ".multislider's rule, clearing being an O(N) write on whichever thread the resize arrived "
      "on. "
      "Issue #562's 'bounded node count allocated at SetParams time' is met more strongly this "
      "way: "
      "allocating at SetParams would put a new on the path a live re-parse takes, while this "
      "removes the allocation from every path. The radius is deliberately not a creation argument "
      "- "
      "it belongs to a node rather than to the object, the field's whole expressive range being "
      "nodes of different reach, and a parameter writing it into every node would need either a "
      "parse callback (which turns every live count edit into an object replacement that discards "
      "the field) or a fallback rule that quietly reinterprets a node's own radius. A bare '.nodes "
      "4' is four unit-radius nodes at the origin, so a cursor at the origin reads back four equal "
      "weights of 0.25, the neutral blend. Only the count parameter persists; the positions and "
      "radii are live control state, and the form a host stores that in is GetGuiValue(), one "
      "string carrying the count as well as the field, which is what .preset is for. No path "
      "allocates, locks or blocks: the list handler walks its numbers in place, Calculate() "
      "samples "
      "the count and cursor once, computes distances and proximities into two 64-wide stack arrays "
      "and renders each list into a string reserved at construction - one buffer refilled "
      "immediately before each of the two sends rather than both up front, since the send path is "
      "synchronous and a patch wiring an outlet back round re-enters Calculate() inside the first "
      "send. The 64-node ceiling is lower than .multislider's 1024 because a node costs a square "
      "root and two renders per evaluation, and a field a person places is tens of points.");
  ADD_CATEGORY(pCategory::GUI);
  INLET_DOC(0, "field", kInDoc, "any float");
  OUTLET_DOC(0, "weights", kWeightDoc, "0.0-1.0, summing to 1 or all 0");
  OUTLET_DOC(1, "distances", kDistanceDoc, "0 and up");
  PARAM_DOC(
      "count", "4",
      "How many nodes the field is built with. Clamped to 1-64, the three coordinate banks "
      "being allocated whole at 64 nodes once at construction so nothing on a message path "
      "ever has to grow them. It is a bound rather than a capacity, which is why a live "
      "SetParams rides the wait-free scalar plan (issue #234) and keeps the object with every "
      "node position in it, and why a whole-field list of a different length may change the "
      "live count afterwards - .multislider's listresize. The parameter is therefore the "
      "count the object is *built* with; the live count, like the positions and radii, is "
      "run-time state and is not saved. The default is 4: the smallest field in which a 2D "
      "morph is a morph rather than a fade between two points. The ceiling is 64 rather than "
      ".multislider's 1024 because every node costs a square root and two renders per "
      "evaluation, and a field a person places and a host draws is tens of points.",
      "1-64");
}

// ─── the field ────────────────────────────────────────────────────────────────

unsigned int gNodes::Nodes() const {
  // Acquire, paired with the release store in the whole-field write: a reader
  // that sees a larger field must also see the nodes that made it larger.
  const int asked = count.load(std::memory_order_acquire);
  if (asked < (int)MIN_NODES) return (unsigned int)MIN_NODES;
  if ((std::size_t)asked > MAX_NODES) return (unsigned int)MAX_NODES;
  return (unsigned int)asked;
}

void gNodes::StoreNode(unsigned int index, float x, float y, float radius) {
  // Against the capacity, not the count: the whole-field write fills nodes
  // before it publishes the count that makes them live.
  if (index >= MAX_NODES) return;
  nodeX[index].store(x, std::memory_order_relaxed);
  nodeY[index].store(y, std::memory_order_relaxed);
  nodeR[index].store(radius, std::memory_order_relaxed);
}

float gNodes::RawDistance(unsigned int index, float cx, float cy) const {
  const float dx = cx - nodeX[index].load(std::memory_order_relaxed);
  const float dy = cy - nodeY[index].load(std::memory_order_relaxed);
  return std::sqrt((dx * dx) + (dy * dy));
}

float gNodes::Proximity(float distance, float radius) {
  // Both tests are written as the negation of what has to hold, so a NaN — which
  // compares false against everything — takes the zero branch rather than
  // propagating into the sum and taking every other node's weight with it.
  if (!(radius > 0.f)) return 0.f;
  if (!(distance < radius)) return 0.f;
  return 1.f - (distance / radius);
}

std::size_t gNodes::Render(float value, char* out) {
  const int written = ExprFormatValue(ExprValue::Float(value), out, kExprValueTextMax);
  return written > 0 ? (std::size_t)written : 0;
}

// ─── inlets ───────────────────────────────────────────────────────────────────

BANG_IN(BangIn) {
  // Nothing to store; the hot inlet calculates on the way out, which is the
  // re-send.
  (void)inlet;
  (void)thread;
}

LIST_IN(ListIn) {
  // Registered on inlet 0 only, but routed anyway: a later inlet must never
  // start writing the field because someone added a handler above.
  if (inlet != 0) return;
  (void)thread;

  const char* text = value.c_str();
  const std::size_t length = value.size();

  std::size_t begin = 0;
  std::size_t end = 0;
  // An empty list changes nothing and falls through to the re-send a bang gives.
  if (!NextToken(text, length, 0, begin, end)) return;

  if (TokenIs(text + begin, end - begin, "set", 3)) {
    // One node: Max's own `set` and issue #551's cell write are the same message
    // here, as they are on .multislider. The radius is optional because the
    // commonest live edit is a drag, which moves a node without re-deciding its
    // reach.
    std::size_t cursor = end;
    float index = 0.f;
    float x = 0.f;
    float y = 0.f;
    if (!NextNumber(text, length, cursor, index)) return;
    if (!NextNumber(text, length, cursor, x)) return;
    if (!NextNumber(text, length, cursor, y)) return;
    // Range-checked against the live count rather than indexed, as the protocol
    // requires: an index outside the field — or a NaN, which fails the first
    // compare — is dropped, never folded onto a real node. A fractional index
    // truncates towards zero, as every other index in the patcher does.
    if (!(index >= 0.f)) return;
    const unsigned int at = (unsigned int)ExprToInt(index);
    if (at >= Nodes()) return;
    float radius = 0.f;
    if (!NextNumber(text, length, cursor, radius)) {
      radius = nodeR[at].load(std::memory_order_relaxed);
    }
    StoreNode(at, x, y, radius);
    return;
  }

  std::size_t cursor = 0;
  float first = 0.f;
  float second = 0.f;
  // Nothing numeric at all, or a single number: neither is a position and
  // neither is a node, so the field is left as it is and the message falls
  // through to the re-send.
  if (!NextNumber(text, length, cursor, first)) return;
  if (!NextNumber(text, length, cursor, second)) return;

  float third = 0.f;
  if (!NextNumber(text, length, cursor, third)) {
    // Exactly two numbers: the cursor, and the object's hot path.
    cursorX.store(first, std::memory_order_relaxed);
    cursorY.store(second, std::memory_order_relaxed);
    return;
  }

  // Three or more: the whole field, one x,y,radius triple per node. Nodes are
  // filled first and the new count published afterwards with a release store, so
  // a reader that sees a larger field sees the nodes that made it larger.
  unsigned int written = 0;
  float x = first;
  float y = second;
  float radius = third;
  for (;;) {
    StoreNode(written, x, y, radius);
    written++;
    // A list longer than the field stops at the ceiling rather than wrapping.
    if (written >= (unsigned int)MAX_NODES) break;
    // A trailing partial triple is ignored: two thirds of a node is not a node,
    // and inventing the missing number would place a point the patch never gave.
    if (!NextNumber(text, length, cursor, x)) break;
    if (!NextNumber(text, length, cursor, y)) break;
    if (!NextNumber(text, length, cursor, radius)) break;
  }
  count.store((int)written, std::memory_order_release);
}

// ─── the GUI value protocol (issue #551) ──────────────────────────────────────

GUI_VALUE() {
  // A local rather than listText: that buffer belongs to the send path on the
  // audio thread, and this runs on the host thread. One call, one allocation —
  // which is what the protocol asks a bulk read to be.
  const unsigned int live = Nodes();
  std::string out;
  out.reserve((std::size_t)live * 3u * ((std::size_t)kExprValueTextMax + 1));

  char text[kExprValueTextMax];
  for (unsigned int i = 0; i < live; i++) {
    const float values[3] = {nodeX[i].load(std::memory_order_relaxed),
                             nodeY[i].load(std::memory_order_relaxed),
                             nodeR[i].load(std::memory_order_relaxed)};
    for (int part = 0; part < 3; part++) {
      if (i > 0 || part > 0) out.push_back(' ');
      const std::size_t written = Render(values[part], text);
      out.append(text, written);
    }
  }
  return out;
}

GUI_VALUE_COUNT() {
  // One cell per node, the cell being the node's whole x,y,radius triple. The
  // count is the node count and not three times it: a host repainting one node
  // wants one read, and a triple is the smallest thing that describes one.
  return Nodes();
}

GUI_VALUE_AT() {
  // Range-checked against the count read *now* rather than indexed, as the
  // protocol requires: past the end is "", never the whole state again.
  if (index >= Nodes()) return std::string();

  const float values[3] = {nodeX[index].load(std::memory_order_relaxed),
                           nodeY[index].load(std::memory_order_relaxed),
                           nodeR[index].load(std::memory_order_relaxed)};
  std::string out;
  char text[kExprValueTextMax];
  for (int part = 0; part < 3; part++) {
    if (part > 0) out.push_back(' ');
    const std::size_t written = Render(values[part], text);
    out.append(text, written);
  }
  return out;
}

// ─── diagnostics ──────────────────────────────────────────────────────────────

float gNodes::NodeX(unsigned int index) const {
  if (index >= Nodes()) return 0.f;
  return nodeX[index].load(std::memory_order_relaxed);
}

float gNodes::NodeY(unsigned int index) const {
  if (index >= Nodes()) return 0.f;
  return nodeY[index].load(std::memory_order_relaxed);
}

float gNodes::NodeRadius(unsigned int index) const {
  if (index >= Nodes()) return 0.f;
  return nodeR[index].load(std::memory_order_relaxed);
}

float gNodes::CursorX() const {
  return cursorX.load(std::memory_order_relaxed);
}

float gNodes::CursorY() const {
  return cursorY.load(std::memory_order_relaxed);
}

float gNodes::DistanceTo(unsigned int index) const {
  if (index >= Nodes()) return 0.f;
  return RawDistance(index, CursorX(), CursorY());
}

float gNodes::WeightOf(unsigned int index) const {
  const unsigned int live = Nodes();
  if (index >= live) return 0.f;

  const float cx = CursorX();
  const float cy = CursorY();
  float total = 0.f;
  float mine = 0.f;
  for (unsigned int i = 0; i < live; i++) {
    const float p = Proximity(RawDistance(i, cx, cy), nodeR[i].load(std::memory_order_relaxed));
    if (i == index) mine = p;
    total += p;
  }
  // Outside every node there is nothing to be a fraction of, and the honest
  // answer is 0 rather than an equal share of nothing.
  return total > 0.f ? mine / total : 0.f;
}

// ─── output ───────────────────────────────────────────────────────────────────

CALC() {
  // The count and the cursor are sampled once, so both lists describe the same
  // field seen from the same place even if a message arriving down one of the
  // cords moves something mid-send.
  const unsigned int live = Nodes();
  const float cx = cursorX.load(std::memory_order_relaxed);
  const float cy = cursorY.load(std::memory_order_relaxed);

  // Half a kilobyte of stack, which is the whole reason the ceiling is 64.
  float distance[MAX_NODES];
  float proximity[MAX_NODES];
  float total = 0.f;
  for (unsigned int i = 0; i < live; i++) {
    distance[i] = RawDistance(i, cx, cy);
    proximity[i] = Proximity(distance[i], nodeR[i].load(std::memory_order_relaxed));
    total += proximity[i];
  }

  char text[kExprValueTextMax];

  // Filled immediately before its send rather than both lists up front: the send
  // path is synchronous, so a patch looping an outlet back into the inlet
  // re-enters here inside SendList, and a buffer filled any earlier would be the
  // inner message's by the time this one was read (.funnel's lesson). Into memory
  // reserved at construction, so nothing here allocates.
  listText.clear();
  for (unsigned int i = 0; i < live; i++) {
    if (i > 0) listText.push_back(' ');
    const std::size_t written = Render(distance[i], text);
    listText.append(text, written);
  }
  // The distances before the weights: the patcher's right-to-left outlet order,
  // so a patch has the geometry before the blend computed from it.
  outputs[1].SendList(listText, thread);

  // Rebuilt from this invocation's own stack arrays, so the re-entrant send
  // above cannot have changed what this list describes — only the buffer it is
  // spelled into, which is why it is refilled rather than kept.
  listText.clear();
  for (unsigned int i = 0; i < live; i++) {
    if (i > 0) listText.push_back(' ');
    const float weight = total > 0.f ? proximity[i] / total : 0.f;
    const std::size_t written = Render(weight, text);
    listText.append(text, written);
  }
  outputs[0].SendList(listText, thread);
}

#undef className
