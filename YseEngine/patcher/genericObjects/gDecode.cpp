#include "gDecode.h"
#include "../../implementations/logImplementation.h"
#include "../math/gExprEval.h"
#include "../pListArgs.h"
#include "../pObjectList.hpp"
#include <cstddef>
#include <string>

using namespace YSE::PATCHER;

#define className gDecode

namespace {

  // An int as text, through the patcher's one int formatter rather than
  // std::to_string. Control-thread only (the construction-time clamp log).
  std::string IntText(int value) {
    char digits[YSE::PATCHER::FORMAT_INT_WIDTH];
    const std::size_t written = YSE::PATCHER::WriteInt(value, digits);
    return std::string(digits, written);
  }

  constexpr char kSelectDoc[] =
      "An int selects the outlet that carries 1; every other outlet carries 0, and the whole state "
      "is sent on each message, right to left. An index the object has no outlet for is ignored "
      "and leaves the selection alone. A bang re-sends the current state without changing it. The "
      "left outlet is selected before anything arrives.";

  constexpr char kSecondaryDoc[] =
      "Max's 'all on' override, one step above the selection. An int greater than 0 puts 1 on "
      "every outlet; 0 or less hands control back to the left inlet's selection. Overridden in "
      "turn by the right inlet, so while that holds a non-zero number anything sent here sends 0 "
      "out every outlet.";

  constexpr char kPrimaryDoc[] =
      "Max's master mute, the top of the hierarchy. A non-zero int puts 0 on every outlet whatever "
      "the other two inlets say; 0 restores the state the middle and left inlets describe, so the "
      "mute is non-destructive and the selection underneath it survives.";

  constexpr char kOutletDoc[] =
      "1 when this outlet is the selected one (or the middle inlet has turned the whole bank on), "
      "0 otherwise. Every outlet fires on every accepted message, in Max's order: the last outlet "
      "first, outlet 0 last.";

} // namespace

