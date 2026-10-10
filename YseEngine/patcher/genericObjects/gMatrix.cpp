#include "gMatrix.h"
#include "../../implementations/logImplementation.h"
#include "../math/gExprEval.h"
#include "../pListArgs.h"
#include "../pObjectList.hpp"
#include <cstddef>
#include <string>

using namespace YSE::PATCHER;

#define className gMatrix

namespace {

  // An int as text, through the patcher's one int formatter rather than
  // std::to_string. Control-thread only (the construction-time clamp logs).
  std::string IntText(int value) {
    char digits[YSE::PATCHER::FORMAT_INT_WIDTH];
    const std::size_t written = YSE::PATCHER::WriteInt(value, digits);
    return std::string(digits, written);
  }

  // Step `cursor` past spaces and tabs.
  void SkipBlanks(const std::string& text, std::size_t& cursor) {
    while (cursor < text.size() && (text[cursor] == ' ' || text[cursor] == '\t'))
      cursor++;
  }

  // The next whitespace-delimited token at `cursor`, read as a finite number
  // and nothing else, with `cursor` left past it. False when there is no token
  // or when the token is not wholly a number.
  //
  // Strict on purpose, and strict in a way `ReadIntArgAt` is not: that reader
  // stops at the first character it cannot use, so it takes `0` out of `0.5`
  // and leaves the cursor mid-token. Right for an index, which Max documents as
  // an int; wrong for a gain, where `0.5` is the whole point. Same real-time
  // properties as the readers it is built from — no allocation, no exception,
  // and `ReadNumericToken`'s one bounded stack copy.
  bool ReadNumberToken(const std::string& text, std::size_t& cursor, float& out) {
    SkipBlanks(text, cursor);
    const std::size_t start = cursor;
    while (cursor < text.size() && text[cursor] != ' ' && text[cursor] != '\t')
      cursor++;
    if (cursor == start) return false;
    return YSE::PATCHER::ReadNumericToken(text.c_str() + start, cursor - start, out);
  }

  // True when nothing but whitespace remains at `cursor`.
  bool AtEnd(const std::string& text, std::size_t cursor) {
    SkipBlanks(text, cursor);
    return cursor == text.size();
  }

  // `<inlet> <outlet>` followed by an optional gain, and nothing after it.
  // `hasGain` reports whether the third item was there, which is what tells
  // `connect 0 1` from `connect 0 1 0.5`.
  //
  // The trailing check is what keeps a typo from looking like a working
  // connection: without it `connect 0 1 please` would quietly become
  // `connect 0 1` and take the default gain.
  bool ReadCell(const std::string& text, std::size_t at, int& inlet, int& outlet, float& gain,
                bool& hasGain) {
    std::size_t cursor = at;
    if (!YSE::PATCHER::ReadIntArgAt(text, cursor, inlet)) return false;
    if (!YSE::PATCHER::ReadIntArgAt(text, cursor, outlet)) return false;
    if (AtEnd(text, cursor)) {
      hasGain = false;
      return true;
    }
    if (!ReadNumberToken(text, cursor, gain)) return false;
    hasGain = true;
    return AtEnd(text, cursor);
  }

  // True when `text` is exactly the message word `word`, give or take trailing
  // whitespace.
  //
  // Exact where `.router` tolerates arguments, because the two objects are
  // documented differently: Max types `.router`'s `clear`/`dump`/`print` as
  // taking an `arguments [list]`, and types `.matrix`'s `clear` and
  // `dumpconnections` as taking nothing at all. So `clear all` is not a message
  // this object has, and is ignored like any other — which surfaces the mistake
  // at the object best placed to expose it.
  bool IsBareWord(const std::string& text, const char* word, std::size_t wordLength) {
    if (text.size() < wordLength) return false;
    if (text.compare(0, wordLength, word) != 0) return false;
    return AtEnd(text, wordLength);
  }

