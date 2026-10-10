#include "gUnjoin.h"
#include "../../implementations/logImplementation.h"
#include "../math/gExprEval.h"
#include "../pAtomList.h"
#include "../pListArgs.h"
#include "../pObjectList.hpp"
#include <cstddef>
#include <string>

using namespace YSE::PATCHER;

#define className gUnjoin

namespace {

  // An int as text, through the patcher's one int formatter rather than
  // std::to_string. Control-thread only (the construction-time logs).
  std::string IntText(int value) {
    char digits[YSE::PATCHER::FORMAT_INT_WIDTH];
    const std::size_t written = YSE::PATCHER::WriteInt(value, digits);
    return std::string(digits, written);
  }

  // True when the whole of `token` is a decimal integer. Stricter than
  // ReadIntArgAt on its own, which would read `4nd` as 4 — and the difference
  // matters here, because a creation argument that is not a number is a typo
  // worth reporting rather than a number worth guessing at.
  bool WholeInt(const std::string& token, int& out) {
    std::size_t cursor = 0;
    if (!ReadIntArgAt(token, cursor, out)) return false;
    return cursor == token.size();
  }

  // Reads argument `position` (1-based, empty tokens skipped) out of the
  // creation-argument list into `out`, clamped to [low, high], and says whether
  // it was there at all. Control thread only — it logs.
  bool ReadArg(const std::vector<std::string>& args, int position, const char* what, int low,
               int high, int& out) {
    int seen = 0;
    for (const std::string& token : args) {
      if (token.empty()) continue;
      seen++;
      if (seen != position) continue;

      int requested = 0;
      if (!WholeInt(token, requested)) {
        YSE::INTERNAL::LogImpl().emit(
            YSE::E_WARNING, std::string("patcher: ") + YSE::OBJ::G_UNJOIN + " " + what + " '" +
                                token + "' is not a whole number; using " + IntText(out));
        return false;
      }

      int clamped = requested;
      if (clamped < low) clamped = low;
      if (clamped > high) clamped = high;
      if (clamped != requested) {
        YSE::INTERNAL::LogImpl().emit(
            YSE::E_WARNING, std::string("patcher: ") + YSE::OBJ::G_UNJOIN + " asked for " +
                                IntText(requested) + " " + what + "; clamped to " +
                                IntText(clamped) + " (" + IntText(low) + "-" + IntText(high) + ")");
      }
      out = clamped;
      return true;
    }
    return false;
  }

  constexpr char kInletDoc[] =
      "The list to split into groups. An int or float is a list of one. Bang is not accepted.";

  constexpr char kGroupOutletDoc[] =
      "One complete group of items, the group in this position. Silent when the list ran out "
      "before filling it.";

  constexpr char kRestOutletDoc[] =
      "Everything the group outlets did not take, as one message. Sent first; silent when nothing "
      "is left over.";

} // namespace

CONSTRUCT() {
  // The one inlet, and the only one this object will ever have — the outlets
  // are what the creation arguments shape. Built here rather than in
  // ShapePorts() so that a re-parse rebuilding the outlets cannot drop the
  // handlers off the inlet a patch is already wired to.
  //
  // No bang: Max documents int, float, list and anything on `unjoin` and no
  // bang, and that is the honest contract rather than an omission — this object
  // holds nothing between messages, so there is nothing for a bang to re-send.
  ADD_IN_0;
  REG_INT_IN(UnjoinInt);
  REG_FLOAT_IN(UnjoinFloat);
  REG_LIST_IN(UnjoinList);

  // Every outlet is built by ShapePorts(), because the first argument *is* the
  // outlet count. The clear callback is what makes `SetParams("")` return the
  // object to Max's no-argument shape rather than leaving the previous outlet
  // count in place.
  REG_PARM_CLEAR;
  REG_PARM_PARSE;

  ADD_PARAM(creationArgs);

  // Max's default: two group outlets and the remainder, one item per group.
  // Also the shape ClearParams() restores.
  ShapePorts();

  // The one allocation the object makes outside its list, and it happens here
  // rather than on an arrival: a group is rendered into storage that is already
  // long enough, on whichever thread the list came in on — routinely the audio
  // callback.
  AtomList::ReserveRender(render);

  ADD_DESCRIPTION(
      "Splits a list into groups of equal size, one group per outlet, and sends whatever is left "
      "over out the rightmost outlet. Nothing is dropped. The reverse of .join.");

  ADD_CATEGORY(pCategory::LIST);

  INLET_DOC(0, "in", kInletDoc, "any");

  // One entry for the one LIST parameter, which carries both arguments (#954).
  PARAM_DOC("groups size", "2 1",
            "The number of group outlets; the object has one more, for the remainder. The second "
            "argument is the number of items in each group.",
            "groups 1-256, optional size 1-256");
}

// ─── the creation arguments ─────────────────────────────────────────────────

