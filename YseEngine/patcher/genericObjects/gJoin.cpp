#include "gJoin.h"
#include "../../implementations/logImplementation.h"
#include "../math/gExprEval.h"
#include "../pAtomList.h"
#include "../pListArgs.h"
#include "../pObjectList.hpp"
#include "../pSelector.h"
#include <cstddef>
#include <string>

using namespace YSE::PATCHER;

#define className gJoin

namespace {

  // Max's one message word on this object's inlets. `.pack` reads the same one
  // for the same reason, and the objection `.prepend` records applies here too:
  // a list whose first item is the literal symbol `set` is swallowed. That is
  // Max's choice rather than this port's.
  constexpr char kWordSet[] = "set";

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

  constexpr char kInletDocLeft[] =
      "The first piece of the joined list, and — unless the trigger arguments say otherwise — the "
      "hot inlet. Whatever message arrives here is stored whole, however many items it carries and "
      "whatever they spell: this object concatenates pieces rather than collecting single typed "
      "values, which is the whole difference between it and .pack. Storing replaces what this "
      "inlet was holding; the other inlets keep theirs. When this inlet is hot the joined list "
      "goes "
      "out as soon as the store lands. A bang releases the list as it stands without storing, and "
      "is accepted here whether or not this inlet is hot — Max's 'outputs the currently stored "
      "list "
      "from any inlet'. 'set <message>' performs exactly the store the plain message would have "
      "performed and releases nothing, which is how a patch loads every inlet and then chooses "
      "when "
      "to send; 'set' followed by nothing empties this inlet, which then contributes nothing to "
      "the "
      "list. A store whose result would not fit the bounded list is refused whole — this inlet "
      "keeps what it had, and so does every other — and counted.";

  constexpr char kInletDocOther[] =
      "The piece of the joined list in this position. Whatever message arrives here is stored "
      "whole, however many items it carries, and replaces what this inlet was holding; the pieces "
      "in the other inlets are untouched. Whether writing it releases the list depends on the "
      "trigger arguments: by default only the leftmost inlet does, an argument naming this inlet's "
      "index makes it release too, and an argument of -1 makes every inlet release. A bang is "
      "accepted here regardless and sends the list as it stands without storing anything, which is "
      "Max's 'outputs the currently stored list from any inlet'. 'set <message>' stores without "
      "releasing, and 'set' followed by nothing empties this inlet so that it contributes nothing "
      "at all to the list. A store whose result would not fit the bounded list is refused whole "
      "and "
      "counted, leaving every inlet holding what it held.";

  constexpr char kOutletDoc[] =
      "The joined list: every inlet's piece laid end to end in inlet order, so its length is the "
      "sum of the lengths of the pieces rather than the number of inlets. Sent whenever a hot "
      "inlet "
      "is written, and whenever any inlet is banged. An inlet no message has reached carries its "
      "starting value of 0, and an inlet a 'set' has emptied contributes nothing. A result of one "
      "atom leaves as the int, float or symbol it spells rather than as a list of one, so it "
      "reaches the inlets an uncollected value would have reached; a result of no atoms at all — "
      "every inlet emptied — sends nothing rather than an empty message.";

} // namespace