CONSTRUCT() {
  // Every outlet is built by ShapePorts(), so a saved `.decode 4` comes back
  // with four of them. The clear callback is what makes `SetParams("")` return
  // the object to Max's no-argument shape rather than leaving the previous
  // outlet count in place.
  REG_PARM_CLEAR;
  REG_PARM_PARSE;

  ADD_PARAM(creationArgs);

  // Three inlets, as Max has, and a fixed set: only the *outlet* count is a
  // creation argument. Max documents `bang` and `int` for this object and
  // nothing else, so `float`, `list` and `anything` are declined outright
  // rather than swallowed by a handler that cannot log — .spray's reading of
  // the same situation, and it leaves GetAcceptedTypes reporting the real
  // contract.
  ADD_IN_0;
  REG_BANG_IN(SetBang);
  REG_INT_IN(SetInt);
  inputs.back().SetDoc("select", kSelectDoc, "0 to outlets-1");

  ADD_IN_1;
  REG_INT_IN(SetInt);
  inputs.back().SetDoc("secondary", kSecondaryDoc, "int");

  ADD_IN_2;
  REG_INT_IN(SetInt);
  inputs.back().SetDoc("primary", kPrimaryDoc, "int");

  // Max: "Sets the number of outlets. The default is one outlet." Also the
  // shape ClearParams() restores.
  ShapePorts();

  ADD_DESCRIPTION(
      "Sends 1 out a selected outlet and 0 out every other one — Max's decode, which 'provides "
      "hierarchical switching' where 'the right inlet turns all outlets off, while the middle "
      "inlet turns all outlets on. The right inlet overrides the middle inlet, and the middle "
      "inlet overrides numbers sent to the left inlet that turn individual outlets on or off'. The "
      "one-hot decoder: an index goes in and a set of states comes out, which is what drives a "
      "bank "
      "of gates, a row of indicators or any group of things of which exactly one is meant to be "
      "live — the mutual exclusion is the object rather than something the patch maintains, where "
      "the alternative is a .sel or a .== in front of every member of the bank with the constants "
      "kept in step by hand. It is not .gate, which routes a value to one outlet and says nothing "
      "on the others: this emits a state on every outlet and the values are its own rather than "
      "anything the patch sent, so a .gate fed 60 puts 60 somewhere while a .decode fed 2 puts 1 "
      "on "
      "outlet 2 and 0 everywhere else. The two compose — a .decode into the select inlets of a "
      "bank "
      "of gates is exactly the hierarchical switch Max names. It is not .sel either, which bangs "
      "on "
      "a match and therefore reports the arrival of a state but never its departure; turning the "
      "previously selected outlet off is the whole difference and the reason the object exists. "
      "And unlike .spray or .cycle, which each send one message out one numbered outlet, one "
      "message here reaches all of them. The three inlets are state rather than events: each "
      "remembers the last number it was given and every message re-resolves the whole set from all "
      "three, which is what makes the overrides non-destructive — a 1 and then a 0 in the right "
      "inlet leave the bank exactly as it was, without the patch re-sending the index. The right "
      "inlet is Max's master mute, 'any positive number other than 0 sends a 0 out all outlets' "
      "and 0 restores what the other two describe, with non-zero taken as off whichever sign it "
      "has since -1 meaning the opposite of 1 in an on/off inlet would surprise every patch. The "
      "middle inlet is the 'all on' override, strictly 'a number greater than 0' as Max words it, "
      "with 0 or less falling through to the selection. The left inlet is the selection itself, "
      "'an "
      "index (starting with 0 for the left outlet) that specifies an outlet out to turn on, "
      "turning "
      "off all other outlets'. Each accepted message sends the complete state, one int out every "
      "outlet from the last to outlet 0 in Max's universal order, which is the reading every "
      "sentence of the reference takes and what makes the object idempotent: a downstream bank "
      "always agrees with the decoder whatever it missed earlier. The three values are snapshotted "
      "before the first send, the settle-first discipline .onebang, .next, .buddy, .cycle, .bucket "
      "and .spray share, so a patch looping an outlet back into an inlet re-enters inside the Send "
      "and the interrupted burst goes on emitting the state it started with. The left outlet is "
      "initially enabled, Max's words, so a bang before anything else arrives reports 1 on outlet "
      "0 "
      "rather than a bank in no state at all; an index the object has no outlet for is ignored and "
      "leaves the selection alone, .gate's, .cycle's and .spray's discipline, since wrapping it or "
      "blanking the bank would hide a miscount at the object best placed to expose it. bang "
      "'causes "
      "decode to output its current state' and is scoped to the left inlet, where the reference "
      "puts it. float, list and anything are not accepted on any inlet: Max documents bang and int "
      "and nothing more. At most 256 outlets, built once before the object is published; "
      "Calculate() does nothing, and no message path allocates, locks or blocks — a burst is three "
      "ints on the stack and one integer compare per outlet.");
  ADD_CATEGORY(pCategory::GENERIC);
  PARAM_DOC("outlets", "1",
            "Max's one creation argument: how many outlets to build, clamped to 1-256 and "
            "defaulting to 1. A float is truncated, as Max's own argument list documents; anything "
            "that is not a whole finite number is ignored and leaves the default in place.",
            "1-256");
}

