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
      "The list to cut up. Its items are taken in order, one complete group at a time: the first "
      "group of items goes out the leftmost outlet, the next out the one after it, and so on for "
      "as many group outlets as there are. A group outlet fires only when there are enough items "
      "left to fill it completely; everything from the first incomplete group onwards — a short "
      "tail, a surplus past the last group outlet, or both — goes out the rightmost outlet as one "
      "message, so nothing is ever dropped for want of an outlet. An int or a float is a one-item "
      "list and lands wherever a one-item list lands: on the leftmost outlet at the default group "
      "size of 1, and on the remainder at any larger size, there being no complete group to put it "
      "in. There is no bang, and no stored list for one to re-send: this object distributes the "
      "message it is given and holds nothing between messages, so a patch that wants to re-send "
      "puts a .l in front of it. A list longer than the bounded storage loses its tail, which is "
      "counted, and its head is distributed.";

  constexpr char kGroupOutletDoc[] =
      "One complete group of items from the incoming list, taken in order — this outlet's position "
      "among the group outlets decides which group. It fires only when the list was long enough to "
      "fill this group completely, so what leaves here is always exactly the group size and never "
      "a short piece; a list that runs out before reaching this outlet leaves it silent and its "
      "items go out the rightmost outlet instead. Outlets fire right to left, Max's universal "
      "order, so the remainder and every group to the right of this one have already been sent, "
      "each of them completing in full through the whole subgraph behind it before this one "
      "starts. A group of one item leaves as the int, float or symbol it spells rather than as a "
      "list of one, so it reaches the inlets an uncollected value would have reached; a longer one "
      "leaves as list text. Nothing is coerced — a group carries whatever the list carried.";

  constexpr char kRestOutletDoc[] =
      "The remainder: everything the group outlets did not claim, sent as one message. That is a "
      "surplus past the last group outlet, a tail too short to fill a complete group, or both at "
      "once — which is why this object drops nothing for want of an outlet, unlike .unpack. It is "
      "the first outlet to fire, Max's right-to-left order, so a patch reading it can rely on the "
      "groups arriving after it. A remainder of one item leaves as the int, float or symbol it "
      "spells rather than as a list of one, and a list that divided exactly into complete groups "
      "leaves nothing over, so this outlet sends nothing at all rather than an empty message.";

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
      "Cuts a list into equal groups and sends each group out its own outlet, with everything left "
      "over going out the rightmost one — Max's unjoin, which 'separates a list's elements by "
      "group, and sends each group of items out a separate outlet', and the inverse of .join. "
      "Where .unpack sends one item per outlet and drops whatever it has no outlet for, this sends "
      "a group of items per outlet and drops nothing at all, because its rightmost outlet is a "
      "remainder: everything the groups did not claim leaves there whole. So .unpack is the object "
      "for taking a list of known shape apart into its named components, and this is the object "
      "for cutting a list of unknown length into equal pieces. The round trip with .join is exact: "
      "a .join 2 holding '1 2' and '3 4' sends '1 2 3 4', and a .unjoin 2 2 gives back '1 2' and "
      "'3 4'. The first creation argument is Max's, 'the number of outlets beyond the rightmost "
      "outlet', so an object with n group outlets has n+1 outlets in all and a bare .unjoin has "
      "three; the default is 2. The second is Max's @outsize attribute, 'defines the number of "
      "items to be sent out the outlets', and defaults to 1 — which makes the default object "
      ".unpack with a remainder outlet instead of a drop. The patcher has no attributes, so an "
      "attribute that changes what an object does becomes a positional creation argument, which is "
      "the route .zl takes for its mode argument. Groups are complete or nothing: a group outlet "
      "fires only when enough items are left to fill it completely, and everything from the first "
      "incomplete group onwards — a short tail, a surplus past the last group outlet, or both — "
      "goes out the rightmost outlet as one message. That is Max's 'the rightmost outlet receives "
      "remaining items that don't fill a complete group' read literally, and it is what makes the "
      "object safe to wire into anything expecting a fixed-length list. Outlets fire right to "
      "left, Max's universal order, so the remainder lands first and the leftmost group last, and "
      "each send completes in full through the whole subgraph behind that outlet before the next "
      "one starts. There is no bang and no stored list for one to re-send: this object is a "
      "distributor rather than a register, and registering no bang is what keeps the reported "
      "contract honest; a patch that wants to re-send puts a .l in front. An int or a float is a "
      "one-item list and lands wherever a one-item list lands — on the leftmost outlet at the "
      "default group size, which is Max's 'the number is sent out the left outlet', and on the "
      "remainder at any larger size. A group of one item leaves as the int, float or symbol it "
      "spells rather than as a list of one, a longer one as list text, and an empty remainder "
      "sends nothing at all rather than an empty message; every outlet is declared as accepting "
      "anything, because a group carries whatever the list carried and this object coerces "
      "nothing. The arriving list is read into the bounded pre-allocated storage the whole list "
      "family shares: at most 256 items spanning at most 1024 characters between them, in memory "
      "reserved when the object is built. A list too long for it loses its tail, which is counted, "
      "and its head is distributed — the surplus here is input the patch has just sent rather than "
      "state the object was holding, which is why it is not refused whole the way .join's store "
      "is; the count is a counter rather than a log line because the refusing thread may be the "
      "audio callback, while a creation argument out of range is logged, parameter parsing being "
      "control-thread only. Re-typing the creation arguments rebuilds the object, since the first "
      "of them is the outlet count. Calculate() does nothing and no message path allocates, locks "
      "or blocks: numbers are rendered into a stack buffer, the list is collected into storage "
      "reserved at construction, and the sends render into a buffer reserved at the same time. Two "
      "threads sending to the same object are serialised by a single test-and-set guard whose "
      "loser is dropped and counted rather than made to spin, which is also what stops an object "
      "wired back into its own inlet from recursing on the audio thread.");

  ADD_CATEGORY(pCategory::GENERIC);

  INLET_DOC(0, "in", kInletDoc, "any");

  PARAM_DOC("groups", "2",
            "The number of group outlets, which is Max's 'the number of outlets beyond the "
            "rightmost outlet': the object has one more outlet than this, the last of them being "
            "the remainder. With no argument it is Max's default of 2, so a bare .unjoin has three "
            "outlets. At most 256 group outlets are built, a group needing at least one item and "
            "the shared bounded list holding at most that many; an argument outside 1-256 is "
            "clamped and the clamp is logged.",
            "1-256");

  PARAM_DOC("size", "1",
            "How many items fill one group — Max's @outsize attribute, 'defines the number of "
            "items to be sent out the outlets', carried here as a positional creation argument "
            "because the patcher has no attributes. With no argument it is Max's default of 1, "
            "which makes the object .unpack with a remainder outlet instead of a drop. A group "
            "outlet fires only when this many items are left to fill it, so what leaves a group "
            "outlet is always exactly this long. An argument outside 1-256 is clamped and the "
            "clamp is logged.",
            "1-256");
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
