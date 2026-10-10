#include "gBuddy.h"
#include "../../implementations/logImplementation.h"
#include "../math/gExprEval.h"
#include "../pListArgs.h"
#include "../pObjectList.hpp"
#include <cstddef>
#include <string>

using namespace YSE::PATCHER;

#define className gBuddy

namespace {

  // An int as text, through the patcher's one int formatter rather than
  // std::to_string. Control-thread only (the construction-time clamp log).
  std::string IntText(int value) {
    char digits[YSE::PATCHER::FORMAT_INT_WIDTH];
    const std::size_t written = YSE::PATCHER::WriteInt(value, digits);
    return std::string(digits, written);
  }

  constexpr char kInletDoc[] =
      "Stores a value here; once every inlet holds one, all are sent. A bang stores 0. On inlet "
      "0, 'clear' empties every inlet.";

  constexpr char kOutletDoc[] = "What the matching inlet received, sent once the set is complete.";

} // namespace

CONSTRUCT() {
  // Every port is built by ShapePorts(), so a saved `.buddy 5` comes back with
  // five inlets and five outlets. The clear callback is what makes
  // `SetParams("")` return the object to Max's no-argument shape rather than
  // leaving the previous port count in place.
  REG_PARM_CLEAR;
  REG_PARM_PARSE;

  ADD_PARAM(creationArgs);

  // Max: "If there is no argument, there are two inlets and two outlets." Also
  // the shape ClearParams() restores.
  ShapePorts();

  ADD_DESCRIPTION(
      "Waits until every inlet has received a value, then sends them all at once, right to left, "
      "and starts waiting again. Use it to assemble one event from parts that arrive separately.");
  ADD_CATEGORY(pCategory::CONTROL);
  PARAM_DOC("inlets", "2", "Number of inlet/outlet pairs, clamped to 1-256. A float is truncated.",
            "1-256");
}