void gDecode::ShapePorts() {
  // Rebuilt rather than resized: the outlet *count* comes from the argument.
  // Safe because every caller runs before the object is wired or published —
  // the constructor, and the two parameter callbacks, which
  // patcherImplementation::CreateObjectUnlocked runs before AssignGraphIds. A
  // *live* SetParams never reaches here on a published object: registering the
  // callbacks makes ParamsNeedRebuild() true, so #234 replaces the object.
  int count = DEFAULT_PORTS;
  int requested = DEFAULT_PORTS;
  bool clamped = false;

  for (const std::string& token : creationArgs) {
    // Parameters::Set splits on single spaces, so a run of them yields empty
    // tokens; an empty argument is not a number.
    if (token.empty()) continue;

    float number = 0.f;
    // Strict on purpose, as the rest of the family is: ExprParseFloatList would
    // read `5abc` as 5 and fold `1e999` to 0, and neither answers "is this
    // creation argument a number at all".
    if (!ReadNumericToken(token, number)) continue;

    // Max's argument list gives a float variant "converted to int".
    requested = ExprToInt(number);
    count = requested;
    if (count > MAX_PORTS) count = MAX_PORTS;
    if (count < MIN_PORTS) count = MIN_PORTS;
    clamped = (requested != count);
    break;
  }

  // The control thread, before the object is wired or published, so this is the
  // one place a clamp can be *said* rather than merely made observable through
  // OutletCount().
  if (clamped) {
    INTERNAL::LogImpl().emit(E_WARNING, "patcher: .decode outlet count " + IntText(requested) +
                                            " is outside " + IntText(MIN_PORTS) + "-" +
                                            IntText(MAX_PORTS) + "; clamped to " + IntText(count));
  }

  outputs.clear();
  for (int i = 0; i < count; i++) {
    // INT rather than ANY: every outlet carries a 1 or a 0 the object computed,
    // never a value it is forwarding.
    ADD_OUT_INT;
    outputs.back().SetDoc(OutletLabel(i), kOutletDoc, "0 or 1");
  }

  // The selection has to name an outlet that exists, or a `.decode 8` reduced
  // to `.decode 2` by a re-parse would keep pointing past its own bank. Max's
  // "the left outlet is initially enabled" is the shape to fall back to.
  if (index >= count) index = 0;
}

PARM_CLEAR() {
  // Runs on the control thread before the parameter string is re-read, and is
  // the whole of `SetParams("")`: Parameters::Set returns without calling the
  // parse callback for an empty argument, so this has to leave Max's
  // no-argument object behind rather than one still holding the previous outlet
  // count.
  creationArgs.clear();
  ShapePorts();
}

PARM_PARSE() {
  ShapePorts();
}

bool gDecode::StateAt(int outlet) const {
  if (outlet < 0 || outlet >= (int)outputs.size()) return false;
  return Resolve(outlet, primary, secondary, index);
}

void gDecode::Emit(YSE::THREAD thread) {
  const int count = (int)outputs.size();

  // Snapshotted before the first send, not read per outlet: the send path is
  // synchronous, so a patch looping an outlet back into an inlet arrives here
  // again inside the SendInt below and would otherwise leave this burst
  // emitting half of one state and half of another.
  const int selected = index;
  const int secondaryNow = secondary;
  const int primaryNow = primary;

  // Max's universal right-to-left order, so a downstream collector sees the
  // last outlet before outlet 0.
  for (int outlet = count - 1; outlet >= 0; outlet--) {
    outputs[(std::size_t)outlet].SendInt(
        Resolve(outlet, primaryNow, secondaryNow, selected) ? 1 : 0, thread);
  }
}

BANG_IN(SetBang) {
  // Max: "The message bang causes decode to output its current state." Scoped
  // to the left inlet, where the reference puts it. A poll — it changes
  // nothing.
  if (inlet != 0) return;
  Emit(thread);
}

INT_IN(SetInt) {
  if (inlet == 0) {
    // Max: "An index (starting with 0 for the left outlet) that specifies an
    // outlet out to turn on, turning off all other outlets." An index with no
    // outlet is ignored outright — .gate's, .cycle's and .spray's discipline,
    // since wrapping it or blanking the bank would hide a miscount in the patch.
    if (value < 0 || value >= (int)outputs.size()) return;
    index = value;
    Emit(thread);
    return;
  }

  if (inlet == 1) {
    // Max's "all on" override: "a number greater than 0 received in the middle
    // inlet sends a 1 out all outlets. If 0 is received in the middle inlet,
    // decode sends a 1 out the last outlet decoded by a number received in the
    // left inlet, and 0 out all other outlets."
    secondary = value;
    Emit(thread);
    return;
  }

  if (inlet == 2) {
    // Max's master mute: "Any positive number other than 0 sends a 0 out all
    // outlets. When decode receives a 0 in its right inlet, it outputs 0 or 1
    // out its outlets based on the values last received in the middle and left
    // inlets."
    primary = value;
    Emit(thread);
  }
}
