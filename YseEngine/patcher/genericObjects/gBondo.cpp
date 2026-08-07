#include "gBondo.h"
#include "../../implementations/logImplementation.h"
#include "../math/gExprEval.h"
#include "../pListArgs.h"
#include "../pObjectList.hpp"
#include <cstddef>
#include <string>

using namespace YSE::PATCHER;

#define className gBondo

namespace {

  // An int as text, through the patcher's one int formatter rather than
  // std::to_string. Control-thread only (the two construction-time logs).
  std::string IntText(int value) {
    char digits[YSE::PATCHER::FORMAT_INT_WIDTH];
    const std::size_t written = YSE::PATCHER::WriteInt(value, digits);
    return std::string(digits, written);
  }

  constexpr char kInletDoc[] =
      "Hot, like every other inlet on this object. An int, float, list or symbol is stored here "
      "and then the whole set is released, right to left. 'set <anything>' performs the same "
      "store and releases nothing; a bang releases the set without storing. A list whose first "
      "token is a number is spread over this slot and the ones to its right unless the 'n' "
      "creation argument was given, in which case it is stored whole.";

  constexpr char kOutletDoc[] =
      "What the corresponding inlet is holding, sent on every release. Int 0 until that inlet has "
      "received something. Released right to left with every other outlet, each send completing "
      "in full before the outlet to its left is served.";

} // namespace

CONSTRUCT() {
  // Every port is built by ShapePorts(), so a saved `.bondo 5 0 n` comes back
  // with five inlets and five outlets. The clear callback is what makes
  // `SetParams("")` return the object to Max's no-argument shape rather than
  // leaving the previous port count in place.
  REG_PARM_CLEAR;
  REG_PARM_PARSE;

  ADD_PARAM(creationArgs);

  // Max: "The default number of inlets and outlets is 2." Also the shape
  // ClearParams() restores.
  ShapePorts();

  ADD_DESCRIPTION(
      "Holds one value per inlet and releases the whole set together the moment any one of them "
      "changes — Max's bondo, 'synchronizes and outputs a set of inputs when any input is "
      "received'. The problem it solves is incoherence in transit: a patch that has to change "
      "three related parameters at once sends them down three cords, and whatever is downstream "
      "sees the new first value against the old second and third for as long as it takes the next "
      "cord to be served — a gap that is invisible in a diagram and audible in a patch. .bondo "
      "closes it, so a reader is never handed a mixture of the old set and the new one. Every "
      "inlet is hot, and that is the object: this is the reverse of almost every other "
      "multi-inlet box here, where one inlet acts and the rest are cold. Max says it once per "
      "method and says it the same way each time — 'In any inlet: the input is stored in the "
      "location corresponding to that inlet, and causes anything previously stored to be sent out "
      "its corresponding outlet' — so a patch does not have to know which of its parameters moved "
      "and does not have to route a bang alongside them; whichever one moves carries the others "
      "out with it. Its sibling buddy is the opposite policy, waiting until every inlet has "
      "something before releasing once; .bondo never waits. There are two ways to store without "
      "releasing. 'set <anything>' stores into the receiving inlet and emits nothing, which is the "
      "only way to write an inlet quietly since there are no cold inlets to do it with, and here "
      "it performs exactly the store the same message without the word would have performed and "
      "suppresses only the output. A bang is the other half — 'send all stored messages' — "
      "storing nothing and releasing everything, in any inlet, so 'set' three values then bang is "
      "the manual mode and it composes with the automatic one rather than replacing it. An inlet "
      "that has never been written releases int 0, as Max's does, so a reader wired to every "
      "outlet gets a complete set from the very first release and never needs a 'have I seen this "
      "one yet' flag of its own. Stored values persist rather than being consumed: a release does "
      "not empty the object, so banging twice sends the same set twice and changing one inlet "
      "re-sends the others unchanged — a consuming object would emit zeros on the second bang, "
      "which is the incoherence this box exists to prevent. Outlet n-1 is served first and outlet "
      "0 last, each send completing in full (the whole subgraph behind it, depth first) before the "
      "next starts, which is Max's right-to-left rule and load-bearing here: the idiom wires "
      "outlet 0 into whatever acts and the rest into whatever stores, so by the time the leftmost "
      "value lands the rest of the set is already in place. A release happens inside the call "
      "frame of the message that caused it, so the whole set is one logical event and .bondo into "
      ".next gives one separated bang and the rest continued, however many outlets there are. A "
      "list whose first token is a number is spread across the outlets from the receiving one "
      "rightwards, as Max spreads one, and anything else is stored whole in the single slot — the "
      "leading-token test is Max's own rule for which method a message reaches, and it is what "
      "this patcher can use in place of a symbol type it does not have. The 'n' creation argument "
      "turns distribution off so that a whole list is stored per outlet, which is the mode to "
      "reach for when an inlet's value is itself a list. Max's delay argument defers a "
      "message-triggered release by that many milliseconds through the patcher's deferred-message "
      "scheduler: the release fires inside the patcher's own dispatch as one fresh logical event, "
      "so the one-release-is-one-event guarantee keeps holding through the deferral. One release "
      "is pending at a time — a new message reschedules it, so a burst of stores collapses into "
      "one deferred release of the final set — and a bang is Max's documented exception, "
      "releasing immediately and leaving a pending release to fire at its own time. A standalone "
      "object outside any patcher has no dispatch to defer into and releases immediately. At most "
      "256 inlet/outlet pairs are built, and the slot table is sized once before the object is "
      "published — Calculate() does nothing, and no message path allocates, locks or blocks.");
  ADD_CATEGORY(pCategory::GENERIC);
  PARAM_DOC("ports", "2",
            "Max's argument list, in Max's order and read by type. The first whole number is how "
            "many inlet/outlet pairs to build, clamped to 1-256 and defaulting to 2. A second "
            "whole number is Max's delay in milliseconds: a message-triggered release is deferred "
            "by that long (block-quantised on the patcher's clock, rescheduled by each new "
            "message), while a bang still releases immediately. The symbol 'n' stores a whole "
            "list per outlet instead of spreading it across them. Anything else is ignored.",
            "1-256, optional delay in ms, optional 'n'");
}