void gUnjoin::ShapePorts() {
  // Rebuilt rather than resized: the outlet *count* comes from the first
  // argument. Safe because every caller runs before the object is wired or
  // published — the constructor, and the two parameter callbacks, which
  // patcherImplementation::CreateObjectUnlocked runs before AssignGraphIds.
  groups = DEFAULT_GROUPS;
  size = DEFAULT_SIZE;
  list.Clear();

  ReadArg(creationArgs, 1, "group outlets", MIN_GROUPS, MAX_GROUPS, groups);
  ReadArg(creationArgs, 2, "items per group", 1, MAX_SIZE, size);

  outputs.clear();

  // One outlet per group, and one more for the remainder. ANY on all of them:
  // a group of one atom leaves as the value it spells, a longer one as list
  // text, and this object coerces nothing on the way through.
  for (int i = 0; i <= groups; i++)
    ADD_OUT_ANY;

  ApplyDocs();
}

void gUnjoin::ApplyDocs() {
  for (int i = 0; i < (int)outputs.size(); i++) {
    const bool rest = (i == groups);
    outputs[(std::size_t)i].SetDoc(rest ? std::string("rest") : OutletLabel(i),
                                   rest ? kRestOutletDoc : kGroupOutletDoc, "any");
  }
}

PARM_CLEAR() {
  // Runs on the control thread before the parameter string is re-read, and is
  // the whole of `SetParams("")`: Parameters::Set returns without calling the
  // parse callback for an empty argument, so this has to leave Max's
  // no-argument object behind rather than one still holding the previous
  // outlets.
  creationArgs.clear();
  ShapePorts();
}

PARM_PARSE() {
  ShapePorts();
}

// ─── the guard ──────────────────────────────────────────────────────────────

bool gUnjoin::Enter() {
  if (busy.exchange(true, std::memory_order_acq_rel)) {
    // Another thread is mid-message, or this object is wired back into its own
    // inlet and a send has come round again. Counted rather than spun on: this
    // is a path the audio callback takes.
    CountDrop();
    return false;
  }
  return true;
}

void gUnjoin::Leave() {
  busy.store(false, std::memory_order_release);
}

void gUnjoin::CountDrop(std::size_t count) {
  dropped.fetch_add((std::uint64_t)count, std::memory_order_relaxed);
}

// ─── distributing ───────────────────────────────────────────────────────────

void gUnjoin::Emit(YSE::THREAD thread) {
  const std::size_t held = list.Size();
  const std::size_t span = (std::size_t)size;

  // Complete groups only — Max's "the rightmost outlet receives remaining items
  // that don't fill a complete group", read literally. A group outlet is whole
  // or silent, which is what makes the object safe to wire into anything
  // expecting a fixed-length list.
  std::size_t filled = held / span;
  if (filled > (std::size_t)groups) filled = (std::size_t)groups;
  const std::size_t claimed = filled * span;

  // Right to left, and each Send returns only once the whole subgraph behind
  // that outlet has run — Max's universal order, the one `.trigger` states as a
  // guarantee. The remainder is the rightmost outlet, so it goes first;
  // everything the groups did not claim leaves there as one message, and
  // SendAtomRange sends nothing at all when there is nothing left over.
  SendAtomRange(outputs[(std::size_t)groups], list, claimed, held - claimed, render, thread);

  // Then the groups themselves, last to first. Walking forwards here would
  // break every patch that wires the right-hand groups into cold inlets, so it
  // is worth being loud: the loop counts down.
  for (std::size_t i = filled; i-- > 0;)
    SendAtomRange(outputs[i], list, i * span, span, render, thread);
}

void gUnjoin::Take(const char* text, std::size_t length, YSE::THREAD thread) {
  if (!Enter()) return;

  // Refilled rather than accumulated: this object holds nothing between
  // messages. An over-long list loses its tail and keeps its head — `.zl`'s
  // rule, and right here because the surplus is input the patch has just sent
  // rather than state the object was holding.
  list.Clear();
  const std::size_t refused = list.AddTokens(text, length);
  if (refused != 0) CountDrop(refused);

  Emit(thread);

  Leave();
}

// ─── the inlet ──────────────────────────────────────────────────────────────

INT_IN(UnjoinInt) {
  // Max: "int: Number sent to left outlet." A one-item list, which is exactly
  // what the list path makes of it, so it goes through the same distribution —
  // and so lands on the left outlet at the default group size of 1 and on the
  // remainder at any larger one, there being no complete group to put it in.
  // Formatted into a stack buffer, through the patcher's one spelling of a
  // number.
  if (inlet != 0) return;
  char text[kExprValueTextMax];
  const int written = ExprFormatValue(ExprValue::Int(value), text, kExprValueTextMax);
  if (written <= 0) return;
  Take(text, (std::size_t)written, thread);
}

FLOAT_IN(UnjoinFloat) {
  if (inlet != 0) return;
  char text[kExprValueTextMax];
  const int written = ExprFormatValue(ExprValue::Float(value), text, kExprValueTextMax);
  if (written <= 0) return;
  Take(text, (std::size_t)written, thread);
}

LIST_IN(UnjoinList) {
  // Max: "list: Elements grouped by outsize and distributed to corresponding
  // outlets; remaining items exit right outlet", and "anything: Functions
  // identically to list" — which is why there is no message word to strip here.
  if (inlet != 0) return;
  Take(value.c_str(), value.size(), thread);
}

#undef className
