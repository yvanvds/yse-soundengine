#include "gRouter.h"
#include "../../implementations/logImplementation.h"
#include "../math/gExprEval.h"
#include "../pListArgs.h"
#include "../pObjectList.hpp"
#include <cstddef>
#include <string>

using namespace YSE::PATCHER;

#define className gRouter

namespace {

  // An int as text, through the patcher's one int formatter rather than
  // std::to_string. Control-thread only (the construction-time clamp logs).
  std::string IntText(int value) {
    char digits[YSE::PATCHER::FORMAT_INT_WIDTH];
    const std::size_t written = YSE::PATCHER::WriteInt(value, digits);
    return std::string(digits, written);
  }

  // Exactly `count` integers starting at `at`, with nothing but whitespace
  // after them. Returns false and writes nothing useful otherwise.
  //
  // Strict on both ends on purpose. A short list is not the message Max
  // documents — `connect 0` names no outlet — and a long one is not either:
  // `ReadIntArgAt` stops at the first thing it cannot read, so without the
  // trailing check `connect 0 1 please` would quietly become `connect 0 1` and
  // a patch's typo would look like a working connection. Same real-time
  // properties as the readers it is built from: no allocation, no locale, no
  // exception.
  bool ReadExactInts(const std::string& text, std::size_t at, int* out, int count) {
    std::size_t cursor = at;
    for (int i = 0; i < count; i++) {
      if (!YSE::PATCHER::ReadIntArgAt(text, cursor, out[i])) return false;
    }
    while (cursor < text.size() && (text[cursor] == ' ' || text[cursor] == '\t'))
      cursor++;
    return cursor == text.size();
  }

  // True when `text` is the bare message word `word`, or that word followed by
  // arguments. Max types `clear`, `dump` and `print` as taking an `arguments
  // [list]`, so trailing text is tolerated where those three are concerned;
  // `MatchWord` alone would reject the bare word, which is the form a patch
  // actually sends.
  bool IsWordMessage(const std::string& text, const char* word, std::size_t wordLength) {
    if (text.size() < wordLength) return false;
    if (text.compare(0, wordLength, word) != 0) return false;
    if (text.size() == wordLength) return true;
    const char separator = text[wordLength];
    return separator == ' ' || separator == '\t';
  }

  constexpr char kControlDoc[] =
      "The connection inlet, and the one inlet nothing is routed from. A list of three numbers is "
      "'an inlet number, an outlet number, and a 0 or 1 specifying the state of a connection', "
      "with any non-zero state connecting. 'connect <inlet> <outlet>' and 'disconnect <inlet> "
      "<outlet>' name the same cell in words; 'patch <inlet> <outlet>' connects it and disconnects "
      "every other inlet from that outlet, making the outlet's source exclusive in one message; "
      "'clear' disconnects everything; 'dump' sends the whole matrix out the rightmost outlet. "
      "Inlet numbers count the routable inlets from 0, so 'connect 0 0' addresses the inlet "
      "immediately to the right of this one. A number naming a port the object does not have, and "
      "a message that is not one of these forms, are ignored. 'print' is accepted and does nothing "
      "— the patcher is headless and has no Max Console; 'dump' is the question a patch can read "
      "the answer to.";

  constexpr char kDataInletDoc[] =
      "A bang, int, float, list or symbol arriving here is sent, unchanged, out every outlet this "
      "inlet is currently connected to — none, one, or all of them — in Max's universal "
      "right-to-left order. Nothing is routed until the control inlet says so, and nothing about "
      "the message itself selects a destination: the routing is the object's state, which is what "
      "lets a destination change without the source knowing. Message words are not interpreted "
      "here, so 'connect 0 1' arriving at this inlet is data to forward and not a method call.";

  constexpr char kDataOutletDoc[] =
      "Whatever arrived at any inlet currently connected to this outlet, forwarded with its kind "
      "and spelling intact — the object switches, it does not convert. Several inlets may reach "
      "this outlet at once, in which case each of them sends here independently.";

  constexpr char kDumpOutletDoc[] =
      "The switching matrix, in answer to a 'dump' on the control inlet: one list per cell, in the "
      "form '<inlet> <outlet> <state>', ascending by inlet and then by outlet, so a patch can read "
      "the whole routing back out and store it. Silent otherwise — no routed message ever leaves "
      "here. A dump requested while one is already running is ignored rather than nested.";

} // namespace

