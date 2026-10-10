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
      "The list to send item by item. An int or float is a list of one. A bang sends the last "
      "list again.";

  constexpr char kOutletDoc[] = "Each item of the list, first to last, as an int, float or symbol.";

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
      "Sends the items of a list out one at a time, first to last, so objects that take single "
      "numbers can process each one. A bang sends the last list again.");

  ADD_CATEGORY(pCategory::LIST);

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