void gBondo::ShapePorts() {
  // Rebuilt rather than resized: the port *count* comes from the arguments, so
  // both sides and the slot table have to agree. Safe because every caller runs
  // before the object is wired or published — the constructor, and the two
  // parameter callbacks, which patcherImplementation::CreateObjectUnlocked runs
  // before AssignGraphIds. A *live* SetParams never reaches here on a published
  // object: registering the callbacks makes ParamsNeedRebuild() true, so #234
  // replaces the object instead.
  int count = DEFAULT_PORTS;

  // Max's typed-positional arguments: the first number is the port count, the
  // second is the delay, and the symbol `n` can sit in either symbol position.
  bool haveCount = false;
  bool haveDelay = false;
  int requested = DEFAULT_PORTS;
  wholeLists = false;
  requestedDelay = 0;

  for (const std::string& token : creationArgs) {
    // Parameters::Set splits on single spaces, so a run of them yields empty
    // tokens; an empty argument is neither a number nor the flag.
    if (token.empty()) continue;

    if (token == "n") {
      // Max: "Using the symbol 'n' as an argument, bondo is able to synchronize
      // lists which arrive in different inlets."
      wholeLists = true;
      continue;
    }

    float number = 0.f;
    // Strict on purpose, as the rest of the family is: ExprParseFloatList would
    // read `5abc` as 5 and fold `1e999` to 0, and neither answers "is this
    // creation argument a number at all".
    if (!ReadNumericToken(token, number)) continue;

    if (!haveCount) {
      requested = ExprToInt(number);
      count = requested;
      if (count > MAX_PORTS) count = MAX_PORTS;
      if (count < MIN_PORTS) count = MIN_PORTS;
      haveCount = true;
    } else if (!haveDelay) {
      requestedDelay = ExprToInt(number);
      haveDelay = true;
    }
  }

  // The control thread, before the object is wired or published, so this is the
  // one place a clamp or a refusal can be *said* rather than merely made
  // observable through PortCount() and RequestedDelay().
  if (haveCount && requested != count) {
    INTERNAL::LogImpl().emit(E_WARNING, "patcher: .bondo port count " + IntText(requested) +
                                            " is outside " + IntText(MIN_PORTS) + "-" +
                                            IntText(MAX_PORTS) + "; clamped to " + IntText(count));
  }

  inputs.clear();
  outputs.clear();

  for (int i = 0; i < count; i++) {
    // Inlet 0 is the object's active one, as it is everywhere else in the
    // patcher; the rest are ordinary control inlets. That is a DSP-readiness
    // distinction, not a hot/cold one — every inlet here releases.
    if (i == 0) {
      ADD_IN_0;
    } else {
      inputs.emplace_back(this, false, i);
    }
    REG_BANG_IN(SetBang);
    REG_INT_IN(SetInt);
    REG_FLOAT_IN(SetFloat);
    REG_LIST_IN(SetList);
    inputs.back().SetDoc(InletLabel(i), kInletDoc, "any");

    // ANY rather than a fixed type: a slot holds whichever of int, float and
    // text last arrived, and releases it as itself.
    ADD_OUT_ANY;
    outputs.back().SetDoc(OutletLabel(i), kOutletDoc, "any");
  }

  // Rebuilt with the ports so the two can never disagree on how many there are;
  // a re-parse deliberately forgets the held set, since the slots it belonged
  // to no longer exist.
  slots.clear();
  slots.resize((std::size_t)count);
  for (Slot& slot : slots)
    // The one allocation a store would otherwise need. Reserved after the final
    // resize, so no vector reallocation can move these strings and lose it.
    slot.text.reserve(TEXT_CAPACITY);
}