CONSTRUCT() {
  // Every port is built by ShapePorts(), so a saved `.router 4 8` comes back
  // with four routable inlets and eight routable outlets. The clear callback is
  // what makes `SetParams("")` return the object to Max's no-argument shape
  // rather than leaving the previous counts in place.
  REG_PARM_CLEAR;
  REG_PARM_PARSE;

  ADD_PARAM(creationArgs);

  // Both counts default to DEFAULT_PORTS. Also the shape ClearParams()
  // restores.
  ShapePorts();

  ADD_DESCRIPTION(
      "A message crossbar whose connections are set by messages rather than by patch cords — Max's "
      "router, where 'any message received in any but the leftmost inlet will be routed to the "
      "outlet to which the inlet is currently connected'. A switching matrix, and the point of it "
      "is when the switching happens: every other way of getting a message from source i to "
      "destination j fixes the answer at edit time, and in this patcher changing a patch cord "
      "means "
      "a whole GraphState rebuilt on the control thread and published between audio blocks, while "
      "a "
      ".router connection change is one message that writes one byte into a table the object "
      "already owns and takes effect on the next message through. That is what makes 'rewire this "
      "while it runs' something a patch can do to itself — from a .metro, a .sel or a preset — "
      "rather than something only an editor can do to a patch. And it is many-to-many: several "
      "inlets may reach the same outlet, one inlet may reach several outlets, and both hold at "
      "once, because the state is a full inlet-by-outlet matrix and not a selection. That is the "
      "difference from the rest of the routing family. It is not .gate, which routes one inlet to "
      "one of N outlets chosen by a number arriving as data on the same event being routed, nor "
      ".switch, which is .gate's N-to-one mirror; neither can express 'inlet 1 reaches outlets 0 "
      "and 3 while inlet 2 also reaches outlet 3', which is one message here. It is not .spray, "
      "which addresses numbered outlets from an index riding in the message and holds no routing "
      "state at all — here the message carries nothing but itself, so a source needs to know "
      "nothing about where it is going, which is precisely what lets the destination be changed "
      "without touching the source. It is not .decode, which also speaks on every outlet but "
      "speaks "
      "its own 1s and 0s describing a selection, where this forwards and the values are the "
      "patch's. And it is not .funnel, the degenerate one-column case plus a tag; the tag is "
      "exactly what a crossbar must not add, since one that rewrote what passed through it would "
      "not be transparent to swap in. The leftmost inlet is not routable — Max scopes every routed "
      "method to 'any but the leftmost inlet' — so the object has one more inlet than asked for "
      "and "
      "the extra one takes the connection messages, accepting a list and declining bang, int and "
      "float outright the way .decode and .spray decline what Max does not document. Inlet numbers "
      "in those messages count the routable inlets from 0, so 'connect 0 0' addresses the inlet "
      "immediately right of the control inlet; numbering from the physical inlet 0 would make "
      "'connect 0 x' name an inlet no message can pass through. Symmetrically there is one more "
      "outlet than asked for, the rightmost, which carries nothing but the answer to dump. Four "
      "spellings set a connection, all of them Max's: the bare list '<inlet> <outlet> <state>', "
      "the "
      "form a patch computes, with any non-zero state connecting; 'connect' and 'disconnect' "
      "naming "
      "the same cell in words, of which Max says 'multiple inlets can be connected to multiple "
      "outlets, and vice versa'; 'patch <inlet> <outlet>', which 'connects an inlet to an outlet "
      "and disconnects all other inlets that are currently connected to that outlet' and is the "
      "one "
      "column operation — it makes an outlet's source exclusive in a single message, where two "
      "messages would leave a moment with two sources or none; and 'clear', after which 'all "
      "inlets "
      "are disconnected from all outlets'. A number naming a port the object does not have is "
      "ignored, and so is a malformed message: .gate's, .spray's and .decode's discipline, since "
      "folding a bad index onto a real port would hide a miscount at the object best placed to "
      "expose it. dump 'sends the state of the object's switching matrix out the right outlet as a "
      "series of single line lists in the form inlet-number outlet-number state' — every cell, "
      "ascending by inlet then by outlet, so the routing is recoverable by a patch and not merely "
      "by a person, which is the only way state that lives in messages rather than in the file can "
      "be saved at all. A dump requested while one is running is ignored rather than nested, since "
      "a dump line fed back into the control inlet would otherwise start a full dump inside every "
      "line of the one already running. print is accepted and does nothing: it targets the Max "
      "Console, an application window this headless patcher does not have, and the engine's own "
      "log "
      "is not a substitute because no patcher object logs from a message handler — a handler runs "
      "on whichever thread sent the message, and that may be the audio thread. At most 256 "
      "routable "
      "inlets and 256 routable outlets, built once before the object is published. Calculate() "
      "does "
      "nothing, and no message path allocates, locks or blocks: routing reads one row and sends, a "
      "connection message writes one byte, and the row is snapshotted onto the stack before the "
      "first send so a patch looping an outlet back into the control inlet cannot deliver one "
      "message under two different routings.");
  ADD_CATEGORY(pCategory::GENERIC);
  PARAM_DOC("inlets", "2",
            "Max's argument list, in Max's order: how many routable inlets to build, then how many "
            "routable outlets. Each is clamped to 1-256 and defaults to 2 — Max documents both as "
            "optional and states no default, so the family's is used. The object builds one inlet "
            "and one outlet more than these, for the connection messages and for dump. A float is "
            "truncated; anything that is not a whole finite number is ignored and leaves that "
            "default in place. The connections themselves are not parameters and do not survive a "
            "save — 'dump' is how a patch reads them back.",
            "1-256, optional outlets 1-256");
}