  // Read the whole of `text` as a list of numbers into `out`. False — with
  // nothing useful written — when any token is not a number, when there are no
  // tokens, or when there are more than `cap` of them.
  //
  // All-or-nothing on purpose. A partial answer would mean deciding what to do
  // with the tokens that are not numbers, and the useful answer to `note 60
  // 100` is to forward it exactly as it came: Max scales numbers, and its
  // `scalemode` attribute exists to *extend* that to messages that do not start
  // with one, off unless asked for.
  bool ReadNumberList(const std::string& text, float* out, int cap, int& count) {
    count = 0;
    std::size_t cursor = 0;
    for (;;) {
      SkipBlanks(text, cursor);
      if (cursor == text.size()) break;
      if (count >= cap) return false;
      if (!ReadNumberToken(text, cursor, out[count])) return false;
      count++;
    }
    return count > 0;
  }

  constexpr char kControlDoc[] =
      "Sets the connections: '<inlet> <outlet> <gain>' (gain 0 disconnects), 'connect <inlet> "
      "<outlet> [gain]', 'disconnect <inlet> <outlet>', 'clear' and 'dumpconnections'. Inlets "
      "and outlets count from 0, starting at the first data inlet.";

  constexpr char kDataInletDoc[] =
      "A message here goes out every outlet this inlet is connected to, right to left, scaled by "
      "each connection's gain.";

  constexpr char kDataOutletDoc[] =
      "Messages from every inlet connected to this outlet, each scaled by its connection's gain. "
      "Messages from different inlets are not added together.";

  constexpr char kDumpOutletDoc[] =
      "The answer to 'dumpconnections': one '<inlet> <outlet> <gain>' list per connection.";

} // namespace

CONSTRUCT() {
  // Every port is built by ShapePorts(), so a saved `.matrix 4 8 0.5` comes
  // back with four routable inlets, eight routable outlets and a default gain
  // of a half. The clear callback is what makes `SetParams("")` return the
  // object to Max's no-argument shape rather than leaving the previous counts
  // in place.
  REG_PARM_CLEAR;
  REG_PARM_PARSE;

  ADD_PARAM(creationArgs);

  // Both counts default to DEFAULT_PORTS and the gain to unity. Also the shape
  // ClearParams() restores.
  ShapePorts();

  ADD_DESCRIPTION(
      "Routes messages from its data inlets to its outlets as set by connection messages, like "
      ".router, but each connection has a gain that scales the numbers passing through. At a "
      "gain of 1 messages pass unchanged.");
  ADD_CATEGORY(pCategory::ROUTING);
  PARAM_DOC("inputs", "2",
            "How many data inlets, then how many data outlets (2 each by default), then the gain "
            "'connect' uses when none is given (1 by default).",
            "1-256, optional outputs 1-256, optional default gain");
}

