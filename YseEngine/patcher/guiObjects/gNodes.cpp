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
      "'x y' moves the cursor; three or more numbers set every node as 'x y radius' triples; "
      "'set <index> <x> <y> [<radius>]' moves one node; a bang sends again. Everything here "
      "sends.";

  constexpr char kWeightDoc[] =
      "One weight per node, as a list. The weights add up to 1 while the cursor is inside at "
      "least one node, and are all 0 outside every node. Sent last.";

  constexpr char kDistanceDoc[] =
      "The distance from the cursor to each node, as a list. Sent first.";

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

  ADD_DESCRIPTION("Blends between circular nodes in a 2D field. Move a cursor with 'x y' and it "
                  "sends each node's distance and a weight that falls from 1 at the node's centre "
                  "to 0 at its edge, scaled so the weights add up to 1.");
  ADD_CATEGORY(pCategory::GUI);
  INLET_DOC(0, "field", kInDoc, "any float");
  OUTLET_DOC(0, "weights", kWeightDoc, "0.0-1.0, summing to 1 or all 0");
  OUTLET_DOC(1, "distances", kDistanceDoc, "0 and up");
  PARAM_DOC("count", "4",
            "How many nodes the field starts with. A field list of another length changes it "
            "later.",
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