PARM_CLEAR() {
  // Runs on the control thread before the parameter string is re-read, and is
  // the whole of `SetParams("")`: Parameters::Set returns without calling the
  // parse callback for an empty argument, so this has to leave Max's
  // no-argument object behind rather than one still holding the previous port
  // count.
  creationArgs.clear();
  ShapePorts();
}

PARM_PARSE() {
  ShapePorts();
}

gBondo::Held gBondo::HeldKind(int index) const {
  if (index < 0 || index >= (int)slots.size()) return Held::NONE;
  return slots[(std::size_t)index].held;
}

int gBondo::HeldInt(int index) const {
  if (index < 0 || index >= (int)slots.size()) return 0;
  const Slot& slot = slots[(std::size_t)index];
  return slot.held == Held::INT ? slot.intValue : 0;
}

float gBondo::HeldFloat(int index) const {
  if (index < 0 || index >= (int)slots.size()) return 0.f;
  const Slot& slot = slots[(std::size_t)index];
  return slot.held == Held::FLOAT ? slot.floatValue : 0.f;
}

std::string gBondo::HeldText(int index) const {
  if (index < 0 || index >= (int)slots.size()) return std::string();
  const Slot& slot = slots[(std::size_t)index];
  return slot.held == Held::LIST ? slot.text : std::string();
}

void gBondo::StoreToken(const char* token, std::size_t length, int index) {
  Slot& slot = slots[(std::size_t)index];

  float number = 0.f;
  if (ReadNumericToken(token, length, number)) {
    // The int-atom / float-atom test lives in pListArgs.h next to the reader
    // that agreed the token is a number; `.trigger` classifies its constants
    // and `.match` echoes its values back through the same answer.
    if (TokenLooksLikeFloat(token, length)) {
      slot.held = Held::FLOAT;
      slot.floatValue = number;
    } else {
      // Spelled as an int, so it is an int atom — but the token may still be
      // wider than an int (ReadNumericToken only promised a finite float), so
      // truncate through the range-checked conversion rather than casting.
      slot.held = Held::INT;
      slot.intValue = ExprToInt(number);
    }
    return;
  }

  slot.held = Held::LIST;
  // Into the buffer reserved when the ports were shaped: no allocation for text
  // up to TEXT_CAPACITY, and one for anything longer.
  slot.text.assign(token, length);
}

void gBondo::DistributeTokens(const std::string& text, std::size_t offset, int first) {
  // Max: "The first element in the list is sent out the outlet which
  // corresponds to the inlet which received the list and each subsequent
  // element in the list is sent out each subsequent outlet." Elements past the
  // last outlet have nowhere to go and are dropped.
  const std::size_t size = text.size();
  const int count = (int)slots.size();
  std::size_t i = offset;
  int index = first;

  while (i < size && index < count) {
    while (i < size && (text[i] == ' ' || text[i] == '\t'))
      i++;
    if (i >= size) break;

    const std::size_t start = i;
    while (i < size && text[i] != ' ' && text[i] != '\t')
      i++;

    StoreToken(text.c_str() + start, i - start, index);
    index++;
  }
}

void gBondo::StoreText(const std::string& text, std::size_t offset, int inlet) {
  if (inlet < 0 || inlet >= (int)slots.size()) return;

  // Which of Max's two methods this message would have reached. Max's parser
  // calls a message a `list` when its first atom is a number and an `anything`
  // when it is a symbol, and the two are stored differently; this patcher has
  // one message type behind both, so the leading token has to answer it — which
  // is the same question Max's own parser asks, not a rule invented here.
  std::size_t i = offset;
  const std::size_t size = text.size();
  while (i < size && (text[i] == ' ' || text[i] == '\t'))
    i++;
  const std::size_t start = i;
  while (i < size && text[i] != ' ' && text[i] != '\t')
    i++;

  float number = 0.f;
  const bool leadsWithNumber =
      (i > start) && ReadNumericToken(text.c_str() + start, i - start, number);

  if (leadsWithNumber && !wholeLists) {
    DistributeTokens(text, offset, inlet);
    return;
  }

  // Max's `anything`, and everything in `n` mode: "The input is stored in the
  // location corresponding to that inlet." Stored verbatim from `offset` rather
  // than through substr(), which would allocate a temporary.
  Slot& slot = slots[(std::size_t)inlet];
  slot.held = Held::LIST;
  slot.text.assign(text.c_str() + offset, size - offset);
}