void gRouter::ShapePorts() {
  // Rebuilt rather than resized: both port *counts* come from the arguments, so
  // the two sides and the matrix have to agree. Safe because every caller runs
  // before the object is wired or published — the constructor, and the two
  // parameter callbacks, which patcherImplementation::CreateObjectUnlocked runs
  // before AssignGraphIds. A *live* SetParams never reaches here on a published
  // object: registering the callbacks makes ParamsNeedRebuild() true, so #234
  // replaces the object instead.
  int counts[2] = {DEFAULT_PORTS, DEFAULT_PORTS};
  int requested[2] = {DEFAULT_PORTS, DEFAULT_PORTS};
  bool clamped[2] = {false, false};
  int read = 0;

  for (const std::string& token : creationArgs) {
    if (read >= 2) break;

    // Parameters::Set splits on single spaces, so a run of them yields empty
    // tokens; an empty argument is not a number.
    if (token.empty()) continue;

    float number = 0.f;
    // Strict on purpose, as the rest of the family is: ExprParseFloatList would
    // read `5abc` as 5 and fold `1e999` to 0, and neither answers "is this
    // creation argument a number at all".
    if (!ReadNumericToken(token, number)) continue;

    requested[read] = ExprToInt(number);
    counts[read] = requested[read];
    if (counts[read] > MAX_PORTS) counts[read] = MAX_PORTS;
    if (counts[read] < MIN_PORTS) counts[read] = MIN_PORTS;
    clamped[read] = (requested[read] != counts[read]);
    read++;
  }

  // The control thread, before the object is wired or published, so this is the
  // one place a clamp can be *said* rather than merely made observable through
  // InletCount() and OutletCount().
  static const char* const kSides[2] = {"inlet", "outlet"};
  for (int side = 0; side < 2; side++) {
    if (!clamped[side]) continue;
    INTERNAL::LogImpl().emit(E_WARNING, std::string("patcher: .router ") + kSides[side] +
                                            " count " + IntText(requested[side]) + " is outside " +
                                            IntText(MIN_PORTS) + "-" + IntText(MAX_PORTS) +
                                            "; clamped to " + IntText(counts[side]));
  }

  inletCount = counts[0];
  outletCount = counts[1];

  inputs.clear();
  outputs.clear();

  // Inlet 0 is the object's active one, as it is everywhere else in the
  // patcher. Here that coincides with Max's leftmost inlet being the one that
  // routes nothing: it takes the connection messages, and a list is the only
  // thing it accepts, since bang, int and float have no meaning on it and a
  // handler that swallowed them could not say so.
  ADD_IN_0;
  REG_LIST_IN(SetList);
  inputs.back().SetDoc("connections", kControlDoc, "list");

  for (int i = 0; i < inletCount; i++) {
    // Labelled by the number the connection messages use, which counts these
    // inlets from 0 and not the physical inlet they sit at.
    inputs.emplace_back(this, false, i + 1);
    REG_BANG_IN(SetBang);
    REG_INT_IN(SetInt);
    REG_FLOAT_IN(SetFloat);
    REG_LIST_IN(SetList);
    inputs.back().SetDoc(InletLabel(i), kDataInletDoc, "any");
  }

  for (int i = 0; i < outletCount; i++) {
    // ANY, because the object forwards whichever of bang, int, float and list
    // arrived rather than producing a kind of its own.
    ADD_OUT_ANY;
    outputs.back().SetDoc(OutletLabel(i), kDataOutletDoc, "any");
  }

  // Max's right outlet, and it always carries a list — "a series of single line
  // lists in the form inlet-number outlet-number state" — so it is typed as one.
  ADD_OUT_LIST;
  outputs.back().SetDoc("dump", kDumpOutletDoc, "list");

  // Rebuilt with the ports so the two can never disagree on how many there are;
  // a re-parse deliberately forgets the routing, since the ports it described
  // no longer exist.
  matrix.clear();
  matrix.resize((std::size_t)inletCount * (std::size_t)outletCount, 0);

  // The one allocation a dump line would otherwise need.
  dumpText.reserve(TEXT_CAPACITY);
}

