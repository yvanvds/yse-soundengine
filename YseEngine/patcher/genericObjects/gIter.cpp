#include "gIter.h"
#include "../math/gExprEval.h"
#include "../pAtomList.h"
#include "../pObjectList.hpp"
#include <cstddef>
#include <string>

using namespace YSE::PATCHER;

#define className gIter

namespace {

  constexpr char kInletDoc[] =
      "The message to break up. Its items are sent out the outlet one at a time, first to last — "
      "Max's 'the numbers in the list are sent out the outlet in sequential order'. Each send "
      "completes in full, the whole subgraph behind the outlet, before the next item leaves, so "
      "the items arrive as a sequence rather than all at once. An int or a float is a one-item "
      "list and so is simply passed through, and anything works exactly like a list, there being "
      "no message word to strip. A bang re-sends the message most recently received, which is why "
      "this inlet's last message is held; before anything has arrived a bang sends nothing at all "
      "rather than an empty message. A list longer than the shared bound of 256 items loses its "
      "tail, which is counted, and its head is iterated. One message here costs one send per item, "
      "up to 256 of them, on whichever thread sent it.";

  constexpr char kOutletDoc[] =
      "The items of the last message, one per send, in the order they appeared. An item leaves as "
      "the int, float or symbol it spells rather than as a list of one, which is the whole point "
      "of the object: it reaches the ordinary scalar inlets — a .i, a .+, a .mtof — that a list "
      "could not. Nothing is coerced, so an item carries whatever the list carried, and a message "
      "with no items sends nothing at all. The sends are synchronous and consecutive: the whole "
      "subgraph behind this outlet runs to completion for one item before the next item is sent, "
      "so a patch may accumulate downstream and rely on the order.";

} // namespace

CONSTRUCT() {
  // One inlet and one outlet, fixed: Max documents no creation arguments for
  // `iter`, so there is nothing here that could reshape the object.
  //
  // bang is registered, unlike on `.unjoin`, because this object *does* hold
  // something between messages — Max: "bang: sends the number or list most
  // recently received, in sequential order".
  ADD_IN_0;
  REG_BANG_IN(IterBang);
  REG_INT_IN(IterInt);
  REG_FLOAT_IN(IterFloat);
  REG_LIST_IN(IterList);

  // ANY, because an item carries whatever the list carried and this object
  // coerces nothing on the way through.
  ADD_OUT_ANY;

  // The one allocation the object makes outside its list, and it happens here
  // rather than on an arrival: an item is rendered into storage that is already
  // long enough, on whichever thread the list came in on — routinely the audio
  // callback.
  AtomList::ReserveRender(render);

  ADD_DESCRIPTION(
      "Sends the items of a list out one at a time — Max's iter, which 'unpacks and outputs list "
      "contents one element at a time'. It is the list family's serialiser: everything else here "
      "answers a list with a list, .zl by rearranging one and .unpack and .unjoin by fanning one "
      "out across a fixed set of outlets, while this turns a list of unknown length into a stream "
      "of individual messages down a single cord. That is what makes ordinary scalar objects — a "
      ".i, a .+, a .mtof, a .coll — usable per element, and it is why the object needs to know "
      "nothing about the list it is given where a .unpack 0 0 0 has to be told the shape up front. "
      "With .uzi it is the patcher's second iteration primitive, and the two iterate over "
      "different things: .uzi counts, driving a loop whose body reads an index, while this walks "
      "the data it was handed. Items leave first to last, and each send completes in full — the "
      "whole subgraph behind the outlet, depth first — before the next item is sent, because the "
      "patcher's send path calls the target inlet directly with no queue in between; that is what "
      "makes the object a serialiser rather than a scatter. A bang re-sends the message most "
      "recently received, which is why the object holds a list at all rather than passing straight "
      "through, and a bang before anything has arrived sends nothing rather than an empty message. "
      "An int or a float is a one-item list, as everywhere in this family, and anything is a list, "
      "so there is no message word to strip. Each item leaves as the int, float or symbol it "
      "spells rather than as a list of one, so it reaches the inlets an uncollected value would "
      "have reached; nothing is coerced. One arriving message becomes as many messages as the list "
      "is long, all inside the call frame of the one send that started it, so the object's cost is "
      "bounded by the same number that bounds its storage: at most 256 items, and so at most 256 "
      "sends, per stimulus. The number worth knowing beside it is that the patcher's value-command "
      "queue is 256 deep, so a full-length .iter feeding a .s from the control thread can saturate "
      "it in one burst and hit its documented drop-and-log backpressure — the same caution .uzi "
      "records, and a reason to keep bursts that cross the send/receive boundary short. The "
      "arriving list is read into the bounded pre-allocated storage the whole list family shares: "
      "at most 256 items spanning at most 1024 characters between them, in memory reserved when "
      "the object is built. A list too long for it loses its tail, which is counted, and its head "
      "is iterated — the surplus here is input the patch has just sent rather than state the "
      "object was holding, which is why it is not refused whole the way .join's store is, and the "
      "count is a counter rather than a log line because the refusing thread may be the audio "
      "callback. There are no creation arguments, Max documents none, and so nothing to re-parse: "
      "the object has a fixed shape and its only bound is the shared one. Calculate() does nothing "
      "and no message path allocates, locks or blocks: numbers are rendered into a stack buffer, "
      "the list is collected into storage reserved at construction, and the sends render into a "
      "buffer reserved at the same time. Two threads sending to the same object are serialised by "
      "a single test-and-set guard whose loser is dropped and counted rather than made to spin. "
      "That guard is held across the whole walk on purpose: this object emits in a loop, so a cord "
      "from its outlet back to its inlet re-enters the handler from inside the walk, and letting "
      "that through would both restart an iteration that never terminates and rewrite the list "
      "being walked. The returning message is refused and counted, exactly as .uzi refuses a "
      "re-entrant start.");

  ADD_CATEGORY(pCategory::GENERIC);

  INLET_DOC(0, "in", kInletDoc, "any");
  OUTLET_DOC(0, "out", kOutletDoc, "any");
}

