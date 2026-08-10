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
      "The message to index. Every element leaves the outlet as its own two-element list, the "
      "element's position plus the offset followed by the element itself, first to last. Each send "
      "completes in full — the whole subgraph behind the outlet — before the next pair leaves, so "
      "the pairs arrive as a sequence rather than all at once. An int or a float is a one-element "
      "message and so leaves as the offset paired with the number, and anything works exactly like "
      "a list. 'offset <n>' changes the index the first element is given and sends nothing; the "
      "bare word does nothing at all. There is no bang method, Max documents none, because the "
      "object holds no message between stimuli. A message longer than the shared bound of 256 "
      "elements loses its tail, which is counted, and its head is indexed. One message here costs "
      "one send per element, up to 256 of them, on whichever thread sent it.";

  constexpr char kOutletDoc[] =
      "One two-element list per element of the last message: the element's position plus the "
      "offset, then the element. The index is always an int and the element keeps the spelling it "
      "arrived with, character for character, since it is copied rather than re-formatted. This is "
      "always a list — unlike .iter, which sends the bare atom — because the index is what makes "
      "the element addressable by .route, .sel or .spray. A message with no elements sends nothing "
      "at all. The sends are synchronous and consecutive: the whole subgraph behind this outlet "
      "runs to completion for one pair before the next pair is sent, so a patch may accumulate "
      "downstream and rely on the order.";

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
      "Indexes the elements of a message and sends each one out as an index/element pair — Max's "
      "listfunnel, which 'outputs the elements of an incoming list in the format: [index] "
      "[element] for each element of the list'. It is the list side of .funnel: .funnel takes the "
      "tag from the wiring, so a value arriving at inlet 2 leaves as 2 followed by the value and a "
      "patch with eight numbered sources needs eight cords, while this takes the tag from the "
      "position in the message, so one cord carrying a b c becomes 0 a, then 1 b, then 2 c. Same "
      "output shape and same downstream consumers — .route, .sel, .spray — but the length of the "
      "list need not be known when the patch is drawn, which is why Max notes that it conveniently "
      "replaces an unpack feeding a funnel: that pair has to be told how many elements there are "
      "in two places at once, and a list one element longer than expected quietly loses its tail. "
      "Next to .iter it is the same walk with the index left on: both turn one arriving message "
      "into one message per element down a single cord, first to last, each send completing in "
      "full — the whole subgraph behind the outlet, depth first — before the next begins, because "
      "the patcher's send path calls the target inlet directly with no queue in between. The "
      "difference is what a downstream object can do with the result: .iter sends the element as "
      "the int, float or symbol it spells, so it reaches the ordinary scalar inlets, while this "
      "always sends a two-element list, so the element is addressed rather than merely delivered. "
      "A patch that wants to do something to every element reaches for .iter; a patch that wants "
      "to put element k somewhere that depends on k reaches for this. It follows that there is no "
      "bang here where .iter has one — Max documents none, and iter's bang re-sends the message "
      "most recently received, which means iter holds a message as state while this holds nothing "
      "between messages but its offset; the inlet declines bang outright rather than swallowing "
      "it, so the accepted-type report is the real contract. The creation argument is Max's "
      "starting index value and the offset message 'specif[ies] an offset for the first index "
      "value', one value from two directions as on .funnel and .spray, so element k leaves tagged "
      "offset plus k. It exists because the numbers a downstream object expects usually start "
      "where it starts — a .spray bank with its own offset, MIDI channels at 1, a table's rows at "
      "some base — and the alternative is a .+ on every element after the split, kept in step by "
      "hand; a .listfunnel into a .spray with the same offset sends element k to outlet k. offset "
      "is run-time state rather than a parameter, as .funnel's and .spray's are, so a saved patch "
      "carries the creation argument, and the addition is saturated at the int limits rather than "
      "wrapped, since a wrapped index reads as a perfectly plausible one. An int or a float is a "
      "one-element message and leaves as the offset paired with the number, Max's 'the low index "
      "value and the received number are sent out as a two-element list', and anything is a list, "
      "so there is no message word to strip beyond the offset method itself. The index is always "
      "an int and the element keeps the spelling it arrived with, character for character, because "
      "it is copied out of the stored text rather than re-formatted. A message with no elements "
      "sends nothing at all rather than an empty message. The arriving message is read into the "
      "bounded pre-allocated storage the whole list family shares: at most 256 elements spanning "
      "at most 1024 characters between them, in memory reserved when the object is built. A "
      "message too long for it loses its tail, which is counted, and its head is indexed — the "
      "surplus here is input the patch has just sent rather than state the object was holding, "
      "which is why it is not refused whole the way .join's store is, and the count is a counter "
      "rather than a log line because the refusing thread may be the audio callback. One arriving "
      "message therefore becomes at most 256 sends, all inside the call frame of the one send that "
      "started it; the patcher's value-command queue is 256 deep, so a full-length burst feeding a "
      ".s from the control thread can saturate it and hit its documented drop-and-log "
      "backpressure. Calculate() does nothing and no message path allocates, locks or blocks: "
      "numbers are rendered into a stack buffer, the message is collected into storage reserved at "
      "construction, and the pairs are assembled in a buffer reserved at the same time. Two "
      "threads sending to the same object are serialised by a single test-and-set guard whose "
      "loser is dropped and counted rather than made to spin, and that guard is held across the "
      "whole walk on purpose: a cord from the outlet back to the inlet re-enters the handler from "
      "inside the walk, and letting it through would restart an iteration that never terminates "
      "and rewrite the list being walked. The offset message takes the same guard, which is what "
      "keeps the index base fixed for the length of one list rather than able to change halfway "
      "down it.");

  ADD_CATEGORY(pCategory::GENERIC);

  INLET_DOC(0, "in", kInletDoc, "any");
  OUTLET_DOC(0, "out", kOutletDoc, "list");

  PARAM_DOC("offset", "0",
            "Max's starting index value: the index the first element of a message is sent with, "
            "each later element counting up from it. Defaults to 0, so an unmodified object "
            "numbers from zero. It is also the starting value of the 'offset' message, which is "
            "run-time state and does not write back here, so a saved patch carries this argument "
            "rather than whatever the last message set. A float is truncated; anything that is not "
            "a whole finite number is ignored and leaves the default in place. Not clamped — a "
            "patch may legitimately want indices in any range — and the addition is saturated at "
            "the int limits where it is done.",
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