PARM_CLEAR() {
  // Runs on the control thread before the parameter string is re-read, and is
  // the whole of `SetParams("")`: Parameters::Set returns without calling the
  // parse callback for an empty argument, so this has to leave Max's
  // no-argument object behind rather than one still holding the previous
  // counts.
  creationArgs.clear();
  ShapePorts();
}

PARM_PARSE() {
  ShapePorts();
}

bool gRouter::Connected(int inlet, int outlet) const {
  if (inlet < 0 || inlet >= inletCount) return false;
  if (outlet < 0 || outlet >= outletCount) return false;
  return matrix[((std::size_t)inlet * (std::size_t)outletCount) + (std::size_t)outlet] != 0;
}

int gRouter::ConnectionCount() const {
  int count = 0;
  for (unsigned char cell : matrix) {
    if (cell != 0) count++;
  }
  return count;
}

int gRouter::SnapshotRow(int inlet, unsigned char* row) const {
  if (inlet < 0 || inlet >= inletCount) return 0;

  // ShapePorts() clamps outletCount to MAX_PORTS, which is what makes `row` big
  // enough. Said again here so the bound on the write is a local fact: the
  // caller reads back exactly as many entries as this returns, and neither the
  // compiler nor the analyzer should have to carry an invariant across two
  // functions to see that every one of them was written.
  const int count = (outletCount > MAX_PORTS) ? MAX_PORTS : outletCount;

  const std::size_t base = (std::size_t)inlet * (std::size_t)outletCount;
  for (int i = 0; i < count; i++)
    row[i] = matrix[base + (std::size_t)i];
  return count;
}

void gRouter::SetConnection(int inlet, int outlet, bool on) {
  // A cell the object does not have is not an error to report, it is a message
  // with nothing to address — .gate's, .spray's and .decode's reading of an
  // index out of range.
  if (inlet < 0 || inlet >= inletCount) return;
  if (outlet < 0 || outlet >= outletCount) return;
  matrix[((std::size_t)inlet * (std::size_t)outletCount) + (std::size_t)outlet] = on ? 1 : 0;
}

void gRouter::Patch(int inlet, int outlet) {
  if (inlet < 0 || inlet >= inletCount) return;
  if (outlet < 0 || outlet >= outletCount) return;

  // A whole column in one pass: "connects an inlet to an outlet and disconnects
  // all other inlets that are currently connected to that outlet". Done here
  // rather than as a clear followed by a connect so the outlet is never
  // momentarily sourceless — the sends that happen in between would otherwise
  // fall through a hole the patch never asked for.
  for (int i = 0; i < inletCount; i++)
    matrix[((std::size_t)i * (std::size_t)outletCount) + (std::size_t)outlet] =
        (i == inlet) ? 1 : 0;
}

void gRouter::Clear() {
  for (unsigned char& cell : matrix)
    cell = 0;
}

void gRouter::AppendInt(int value) {
  char digits[FORMAT_INT_WIDTH];
  const std::size_t written = WriteInt(value, digits);
  dumpText.append(digits, written);
}

void gRouter::Dump(YSE::THREAD thread) {
  // The dump outlet is an ordinary outlet, so a patch can wire it back to the
  // control inlet in one cord. Without this a dump line arriving there would
  // start a second full dump inside the first, and each of *its* lines a third:
  // the send-depth guard bounds the depth of that, but the breadth is
  // inletCount * outletCount per level and multiplies.
  if (dumping) return;
  dumping = true;

  const std::size_t dumpOutlet = (std::size_t)outletCount;

  for (int in = 0; in < inletCount; in++) {
    for (int out = 0; out < outletCount; out++) {
      // Refilled per line into memory reserved at construction, so a dump of
      // any size touches no allocator. Read live rather than from a snapshot:
      // the whole matrix can be 65536 cells, which is not a thing to copy on a
      // message path, and a re-entrant connection change during a dump is a
      // patch describing a matrix it is simultaneously editing.
      dumpText.clear();
      AppendInt(in);
      dumpText.push_back(' ');
      AppendInt(out);
      dumpText.push_back(' ');
      AppendInt(Connected(in, out) ? 1 : 0);
      outputs[dumpOutlet].SendList(dumpText, thread);
    }
  }

  dumping = false;
}

