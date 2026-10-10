#include "gListFunnel.h"
#include "../math/gExprEval.h"
#include "../pAtomList.h"
#include "../pListArgs.h"
#include "../pObjectList.hpp"
#include <cstddef>
#include <string>

using namespace YSE::PATCHER;

#define className gListFunnel

namespace {

  // An element's position plus the offset, saturated to the `int` range. Both
  // sides are ints, so the sum need not be one; the addition is done in 64 bits
  // and the result pinned rather than allowed to wrap, since a wrapped index
  // would read as a perfectly plausible one to whatever consumes it. `.funnel`
  // saturates its tag for the same reason.
  int SaturatingIndex(std::size_t item, int offset) {
    const I64 sum = (I64)item + (I64)offset;
    if (sum > 0x7FFFFFFFLL) return 0x7FFFFFFF;
    if (sum < -0x80000000LL) return (int)(-0x80000000LL);
    return (int)sum;
  }

  constexpr char kInletDoc[] =
      "The list to number. An int or float is a list of one. 'offset <n>' sets the number given "
      "to the first item, without sending anything. Bang is not accepted.";

  constexpr char kOutletDoc[] =
      "One two-item list per item, first to last: the item's index, then the item unchanged.";

} // namespace

CONSTRUCT() {
  // The creation argument is only a number, not a shape, so the callbacks do
  // not rebuild any port — but they are registered all the same, because
  // registering them is what makes ParamsNeedRebuild() true and so sends a live
  // SetParams (#234) down the structural-rebuild route. That matters here: the
  // `offset` message writes the same field from a message thread, and the
  // scalar-patch route would have the audio thread write it at the top of a
  // block with no guard between the two.
  REG_PARM_CLEAR;
  REG_PARM_PARSE;

  ADD_PARAM(creationArgs);

  // One inlet, one outlet, fixed. `bang` is deliberately not registered: Max
  // documents no bang method for `listfunnel`, and unlike `.iter` this object
  // holds no message to re-send. The inlet declines the type outright rather
  // than accepting it and doing nothing, so GetAcceptedTypes() reports the real
  // contract — `.spray`'s reasoning.
  ADD_IN_0;
  REG_INT_IN(FunnelInt);
  REG_FLOAT_IN(FunnelFloat);
  REG_LIST_IN(FunnelList);

  // LIST rather than ANY: there is no arity at which this object sends a bare
  // atom, since the index is always in front of the element.
  ADD_OUT_LIST;

  // Seeds `offset` from the creation argument. Also what ClearParams() restores.
  ReadArgs();

  // The one allocation the object makes outside its list, and it happens here
  // rather than on an arrival: a pair is assembled in storage that is already
  // long enough, on whichever thread the message came in on — routinely the
  // audio callback.
  AtomList::ReserveRender(render);

  ADD_DESCRIPTION(
      "Sends each item of a list out as a two-item list: its index, then the item. Use it to "
      "address items by position with .route, .sel or .spray.");

  ADD_CATEGORY(pCategory::LIST);

  INLET_DOC(0, "in", kInletDoc, "any");
  OUTLET_DOC(0, "out", kOutletDoc, "list");

  PARAM_DOC("offset", "0",
            "The index given to the first item. The 'offset' message changes it while the patch "
            "runs; that change is not saved.",
            "any whole number");
}

// ─── parameters ─────────────────────────────────────────────────────────────

void gListFunnel::ReadArgs() {
  // Control thread only, and before the object is wired or published: the
  // constructor and the two parameter callbacks, which
  // patcherImplementation::CreateObjectUnlocked runs before AssignGraphIds. A
  // *live* SetParams never reaches here on a published object — registering the
  // callbacks makes ParamsNeedRebuild() true, so #234 replaces the object.
  offset = 0;

  for (const std::string& token : creationArgs) {
    // Parameters::Set splits on single spaces, so a run of them yields empty
    // tokens; an empty argument is not a number.
    if (token.empty()) continue;

    float number = 0.f;
    // Strict on purpose, as the rest of the family is: ExprParseFloatList would
    // read `5abc` as 5 and fold `1e999` to 0, and neither answers "is this
    // creation argument a number at all".
    if (!ReadNumericToken(token, number)) continue;

    // Max documents one argument only, so the first number wins and the rest of
    // the string is kept verbatim by Parameters for the round trip.
    offset = ExprToInt(number);
    break;
  }
}

PARM_CLEAR() {
  // Runs on the control thread before the parameter string is re-read, and is
  // the whole of `SetParams("")`: Parameters::Set returns without calling the
  // parse callback for an empty argument, so this has to leave Max's
  // no-argument object behind rather than one still holding the previous offset.
  creationArgs.clear();
  ReadArgs();
}

PARM_PARSE() {
  ReadArgs();
}

// ─── diagnostics ────────────────────────────────────────────────────────────