void gMatrix::ShapePorts() {
  // Rebuilt rather than resized: both port *counts* come from the arguments, so
  // the two sides and the gain table have to agree. Safe because every caller
  // runs before the object is wired or published — the constructor, and the two
  // parameter callbacks, which patcherImplementation::CreateObjectUnlocked runs
  // before AssignGraphIds. A *live* SetParams never reaches here on a published
  // object: registering the callbacks makes ParamsNeedRebuild() true, so #234
  // replaces the object instead.
  int counts[2] = {DEFAULT_PORTS, DEFAULT_PORTS};
  int requested[2] = {DEFAULT_PORTS, DEFAULT_PORTS};
  bool clamped[2] = {false, false};
  int read = 0;

  defaultGain = DEFAULT_GAIN;

  for (const std::string& token : creationArgs) {
    if (read >= 3) break;

    // Parameters::Set splits on single spaces, so a run of them yields empty
    // tokens; an empty argument is not a number.
    if (token.empty()) continue;

    float number = 0.f;
    // Strict on purpose, as the rest of the family is: ExprParseFloatList would
    // read `5abc` as 5 and fold `1e999` to 0, and neither answers "is this
    // creation argument a number at all".
    if (!ReadNumericToken(token, number)) continue;

    if (read == 2) {
      // The gain is taken as it is written — it is a coefficient and not a
      // count, so there is nothing to clamp it to. Max says the same by giving
      // it no documented range.
      defaultGain = number;
      read++;
      continue;
    }

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
  static const char* const kSides[2] = {"input", "output"};
  for (int side = 0; side < 2; side++) {
    if (!clamped[side]) continue;
    INTERNAL::LogImpl().emit(E_WARNING, std::string("patcher: .matrix ") + kSides[side] +
                                            " count " + IntText(requested[side]) + " is outside " +
                                            IntText(MIN_PORTS) + "-" + IntText(MAX_PORTS) +
                                            "; clamped to " + IntText(counts[side]));
  }

  inletCount = counts[0];
  outletCount = counts[1];

  inputs.clear();
  outputs.clear();

  // Inlet 0 is the object's active one, as it is everywhere else in the
  // patcher. Here that coincides with Max's left inlet being the one that
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
    // arrived — scaled, but never turned into a kind of its own choosing.
    ADD_OUT_ANY;
    outputs.back().SetDoc(OutletLabel(i), kDataOutletDoc, "any");
  }

  // The dump outlet, and it always carries a list — one `<in> <out> <gain>` per
  // connected cell — so it is typed as one.
  ADD_OUT_LIST;
  outputs.back().SetDoc("connections", kDumpOutletDoc, "list");

  // Rebuilt with the ports so the two can never disagree on how many there are;
  // a re-parse deliberately forgets the routing, since the ports it described
  // no longer exist.
  gains.clear();
  gains.resize((std::size_t)inletCount * (std::size_t)outletCount, 0.f);

  // The two allocations a send would otherwise need. The scaled-list buffer is
  // sized for the longest MAX_LIST_VALUES values can print as, joined by single
  // spaces — `.vexpr`'s bound, for `.vexpr`'s reason.
  dumpText.reserve(DUMP_CAPACITY);
  listText.reserve((std::size_t)MAX_LIST_VALUES * (std::size_t)kExprValueTextMax);
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

float gMatrix::Gain(int inlet, int outlet) const {
  if (inlet < 0 || inlet >= inletCount) return 0.f;
  if (outlet < 0 || outlet >= outletCount) return 0.f;
  return gains[((std::size_t)inlet * (std::size_t)outletCount) + (std::size_t)outlet];
}

int gMatrix::ConnectionCount() const {
  int count = 0;
  for (float cell : gains) {
    if (cell != 0.f) count++;
  }
  return count;
}

int gMatrix::SnapshotRow(int inlet, float* row) const {
  if (inlet < 0 || inlet >= inletCount) return 0;

  // ShapePorts() clamps outletCount to MAX_PORTS, which is what makes `row` big
  // enough. Said again here so the bound on the write is a local fact: the
  // caller reads back exactly as many entries as this returns, and neither the
  // compiler nor the analyzer should have to carry an invariant across two
  // functions to see that every one of them was written.
  const int count = (outletCount > MAX_PORTS) ? MAX_PORTS : outletCount;

  const std::size_t base = (std::size_t)inlet * (std::size_t)outletCount;
  for (int i = 0; i < count; i++)
    row[i] = gains[base + (std::size_t)i];
  return count;
}

void gMatrix::SetGain(int inlet, int outlet, float gain) {
  // A cell the object does not have is not an error to report, it is a message
  // with nothing to address — .gate's, .spray's, .decode's and .router's
  // reading of an index out of range.
  if (inlet < 0 || inlet >= inletCount) return;
  if (outlet < 0 || outlet >= outletCount) return;
  gains[((std::size_t)inlet * (std::size_t)outletCount) + (std::size_t)outlet] = gain;
}

void gMatrix::Clear() {
  for (float& cell : gains)
    cell = 0.f;
}

void gMatrix::DumpConnections(YSE::THREAD thread) {
  // The dump outlet is an ordinary outlet, so a patch can wire it back to the
  // control inlet in one cord. Without this a dump line arriving there would
  // start a second full dump inside the first, and each of *its* lines a third:
  // the send-depth guard bounds the depth of that, but the breadth is the
  // connection count per level and multiplies.
  if (dumping) return;
  dumping = true;

  const std::size_t dumpOutlet = (std::size_t)outletCount;

  for (int in = 0; in < inletCount; in++) {
    for (int out = 0; out < outletCount; out++) {
      // "All current connections", so a disconnected cell is not a line. Read
      // live rather than from a snapshot: the whole table can be 65536 cells,
      // which is not a thing to copy on a message path, and a re-entrant change
      // during a dump is a patch describing a matrix it is simultaneously
      // editing.
      const float gain = Gain(in, out);
      if (gain == 0.f) continue;

      // Refilled per line into memory reserved at construction, so a dump of
      // any size touches no allocator. Filled immediately before its send, so
      // that a patch re-entering inside that send cannot have left the buffer
      // holding the inner message's text by the time this one is read.
      char digits[FORMAT_INT_WIDTH];
      std::size_t written = WriteInt(in, digits);
      dumpText.assign(digits, written);
      dumpText.push_back(' ');
      written = WriteInt(out, digits);
      dumpText.append(digits, written);
      dumpText.push_back(' ');

      char text[kExprValueTextMax];
      const int length = ExprFormatValue(ExprValue::Float(gain), text, kExprValueTextMax);
      dumpText.append(text, (std::size_t)length);

      outputs[dumpOutlet].SendList(dumpText, thread);
    }
  }

  dumping = false;
}

void gMatrix::SendScaled(int outlet, const std::string& text, const float* values, int count,
                         bool numeric, float gain, YSE::THREAD thread) {
  // Unity is the identity, and the identity has to be invisible: the text goes
  // out exactly as it came, which is what lets a .matrix at its default gain be
  // dropped in where a .router was. It is also the only branch a list that is
  // not all numbers can take.
  if (gain == 1.f || !numeric) {
    outputs[(std::size_t)outlet].SendList(text, thread);
    return;
  }

  // Rebuilt into memory reserved at construction, immediately before the send
  // for the same reason the dump line is.
  listText.clear();
  for (int i = 0; i < count; i++) {
    if (i != 0) listText.push_back(' ');
    char digits[kExprValueTextMax];
    const int length =
        ExprFormatValue(ExprValue::Float(values[i] * gain), digits, kExprValueTextMax);
    listText.append(digits, (std::size_t)length);
  }
  outputs[(std::size_t)outlet].SendList(listText, thread);
}

BANG_IN(SetBang) {
  // A bang carries no value, so a gain has nothing to act on: it is forwarded
  // as a bang wherever the row is connected. The control inlet does not
  // register this handler at all, so `inlet` is always a routable one here.
  float row[MAX_PORTS] = {};
  const int count = SnapshotRow(inlet - 1, row);
  // Max's universal right-to-left order, so a downstream collector sees the
  // last outlet before outlet 0.
  for (int i = count - 1; i >= 0; i--) {
    if (row[i] != 0.f) outputs[(std::size_t)i].SendBang(thread);
  }
}

INT_IN(SetInt) {
  float row[MAX_PORTS] = {};
  const int count = SnapshotRow(inlet - 1, row);
  for (int i = count - 1; i >= 0; i--) {
    if (row[i] == 0.f) continue;
    // An int at unity stays an int; at any other gain the product is a real
    // number, and rounding it back to an int would throw away the scaling
    // itself rather than merely its precision.
    if (row[i] == 1.f) {
      outputs[(std::size_t)i].SendInt(value, thread);
    } else {
      outputs[(std::size_t)i].SendFloat((float)value * row[i], thread);
    }
  }
}

FLOAT_IN(SetFloat) {
  float row[MAX_PORTS] = {};
  const int count = SnapshotRow(inlet - 1, row);
  for (int i = count - 1; i >= 0; i--) {
    if (row[i] == 0.f) continue;
    // No unity special case: a float is already the kind a scaled value leaves
    // as, and multiplying by exactly 1 returns the value bit for bit, so the
    // branch the int path needs has nothing to do here.
    outputs[(std::size_t)i].SendFloat(value * row[i], thread);
  }
}

LIST_IN(SetList) {
  if (inlet != 0) {
    // Max: "A list received in any other inlet is routed to any connected
    // outlets." The message words below are methods of the *control* inlet
    // only, so `connect 0 1` arriving here is data and not a method call, which
    // is what makes the crossbar transparent to whatever passes through it.
    float row[MAX_PORTS] = {};
    const int count = SnapshotRow(inlet - 1, row);

    // Parsed once for the whole fan-out rather than once per destination: a
    // 256-wide row would otherwise re-read the same characters 256 times, and
    // the answer cannot differ between outlets. A list that is not wholly
    // numbers, or is longer than the bounded buffer can hold, is forwarded
    // verbatim to every connected outlet.
    float values[MAX_LIST_VALUES];
    int values_count = 0;
    const bool numeric = ReadNumberList(value, values, MAX_LIST_VALUES, values_count);

    for (int i = count - 1; i >= 0; i--) {
      if (row[i] != 0.f) SendScaled(i, value, values, values_count, numeric, row[i], thread);
    }
    return;
  }

  std::size_t at = 0;
  int in = 0;
  int out = 0;
  float gain = 0.f;
  bool hasGain = false;

  // Checked before `connect`, since "disconnect" does not begin with "connect"
  // but the pair is easy to get the wrong way round with a prefix test.
  if (MatchWord(value, "disconnect", 10, at)) {
    // Max: "The word disconnect followed by an inlet index and outlet index
    // deletes a connection between the inlet and outlet if it exists." No gain
    // argument is documented, so one is not accepted — a gain on a disconnect
    // would have nothing to mean.
    if (ReadCell(value, at, in, out, gain, hasGain) && !hasGain) SetGain(in, out, 0.f);
    return;
  }

  if (MatchWord(value, "connect", 7, at)) {
    // Max: "The word connect followed by an inlet index, outlet index, and
    // optional gain, adds a connection between the inlet and outlet. If the
    // gain value is not present, the default gain ... will be used."
    if (ReadCell(value, at, in, out, gain, hasGain)) SetGain(in, out, hasGain ? gain : defaultGain);
    return;
  }

  if (IsBareWord(value, "dictionary", 10) || MatchWord(value, "dictionary", 10, at)) {
    // Max replaces every connection with the ones in a named dictionary object.
    // This patcher has no dictionaries and, being headless, is not going to
    // grow them. Accepted and silent rather than treated as data — a patch that
    // sends it means a method call — and `dumpconnections` is the form of the
    // same conversation a patch here can actually act on.
    return;
  }

  if (IsBareWord(value, "clear", 5)) {
    // Max: "The clear message removes all inlet - outlet connections."
    Clear();
    return;
  }

  if (IsBareWord(value, "dumpconnections", 15)) {
    DumpConnections(thread);
    return;
  }

  // Max: "A list either adds or removes a connection between an inlet and
  // outlet. The format of the message is input index ..., output index ...,
  // gain. A non-zero gain adds a connection with the designated gain; a gain of
  // 0 deletes the connection if it exists." All three are required here — two
  // numbers name a cell without saying what to do with it, and a bare
  // `connect` is the message for "use the default gain".
  if (ReadCell(value, 0, in, out, gain, hasGain) && hasGain) SetGain(in, out, gain);
}