CONSTRUCT() {
  // Every port is built by ShapePorts(), because the first argument *is* the
  // inlet count. The clear callback is what makes `SetParams("")` return the
  // object to Max's no-argument shape rather than leaving the previous inlet
  // count in place.
  REG_PARM_CLEAR;
  REG_PARM_PARSE;

  ADD_PARAM(creationArgs);

  // Max: "Defaults to two inlets with initial values of 0." Also the shape
  // ClearParams() restores.
  ShapePorts();

  // The one allocation the object makes outside its two lists, and it happens
  // here rather than on an arrival: the list is rendered into storage that is
  // already long enough, on whichever thread the message came in on —
  // routinely the audio callback.
  AtomList::ReserveRender(render);

  ADD_DESCRIPTION(
      "Concatenates what several inlets are holding into one list — Max's join, 'combines separate "
      "untyped items into an output list', and the variable-length counterpart of .pack. The two "
      "look alike from the outside and are not interchangeable: a .pack slot holds exactly one "
      "atom of a type its creation argument declared, so its output is always as long as its "
      "argument list, while a .join inlet holds whatever message arrived at it — however many "
      "items that was and whatever they spell — and the output is those pieces laid end to end. A "
      ".join 2 whose inlets hold '1 2' and '3 4' sends '1 2 3 4'; the same two messages into a "
      ".pack 0 0 send '1 3', because there each item after the first spreads rightwards into the "
      "neighbouring slot and the surplus is dropped. There is no type coercion at all, which is "
      "Max's 'separate untyped items': a variable-length piece has no per-item type to declare, "
      "and declaring one per inlet would say nothing about an inlet holding four items. .unjoin is "
      "the way back. The first creation argument is the number of inlets, 1 to 256, and with no "
      "arguments the object is Max's default of two inlets whose starting value is 0 — so a bang "
      "before anything arrives sends a complete list rather than silence. The arguments after it "
      "are Max's @triggers attribute, 'designates inputs that automatically trigger output': each "
      "one names an inlet that releases the list when written, and -1 makes every inlet hot. With "
      "none of them the leftmost inlet is the hot one, which is the arrangement .pack, .+ and "
      ".counter already use. Hot-ness being a creation argument is why there is no second "
      "registered name beside this one the way .pak sits beside .pack: it would be sugar for "
      "'.join <n> -1'. A bang sends the list as it stands without storing and is accepted on every "
      "inlet, Max's 'outputs the currently stored list from any inlet', so cold here means "
      "'writing "
      "me does not release' rather than 'I am inert'. 'set <message>' performs exactly the store "
      "the same message without the word would have performed while suppressing only the release, "
      "and 'set' with nothing after it empties an inlet, which then contributes nothing to the "
      "list. The joined list leaves as list text, or — when it holds a single atom — as the int, "
      "float or symbol that atom spells, since a list of one is not a list and this patcher does "
      "no coercion at an inlet; an object every one of whose inlets has been emptied sends nothing "
      "at all rather than an empty message. The storage is the bounded pre-allocated list the "
      "whole family shares: at most 256 atoms spanning at most 1024 characters between them, in "
      "memory reserved when the object is built, held already concatenated in inlet order with a "
      "small table saying how many atoms belong to each inlet, so a release has nothing to "
      "assemble. A store whose result would not fit is refused whole and counted rather than "
      "losing its tail, because what would be lost is the messages the other inlets are holding "
      "rather than surplus input; the count is a counter rather than a log line because the "
      "refusing thread may be the audio callback, while a creation argument that does not fit is "
      "logged, parameter parsing being control-thread only. Re-typing the creation arguments "
      "rebuilds the object, since they are the inlet count and the trigger set. Calculate() does "
      "nothing and no message path allocates, locks or blocks: numbers are rendered into a stack "
      "buffer, the store is a rebuild inside a list reserved at construction, and the release "
      "renders into a buffer reserved at the same time. Two threads writing the same object are "
      "serialised by a single test-and-set guard whose loser is dropped and counted rather than "
      "made to spin, which is also what stops an object wired back into one of its own inlets from "
      "recursing on the audio thread.");

  ADD_CATEGORY(pCategory::GENERIC);
}

// ─── the creation arguments ─────────────────────────────────────────────────