void gBuddy::ShapePorts() {
  // Rebuilt rather than resized: the port *count* comes from the argument, so
  // both sides and both slot tables have to agree. Safe because every caller
  // runs before the object is wired or published — the constructor, and the two
  // parameter callbacks, which patcherImplementation::CreateObjectUnlocked runs
  // before AssignGraphIds. A *live* SetParams never reaches here on a published
  // object: registering the callbacks makes ParamsNeedRebuild() true, so #234
  // replaces the object instead.
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
    // creation argument a number at all". Max has one argument, so the first
    // number wins and the rest are ignored.
    if (!ReadNumericToken(token, number)) continue;

    requested = ExprToInt(number);
    count = requested;
    if (count > MAX_PORTS) count = MAX_PORTS;
    if (count < MIN_PORTS) count = MIN_PORTS;
    clamped = (requested != count);
    break;
  }

  // The control thread, before the object is wired or published, so this is the
  // one place a clamp can be *said* rather than merely made observable through
  // PortCount().
  if (clamped) {
    INTERNAL::LogImpl().emit(E_WARNING, "patcher: .buddy inlet count " + IntText(requested) +
                                            " is outside " + IntText(MIN_PORTS) + "-" +
                                            IntText(MAX_PORTS) + "; clamped to " + IntText(count));
  }

  inputs.clear();
  outputs.clear();

  for (int i = 0; i < count; i++) {
    // Inlet 0 is the object's active one, as it is everywhere else in the
    // patcher; the rest are ordinary control inlets. That is a DSP-readiness
    // distinction, not a hot/cold one — every inlet here takes part in the
    // rendezvous.
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

  // Rebuilt with the ports so the three can never disagree on how many there
  // are; a re-parse deliberately forgets the part-assembled set, since the slots
  // it belonged to no longer exist.
  slots.clear();
  slots.resize((std::size_t)count);
  outgoing.clear();
  outgoing.resize((std::size_t)count);
  filled = 0;

  // The one allocation a store or a release would otherwise need. Reserved
  // after the final resize, so no vector reallocation can move these strings
  // and lose it. Both tables, because Release() swaps their buffers: a slot
  // that has just released is holding what used to be an `outgoing` buffer.
  for (Slot& slot : slots)
    slot.text.reserve(TEXT_CAPACITY);
  for (Slot& slot : outgoing)
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

gBuddy::Held gBuddy::HeldKind(int index) const {
  if (index < 0 || index >= (int)slots.size()) return Held::NONE;
  return slots[(std::size_t)index].held;
}

int gBuddy::HeldInt(int index) const {
  if (index < 0 || index >= (int)slots.size()) return 0;
  const Slot& slot = slots[(std::size_t)index];
  return slot.held == Held::INT ? slot.intValue : 0;
}

float gBuddy::HeldFloat(int index) const {
  if (index < 0 || index >= (int)slots.size()) return 0.f;
  const Slot& slot = slots[(std::size_t)index];
  return slot.held == Held::FLOAT ? slot.floatValue : 0.f;
}

std::string gBuddy::HeldText(int index) const {
  if (index < 0 || index >= (int)slots.size()) return std::string();
  const Slot& slot = slots[(std::size_t)index];
  return slot.held == Held::LIST ? slot.text : std::string();
}

void gBuddy::Arrive(int inlet, Held kind, int intValue, float floatValue, const std::string* text,
                    YSE::THREAD thread) {
  if (inlet < 0 || inlet >= (int)slots.size()) return;

  Slot& slot = slots[(std::size_t)inlet];

  // Max stores one value per location, so a second arrival replaces the first
  // and only the transition out of NONE counts towards the rendezvous. Keeping
  // the tally incrementally is what makes a message cost one compare rather
  // than a walk over 256 slots.
  if (slot.held == Held::NONE) filled++;

  slot.held = kind;
  slot.intValue = intValue;
  slot.floatValue = floatValue;
  // Into the buffer reserved when the ports were shaped: no allocation for text
  // up to TEXT_CAPACITY, and one for anything longer.
  if (text != nullptr) slot.text.assign(*text);

  // Max: "When data has been received in all its inlets, buddy sends the
  // received messages out their corresponding outlets, then waits until data
  // has arrived again in all inlets."
  if (filled == (int)slots.size()) Release(thread);
}

void gBuddy::Release(YSE::THREAD thread) {
  const std::size_t count = slots.size();

  // Hand the set over and empty the object *first*. The send path is
  // synchronous and re-entrant, so a patch looping an outlet back into an inlet
  // arrives here again inside the Send below and must find an object that is
  // already waiting for the next round; with the emptying after the send it
  // would find a complete set and release again, and again.
  //
  // The text moves by swap rather than by assignment: both buffers are already
  // reserved, so exchanging them cannot allocate however long the text is, and
  // the buffer that lands back in the slot is just as reserved as the one that
  // left. Nothing reads a NONE slot's text, so the stale contents are harmless.
  for (std::size_t i = 0; i < count; i++) {
    Slot& from = slots[i];
    Slot& to = outgoing[i];
    to.held = from.held;
    to.intValue = from.intValue;
    to.floatValue = from.floatValue;
    to.text.swap(from.text);
    from.held = Held::NONE;
  }
  filled = 0;

  // Max: "it is sent out the outlets, in order from right to left." Each Send
  // returns only once the whole subgraph behind that outlet has run — see the
  // header on why that is a guarantee rather than a coincidence. Signed index
  // rather than a reverse iterator so the "n-1 down to 0" reads the way the
  // guarantee is stated. No allocation, no lock, no I/O on any branch.
  for (int i = (int)count - 1; i >= 0; i--) {
    const Slot& slot = outgoing[(std::size_t)i];
    switch (slot.held) {
    case Held::NONE:
      // Unreachable: a release only happens once every slot is filled, and a
      // filled slot is never NONE. Listed so the switch is exhaustive.
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

BANG_IN(SetBang) {
  // Max: "bang: In any inlet: Same as sending the number 0." Not a trigger and
  // not a query — a value, so the inlet counts as having received data and a
  // bang into the last empty inlet releases the set with int 0 on that outlet.
  Arrive(inlet, Held::INT, 0, 0.f, nullptr, thread);
}

INT_IN(SetInt) {
  Arrive(inlet, Held::INT, value, 0.f, nullptr, thread);
}

FLOAT_IN(SetFloat) {
  // "float: Performs the same function as int" — kept as a float rather than
  // truncated, since the slot releases whatever it was given.
  Arrive(inlet, Held::FLOAT, 0, value, nullptr, thread);
}

LIST_IN(SetList) {
  if (inlet == 0 && value == "clear") {
    // Max: "clear: In left inlet: Deletes all values stored in the inlets."
    // Emits nothing — the escape hatch for a rendezvous that will never
    // complete. Bare, as `.uzi`'s `pause` and `.change`'s `mode` are, so
    // `clear 1` is a list and a list is data. In any other inlet the word is an
    // ordinary symbol and falls through to be stored, which is Max's scoping
    // kept literally: those inlets are fed by other objects' outlets, and a
    // message that happened to read `clear` wiping the object from a data inlet
    // would be far worse than storing it as the symbol it is.
    for (Slot& slot : slots)
      slot.held = Held::NONE;
    filled = 0;
    return;
  }

  // Max gives `list` and `anything` the same sentence as `int` and `float`, and
  // documents no spreading here (unlike `bondo`), so the message occupies the
  // one inlet it arrived at and will leave the matching outlet verbatim.
  Arrive(inlet, Held::LIST, 0, 0.f, &value, thread);
}