BANG_IN(SetBang) {
  // Max: "bang received in any but the leftmost inlet will be sent to all
  // outlets that are connected to that inlet." The control inlet does not
  // register this handler at all, so `inlet` is always a routable one here.
  unsigned char row[MAX_PORTS] = {};
  const int count = SnapshotRow(inlet - 1, row);
  for (int i = count - 1; i >= 0; i--) {
    if (row[i]) outputs[(std::size_t)i].SendBang(thread);
  }
}

INT_IN(SetInt) {
  unsigned char row[MAX_PORTS] = {};
  const int count = SnapshotRow(inlet - 1, row);
  // Max's universal right-to-left order, so a downstream collector sees the
  // last outlet before outlet 0.
  for (int i = count - 1; i >= 0; i--) {
    if (row[i]) outputs[(std::size_t)i].SendInt(value, thread);
  }
}

FLOAT_IN(SetFloat) {
  unsigned char row[MAX_PORTS] = {};
  const int count = SnapshotRow(inlet - 1, row);
  for (int i = count - 1; i >= 0; i--) {
    if (row[i]) outputs[(std::size_t)i].SendFloat(value, thread);
  }
}

LIST_IN(SetList) {
  if (inlet != 0) {
    // Max: "Any Max message received in any but the leftmost inlet will be sent
    // to all outlets that are connected to that inlet." Forwarded by text and
    // untouched — the message words below are methods of the *control* inlet
    // only, so `connect 0 1` arriving here is data and not a method call, which
    // is what makes the crossbar transparent to whatever passes through it.
    unsigned char row[MAX_PORTS] = {};
    const int count = SnapshotRow(inlet - 1, row);
    for (int i = count - 1; i >= 0; i--) {
      if (row[i]) outputs[(std::size_t)i].SendList(value, thread);
    }
    return;
  }

  std::size_t at = 0;
  int args[3] = {0, 0, 0};

  // Checked before `connect`, since "disconnect" does not begin with "connect"
  // but the pair is easy to get the wrong way round with a prefix test.
  if (MatchWord(value, "disconnect", 10, at)) {
    // Max: "The word disconnect, followed by two numbers that specify inlet and
    // outlet numbers, disconnects an inlet from an outlet."
    if (ReadExactInts(value, at, args, 2)) SetConnection(args[0], args[1], false);
    return;
  }

  if (MatchWord(value, "connect", 7, at)) {
    // Max: "connects an inlet to an outlet. Multiple inlets can be connected to
    // multiple outlets, and vice versa."
    if (ReadExactInts(value, at, args, 2)) SetConnection(args[0], args[1], true);
    return;
  }

  if (MatchWord(value, "patch", 5, at)) {
    // Max: "connects an inlet to an outlet and disconnects all other inlets
    // that are currently connected to that outlet."
    if (ReadExactInts(value, at, args, 2)) Patch(args[0], args[1]);
    return;
  }

  if (IsWordMessage(value, "clear", 5)) {
    // Max: "Clears the state of the switching matrix, All inlets are
    // disconnected from all outlets."
    Clear();
    return;
  }

  if (IsWordMessage(value, "dump", 4)) {
    Dump(thread);
    return;
  }

  if (IsWordMessage(value, "print", 5)) {
    // Max prints to the Max Console. This patcher is headless and has none, and
    // the engine's log is not a substitute: no patcher object logs from a
    // message handler, because a handler runs on whichever thread sent the
    // message. Accepted and silent rather than treated as data — a patch that
    // sends it means a method call, and `dump` is the form it can read.
    return;
  }

  // Max: "A list of three numbers received in the left inlet is interpreted as
  // specifying an inlet number, an outlet number, and a 0 or 1 specifying the
  // state of a connection." Exactly three — a shorter list names no cell, and a
  // longer one is not the message either. Any non-zero state connects, the
  // reading .decode takes of its own on/off inlets, since a state of 2 meaning
  // "leave it alone" would surprise every patch that computed it.
  if (ReadExactInts(value, 0, args, 3)) SetConnection(args[0], args[1], args[2] != 0);
}