int gListFunnel::IndexFor(std::size_t item) const {
  return SaturatingIndex(item, offset);
}

std::string gListFunnel::Stored() const {
  // Allocates, and says so in the header: this is for tests and hosts, not for
  // a message path. The object's own sends use the buffer reserved above.
  std::string out;
  AtomList::ReserveRender(out);
  list.Render(out);
  return out;
}

// ─── the guard ──────────────────────────────────────────────────────────────

bool gListFunnel::Enter() {
  if (busy.exchange(true, std::memory_order_acq_rel)) {
    // Another thread is mid-message, or this object is wired back into its own
    // inlet and a send has come round again from inside the walk. Counted
    // rather than spun on: this is a path the audio callback takes, and the
    // refusal is also what keeps `list` and `offset` still while the walk runs.
    CountDrop();
    return false;
  }
  return true;
}

void gListFunnel::Leave() {
  busy.store(false, std::memory_order_release);
}

void gListFunnel::CountDrop(std::size_t count) {
  dropped.fetch_add((std::uint64_t)count, std::memory_order_relaxed);
}

// ─── the walk ───────────────────────────────────────────────────────────────

void gListFunnel::Emit(YSE::THREAD thread) {
  const std::size_t held = list.Size();

  // `held` and `offset` are read once and cannot move underneath the loop:
  // every handler that could rewrite either goes through Enter(), and the guard
  // is held here.
  for (std::size_t i = 0; i < held; i++) {
    char digits[FORMAT_INT_WIDTH];
    const std::size_t written = WriteInt(SaturatingIndex(i, offset), digits);

    // Assembled immediately before each send rather than once before the loop:
    // the send path is synchronous, so anything re-entering here would find the
    // buffer mid-use. Into memory reserved at construction, so no send
    // allocates. AppendAtom puts the single separating space in, and copies the
    // element's characters out of the AtomList's backing text — which is what
    // makes "the element keeps the spelling it arrived with" true character for
    // character rather than merely by type.
    render.assign(digits, written);
    list.AppendAtom(render, i);

    // Always a list, never a bare atom: the index in front of the element is
    // the object, so this deliberately does *not* take SendAtomRange's
    // one-atom-leaves-as-a-scalar route the way `.iter` does.
    outputs[0].SendList(render, thread);
  }
}

void gListFunnel::Take(const char* text, std::size_t length, YSE::THREAD thread) {
  if (!Enter()) return;

  // Replaced rather than accumulated: the object indexes the message in front
  // of it and holds nothing afterwards. An over-long message loses its tail and
  // keeps its head — `.zl`'s rule, and right here because the surplus is input
  // the patch has just sent rather than state the object was holding.
  list.Clear();
  const std::size_t refused = list.AddTokens(text, length);
  if (refused != 0) CountDrop(refused);

  Emit(thread);

  Leave();
}

// ─── the inlet ──────────────────────────────────────────────────────────────

INT_IN(FunnelInt) {
  // Max: "The low index value and the received number are sent out as a
  // two-element list." A one-element list, which is what the list path makes of
  // it. Formatted into a stack buffer, through the patcher's one spelling of a
  // number.
  if (inlet != 0) return;
  char text[kExprValueTextMax];
  const int written = ExprFormatValue(ExprValue::Int(value), text, kExprValueTextMax);
  if (written <= 0) return;
  Take(text, (std::size_t)written, thread);
}

FLOAT_IN(FunnelFloat) {
  // Max: "Performs the same as int", and the float keeps its decimal point so
  // that what leaves is what arrived.
  if (inlet != 0) return;
  char text[kExprValueTextMax];
  const int written = ExprFormatValue(ExprValue::Float(value), text, kExprValueTextMax);
  if (written <= 0) return;
  Take(text, (std::size_t)written, thread);
}

LIST_IN(FunnelList) {
  if (inlet != 0) return;

  std::size_t at = 0;
  if (MatchWord(value, "offset", 6, at)) {
    // Max: "The word offset followed by an integer argument is used to specify
    // an offset for the first index value." The same value the creation
    // argument sets. Run-time state: it does not write back to the parameters,
    // so it does not survive a save. A malformed argument is a method call that
    // does nothing rather than data to index.
    int shift = 0;
    if (!ReadIntArg(value, at, shift)) return;
    // Under the guard, so the index base cannot change halfway down a list
    // another thread is already walking.
    if (!Enter()) return;
    offset = shift;
    Leave();
    return;
  }

  // The bare word is the method with nothing to do. Indexing it as the symbol
  // it also is would answer a method call with `0 offset`, which no patch means.
  if (value == "offset") return;

  // Max: "Each element of the list is indexed and this index is prepended to
  // the list element and sent out the outlet as a two-element list", and
  // "anything: Performs the same as list" — which is why there is no message
  // word to strip here.
  Take(value.c_str(), value.size(), thread);
}

#undef className