void gJoin::ShapePorts() {
  // Rebuilt rather than resized: the inlet *count* comes from the first
  // argument, so the inlets, the per-inlet table and the joined list all have
  // to agree. Safe because every caller runs before the object is wired or
  // published — the constructor, and the two parameter callbacks, which
  // patcherImplementation::CreateObjectUnlocked runs before AssignGraphIds.
  counts.clear();
  hot.clear();
  slots.Clear();
  work.Clear();

  // Parameters::Set splits on single spaces, so a run of them yields empty
  // tokens; an empty token is not an argument.
  int ports = DEFAULT_PORTS;
  int seen = 0;
  for (const std::string& token : creationArgs) {
    if (token.empty()) continue;
    seen++;
    if (seen != 1) continue;

    int requested = 0;
    if (!WholeInt(token, requested)) {
      // The control thread, before the object is wired or published, so this is
      // the one place a bad argument can be *said* rather than merely observable
      // through PortCount().
      INTERNAL::LogImpl().emit(
          E_WARNING, std::string("patcher: ") + YSE::OBJ::G_JOIN + " inlet count '" + token +
                         "' is not a whole number; using " + IntText(DEFAULT_PORTS));
      continue;
    }

    ports = requested;
    if (ports < MIN_PORTS) ports = MIN_PORTS;
    if (ports > MAX_PORTS) ports = MAX_PORTS;
    if (ports != requested) {
      INTERNAL::LogImpl().emit(E_WARNING, std::string("patcher: ") + YSE::OBJ::G_JOIN +
                                              " asked for " + IntText(requested) +
                                              " inlets; clamped to " + IntText(ports) + " (" +
                                              IntText(MIN_PORTS) + "-" + IntText(MAX_PORTS) + ")");
    }
  }

  hot.assign((std::size_t)ports, 0);

  // Max's @triggers, "designates inputs that automatically trigger output",
  // read here as the arguments after the inlet count because the patcher has no
  // attributes — `.zl <mode> <arg>`'s route.
  bool anyTrigger = false;
  int index = 0;
  for (const std::string& token : creationArgs) {
    if (token.empty()) continue;
    index++;
    if (index == 1) continue;

    int which = 0;
    if (!WholeInt(token, which)) {
      INTERNAL::LogImpl().emit(E_WARNING, std::string("patcher: ") + YSE::OBJ::G_JOIN +
                                              " trigger '" + token +
                                              "' is not a whole number; ignored");
      continue;
    }

    if (which <= TRIGGER_ALL) {
      // Max's "-1 makes all inlets hot". Every negative index reads the same
      // way: there is no inlet to the left of the leftmost one, so the only
      // other reading available is an error nobody asked for.
      for (char& flag : hot)
        flag = 1;
      anyTrigger = true;
      continue;
    }

    if (which >= ports) {
      INTERNAL::LogImpl().emit(E_WARNING, std::string("patcher: ") + YSE::OBJ::G_JOIN +
                                              " trigger " + IntText(which) +
                                              " names no inlet; the object has " + IntText(ports) +
                                              " (0-" + IntText(ports - 1) + ")");
      continue;
    }

    hot[(std::size_t)which] = 1;
    anyTrigger = true;
  }

  // Max's default, and the arrangement `.pack`, `.+` and `.counter` already
  // use: the leftmost inlet releases and the rest only store.
  if (!anyTrigger) hot[0] = 1;

  inputs.clear();
  outputs.clear();

  for (int i = 0; i < ports; i++) {
    // Max: "Defaults to two inlets with initial values of 0." Every inlet
    // starts holding the single atom 0, so a bang before anything arrives sends
    // a complete list rather than silence.
    slots.AddInt(0);
    counts.push_back(1);

    // Inlet 0 is the object's active one, as it is everywhere else in the
    // patcher; the rest are ordinary control inlets.
    if (i == 0) {
      ADD_IN_0;
    } else {
      inputs.emplace_back(this, false, i);
    }

    // Bang on every inlet, hot or cold — Max's "bang: Outputs the currently
    // stored list from any inlet". This is where the object parts company with
    // `.pack`, whose bang lives on its releasing inlets only, and registering it
    // everywhere is what makes GetAcceptedTypes() report the real contract.
    REG_BANG_IN(JoinBang);
    REG_INT_IN(JoinInt);
    REG_FLOAT_IN(JoinFloat);
    REG_LIST_IN(JoinList);
  }

  // ANY rather than LIST: a join that holds one atom sends the int, float or
  // symbol it spells rather than a list of one, and this patcher does no
  // coercion at an inlet.
  ADD_OUT_ANY;

  ApplyDocs();
}

void gJoin::ApplyDocs() {
  for (int i = 0; i < (int)inputs.size(); i++)
    inputs[(std::size_t)i].SetDoc(InletLabel(i), i == 0 ? kInletDocLeft : kInletDocOther, "any");

  if (!outputs.empty()) outputs[0].SetDoc("list", kOutletDoc, "any");
}

PARM_CLEAR() {
  // Runs on the control thread before the parameter string is re-read, and is
  // the whole of `SetParams("")`: Parameters::Set returns without calling the
  // parse callback for an empty argument, so this has to leave Max's
  // no-argument object behind rather than one still holding the previous
  // inlets.
  creationArgs.clear();
  ShapePorts();
}

PARM_PARSE() {
  ShapePorts();
}

// ─── the pieces ─────────────────────────────────────────────────────────────

bool gJoin::Hot(int index) const {
  if (index < 0 || index >= (int)hot.size()) return false;
  return hot[(std::size_t)index] != 0;
}

int gJoin::InletSize(int index) const {
  if (index < 0 || index >= (int)counts.size()) return 0;
  return counts[(std::size_t)index];
}

std::string gJoin::Joined() const {
  // Diagnostics only: this builds a string, which is exactly what the object's
  // own release path is written to avoid.
  std::string out;
  AtomList::ReserveRender(out);
  slots.Render(out);
  return out;
}

// ─── the guard ──────────────────────────────────────────────────────────────

bool gJoin::Enter() {
  if (busy.exchange(true, std::memory_order_acq_rel)) {
    // Another thread is mid-message, or this object is wired back into one of
    // its own inlets and a release has come round again. Counted rather than
    // spun on: this is a path the audio callback takes.
    CountDrop();
    return false;
  }
  return true;
}

void gJoin::Leave() {
  busy.store(false, std::memory_order_release);
}

void gJoin::CountDrop(std::size_t count) {
  dropped.fetch_add((std::uint64_t)count, std::memory_order_relaxed);
}