// ─── diagnostics ────────────────────────────────────────────────────────────

std::string gIter::Stored() const {
  // Allocates, and says so in the header: this is for tests and hosts, not for
  // a message path. The object's own sends use the buffer reserved above.
  std::string out;
  AtomList::ReserveRender(out);
  list.Render(out);
  return out;
}

// ─── the guard ──────────────────────────────────────────────────────────────

bool gIter::Enter() {
  if (busy.exchange(true, std::memory_order_acq_rel)) {
    // Another thread is mid-message, or this object is wired back into its own
    // inlet and a send has come round again from inside the walk. Counted
    // rather than spun on: this is a path the audio callback takes, and the
    // refusal is also what keeps `list` still while it is being walked.
    CountDrop();
    return false;
  }
  return true;
}

void gIter::Leave() {
  busy.store(false, std::memory_order_release);
}

void gIter::CountDrop(std::size_t count) {
  dropped.fetch_add((std::uint64_t)count, std::memory_order_relaxed);
}

// ─── the walk ───────────────────────────────────────────────────────────────

void gIter::Emit(YSE::THREAD thread) {
  const std::size_t held = list.Size();

  // First to last, one item per send — Max's "in sequential order". Through the
  // family's shared slice sender with a span of one, so an item leaves as the
  // int, float or symbol it spells rather than as a list of one, and so the
  // bounds are clamped for us.
  //
  // `held` is read once and cannot move underneath the loop: every handler that
  // could rewrite `list` goes through Enter(), and the guard is held here.
  for (std::size_t i = 0; i < held; i++)
    SendAtomRange(outputs[0], list, i, 1, render, thread);
}

void gIter::Take(const char* text, std::size_t length, YSE::THREAD thread) {
  if (!Enter()) return;

  // Replaced rather than accumulated: the held list is "the message most
  // recently received" and nothing more. An over-long list loses its tail and
  // keeps its head — `.zl`'s rule, and right here because the surplus is input
  // the patch has just sent rather than state the object was holding.
  list.Clear();
  const std::size_t refused = list.AddTokens(text, length);
  if (refused != 0) CountDrop(refused);

  Emit(thread);

  Leave();
}

// ─── the inlet ──────────────────────────────────────────────────────────────

BANG_IN(IterBang) {
  // Max: "bang: Sends the number or list most recently received, in sequential
  // order." Nothing received yet means nothing held, and so nothing sent —
  // rather than an empty message.
  if (inlet != 0) return;
  if (!Enter()) return;
  Emit(thread);
  Leave();
}

INT_IN(IterInt) {
  // Max: "int: Outputs the number." A one-item list, which is what the list
  // path makes of it, so it is stored (a later bang re-sends it) and passed
  // straight back out. Formatted into a stack buffer, through the patcher's one
  // spelling of a number.
  if (inlet != 0) return;
  char text[kExprValueTextMax];
  const int written = ExprFormatValue(ExprValue::Int(value), text, kExprValueTextMax);
  if (written <= 0) return;
  Take(text, (std::size_t)written, thread);
}

FLOAT_IN(IterFloat) {
  if (inlet != 0) return;
  char text[kExprValueTextMax];
  const int written = ExprFormatValue(ExprValue::Float(value), text, kExprValueTextMax);
  if (written <= 0) return;
  Take(text, (std::size_t)written, thread);
}

LIST_IN(IterList) {
  // Max: "list: The numbers in the list are sent out the outlet in sequential
  // order", and "anything: Functions identically to list" — which is why there
  // is no message word to strip here.
  if (inlet != 0) return;
  Take(value.c_str(), value.size(), thread);
}

#undef className