void gBondo::EmitAll(YSE::THREAD thread) {
  // **The object.** Right to left, and each Send returns only once the whole
  // subgraph behind that outlet has run — see the header on why that is a
  // guarantee rather than a coincidence. Walking forwards here would hand the
  // leftmost reader a half-updated set, which is the bug one level down from
  // the one this object exists to prevent, so it is worth being loud: the loop
  // counts down.
  //
  // Signed index rather than a reverse iterator so the "n-1 down to 0" reads
  // the way the guarantee is stated. No allocation, no lock, no I/O on any
  // branch: nothing here formats, converts or copies.
  for (int i = (int)slots.size() - 1; i >= 0; i--) {
    const Slot& slot = slots[(std::size_t)i];
    switch (slot.held) {
    case Held::NONE:
      // Max: "If no message has yet been received in a particular inlet, 0 is
      // sent out of the corresponding outlet." Never silence — a release is
      // always a complete set.
      outputs[(std::size_t)i].SendInt(0, thread);
      break;
    case Held::INT:
      outputs[(std::size_t)i].SendInt(slot.intValue, thread);
      break;
    case Held::FLOAT:
      outputs[(std::size_t)i].SendFloat(slot.floatValue, thread);
      break;
    case Held::LIST:
      outputs[(std::size_t)i].SendList(slot.text, thread);
      break;
    }
  }
}

void gBondo::ReleaseOrDefer(YSE::THREAD thread) {
  // The message-triggered release. Max: "If output is triggered by a message,
  // and a second argument has been typed in, output will be delayed by the
  // number of milliseconds specified in the second argument."
  if (requestedDelay > 0) {
    messageScheduler* scheduler = Scheduler();
    if (scheduler != nullptr) {
      // One clock per object, Max's shape: a release armed while one is
      // pending reschedules it — cancel-then-arm, both wait-free — so a burst
      // of stores collapses into a single release of the final set, delay
      // measured from the newest message. The bang payload is enough: the
      // slots are the state, and the delivery emits whatever they hold *then*.
      if (pendingRelease != 0) {
        scheduler->Cancel(pendingRelease);
      }
      pendingRelease = scheduler->ScheduleBang(this, 0, requestedDelay);
      if (pendingRelease != 0) return;
      // Pending set full (the scheduler is bounded): fall through to the
      // immediate release rather than dropping the set on the floor.
    }
    // No scheduler: a standalone object has no dispatch to defer into, so the
    // release is immediate — the pre-#628 behaviour, now scoped to the one
    // case where deferring is impossible rather than merely unimplemented.
  }
  EmitAll(thread);
}

void gBondo::DeliverDeferred(const deferredMessage&, YSE::THREAD thread) {
  // The deferred release coming due. The scheduler wraps this in a fresh
  // messageEventScope, so the whole set below is one logical event — the
  // guarantee the header pins, preserved through the deferral (#628).
  pendingRelease = 0;
  EmitAll(thread);
}

BANG_IN(SetBang) {
  // Max: "bang: Send all stored messages." Stores nothing, and works in any
  // inlet — there is no inlet on this object that a bang means something
  // different in. "Output will be immediate if triggered by a bang": the delay
  // argument does not apply here, and a pending deferred release is left to
  // fire at its own time.
  (void)inlet;
  EmitAll(thread);
}

INT_IN(SetInt) {
  // Max: "In any inlet: The input is stored in the location corresponding to
  // that inlet, and causes anything previously stored to be sent out its
  // corresponding outlet." The store is settled before the release, so a patch
  // that loops an outlet back into an inlet finds the set it is being handed.
  if (inlet < 0 || inlet >= (int)slots.size()) return;
  Slot& slot = slots[(std::size_t)inlet];
  slot.held = Held::INT;
  slot.intValue = value;
  ReleaseOrDefer(thread);
}

FLOAT_IN(SetFloat) {
  // "float: Performs the same function as int" — kept as a float rather than
  // truncated, since the slot releases whatever it was given.
  if (inlet < 0 || inlet >= (int)slots.size()) return;
  Slot& slot = slots[(std::size_t)inlet];
  slot.held = Held::FLOAT;
  slot.floatValue = value;
  ReleaseOrDefer(thread);
}

LIST_IN(SetList) {
  if (inlet < 0 || inlet >= (int)slots.size()) return;

  std::size_t at = 0;
  if (MatchWord(value, "set", 3, at)) {
    // Max: "The word set, followed by any message, stores the input in the
    // location corresponding to that inlet without triggering any output." The
    // same store as below, minus the release — one storage rule, so the two
    // paths cannot drift apart. A bare `set` with nothing after it is not this
    // message (MatchWord requires a separator) and falls through as a symbol,
    // which is the reading the rest of the patcher's word-plus-argument
    // messages already take.
    StoreText(value, at, inlet);
    return;
  }

  StoreText(value, 0, inlet);
  ReleaseOrDefer(thread);
}