// ─── storing ────────────────────────────────────────────────────────────────

bool gJoin::Store(const char* text, std::size_t length, int at) {
  // The backing text of an AtomList is append-only until Clear(), so replacing
  // one inlet's piece is a rebuild: every piece written into the scratch list in
  // inlet order, then one Assign. Bounded, and allocation-free — both lists
  // reserved their text in the constructor. Written into `work` rather than in
  // place so that a store which does not fit leaves the joined list exactly as
  // it was.
  const int ports = (int)counts.size();
  work.Clear();

  std::size_t atom = 0;
  int stored = 0;

  for (int j = 0; j < ports; j++) {
    if (j != at) {
      // Every other inlet keeps its own piece, whatever its length: these are
      // the messages the patch's other cords delivered, not surplus input.
      for (int k = 0; k < counts[(std::size_t)j]; k++, atom++) {
        if (!work.Add(slots.AtomText(atom), slots.AtomLength(atom))) return false;
      }
      continue;
    }

    // The new piece, in place of whatever this inlet was holding — stored whole
    // rather than spread rightwards, which is the difference from `.pack`.
    std::size_t i = 0;
    while (i < length) {
      while (i < length && IsSelectorSeparator(text[i]))
        i++;
      if (i >= length) break;
      const std::size_t begin = i;
      while (i < length && !IsSelectorSeparator(text[i]))
        i++;
      if (!work.Add(text + begin, i - begin)) return false;
      stored++;
    }

    // Past the atoms this inlet used to own; they are not copied.
    atom += (std::size_t)counts[(std::size_t)j];
  }

  slots.Assign(work);
  counts[(std::size_t)at] = stored;
  return true;
}

void gJoin::Emit(YSE::THREAD thread) {
  // The family's transport convention: a joined list of one atom leaves as the
  // int, float or symbol it spells, and an object every one of whose inlets has
  // been emptied sends nothing at all — which is reachable here, unlike in
  // `.pack`, because an inlet may hold no atoms.
  SendAtoms(outputs[0], slots, render, thread);
}

void gJoin::Take(const char* text, std::size_t length, int inlet, bool emit, YSE::THREAD thread) {
  if (inlet < 0 || inlet >= (int)counts.size()) return;
  if (!Enter()) return;

  // Refused whole rather than losing its tail, which is where this parts
  // company with `.zl`: what would be lost is the messages the other inlets are
  // holding, and dropping them would silently rewrite state nobody touched.
  if (!Store(text, length, inlet)) CountDrop();

  // The store is settled before the release, so a patch that loops the outlet
  // back into an inlet finds the list it is being handed — and finds the guard
  // taken, which is what stops it recursing on the audio thread.
  if (emit) Emit(thread);

  Leave();
}

// ─── the inlets ─────────────────────────────────────────────────────────────

BANG_IN(JoinBang) {
  // Max: "bang: Outputs the currently stored list from any inlet." Stores
  // nothing, and accepted on every inlet whether or not it releases — cold here
  // means "writing me does not release", not "I am inert".
  if (inlet < 0 || inlet >= (int)counts.size()) return;
  if (!Enter()) return;
  Emit(thread);
  Leave();
}

INT_IN(JoinInt) {
  // Formatted and then stored through the same token path a list takes, so
  // there is one storage rule and one place a piece's length is decided. Into a
  // stack buffer, through the patcher's one spelling of a number.
  char text[kExprValueTextMax];
  const int written = ExprFormatValue(ExprValue::Int(value), text, kExprValueTextMax);
  if (written <= 0) return;
  Take(text, (std::size_t)written, inlet, Hot(inlet), thread);
}

FLOAT_IN(JoinFloat) {
  char text[kExprValueTextMax];
  const int written = ExprFormatValue(ExprValue::Float(value), text, kExprValueTextMax);
  if (written <= 0) return;
  Take(text, (std::size_t)written, inlet, Hot(inlet), thread);
}

LIST_IN(JoinList) {
  std::size_t at = 0;
  if (MatchWord(value, kWordSet, sizeof(kWordSet) - 1, at)) {
    // Max: "set: Stores list array without triggering output." The same store as
    // below, minus the release — one storage rule, so the two paths cannot drift
    // apart. `set` with only separators after it stores no atoms, which is how a
    // patch empties an inlet; a bare `set` with nothing at all after it is not
    // this message (MatchWord requires a separator) and falls through as a
    // symbol.
    Take(value.c_str() + at, value.size() - at, inlet, false, thread);
    return;
  }

  Take(value.c_str(), value.size(), inlet, Hot(inlet), thread);
}

#undef className
