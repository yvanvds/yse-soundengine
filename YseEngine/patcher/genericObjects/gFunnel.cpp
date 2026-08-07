#include "gFunnel.h"
#include "../../implementations/logImplementation.h"
#include "../math/gExprEval.h"
#include "../pListArgs.h"
#include "../pObjectList.hpp"
#include <cstddef>
#include <string>

using namespace YSE::PATCHER;

#define className gFunnel

namespace {

  // An int as text, through the patcher's one int formatter rather than
  // std::to_string. Control-thread only (the construction-time clamp log).
  std::string IntText(int value) {
    char digits[YSE::PATCHER::FORMAT_INT_WIDTH];
    const std::size_t written = YSE::PATCHER::WriteInt(value, digits);
    return std::string(digits, written);
  }

  // An inlet index plus an offset, saturated to the `int` range. Both sides are
  // ints, so the sum need not be one; the addition is done in 64 bits and the
  // result pinned rather than allowed to wrap, since a wrapped tag would read
  // as a perfectly plausible inlet number to whatever consumes it.
  int SaturatingTag(int inlet, int offset) {
    const I64 sum = (I64)inlet + (I64)offset;
    if (sum > 0x7FFFFFFFLL) return 0x7FFFFFFF;
    if (sum < -0x80000000LL) return (int)(-0x80000000LL);
    return (int)sum;
  }

  constexpr char kInletDoc[] =
      "An int, float, list or symbol is sent out the one outlet with this inlet's number "
      "prepended, "
      "so a bank of sources merges onto one cord without losing which of them spoke. An int or a "
      "float is also stored here, and a bang re-sends the stored number tagged the same way — 0 "
      "before anything has arrived. A list is forwarded but not stored, since the stored value is "
      "a "
      "number and bang's output is a two-item list. In any inlet, 'set <n0> <n1> ...' writes the "
      "stored numbers of the inlets in order and sends nothing, and 'offset <n>' changes the "
      "number "
      "every inlet stamps.";

  constexpr char kOutletDoc[] =
      "A list whose first element is the number of the inlet the message arrived at, plus the "
      "offset, followed by the message itself. Values keep the spelling they arrived with — a "
      "float "
      "in a list is not converted to an int — and the tag is always an int.";

} // namespace

CONSTRUCT() {
  // Every inlet is built by ShapePorts(), so a saved `.funnel 4` comes back with
  // four of them. The clear callback is what makes `SetParams("")` return the
  // object to Max's no-argument shape rather than leaving the previous inlet
  // count in place.
  REG_PARM_CLEAR;
  REG_PARM_PARSE;

  ADD_PARAM(creationArgs);

  // Max: "If there is no argument there will be two inlets." Also the shape
  // ClearParams() restores.
  ShapePorts();

  // One outlet, and it always carries a list — "funnel outputs a list consisting
  // of the inlet number followed the input" — so it is typed as one rather than
  // as ANY.
  ADD_OUT_LIST;
  outputs.back().SetDoc("out", kOutletDoc, "list");

  ADD_DESCRIPTION(
      "Tags incoming data with the number of the inlet it arrived at and merges everything onto "
      "one "
      "outlet — Max's funnel, which 'outputs a list consisting of the inlet number followed the "
      "input' when 'a number or list is received in any inlet'. The many-to-one collector: a bank "
      "of controls, a row of sensors or eight voices reporting their state all reach one "
      "destination, and the destination still knows which of them spoke. Without it a patch either "
      "runs one cord per source the whole way or puts a box in front of every source to stamp a "
      "constant on it by hand, which is N boxes to maintain and N places to get the number wrong "
      "when a source is inserted; this is that stamp, once, with the number taken from the wiring "
      "itself. It is the exact inverse of .spray, which Max lists under See Also: .spray turns 2 "
      "60 "
      "into 60 leaving outlet 2, and this turns 60 arriving at inlet 2 into 2 60. The pairing is "
      "literal — a .funnel n into a .spray n with the same offset is an n-way bus carried over a "
      "single patch cord, entering at inlet i and leaving at outlet i, and the cord between them "
      "can cross a subpatch boundary or a .s/.r pair, which n separate cords could not do as "
      "cheaply. It is not .bondo or .buddy, which also gather several inlets but gather them into "
      "a "
      "set, with N outlets and a release that speaks about all of them at once; this has one "
      "outlet "
      "and speaks about exactly one arrival, so two values at two inlets make two output lists "
      "rather than one, and nothing here ever waits. It is not .switch either, which merges by "
      "selecting: there only the chosen inlet passes and what comes out carries no record of its "
      "origin, where here every inlet always passes and the record is the point. The objects that "
      "read the tag back are .route and .sel, which is why it is the first element of the list. "
      "The "
      "number prepended is the inlet index plus the offset — Max's second argument 'specifies an "
      "offset for the first inlet number' and the 'offset' message 'will offset the numbering of "
      "inlets by the number given', one value from two directions — and it adds where .spray's "
      "offset subtracts, which is precisely what makes the two mirror images at any offset. It "
      "exists because the numbers a downstream object expects usually start where it starts, MIDI "
      "channels at 1 or a table's rows at some base, and the alternative is a .+ on the far side "
      "of "
      "the merge that has to be kept in step by hand. 'offset' is run-time state rather than a "
      "parameter, as .spray's offset and .cycle's thresh are, so a saved patch carries the "
      "creation "
      "argument. Each inlet also remembers the last number it received and a bang replays it "
      "tagged, Max's 'the stored (most recently received) number in that inlet are sent out as a "
      "two-item list', which makes the object pollable as well as reactive — a patch can ask what "
      "source 3 is saying without source 3 speaking again. Because Max says the store is a number "
      "and the reply a two-item list, a list or an anything is tagged and forwarded but is not "
      "stored: replaying note 60 would send three items out of a method documented to send two. "
      "The "
      "store starts at 0, so a bang into an inlet nothing has reached is 0 rather than silence. "
      "Max "
      "'set followed by a list of numbers which correspond with the number of inlets' addresses "
      "every inlet at once, element k writing inlet k's store and sending nothing, which is how a "
      "patch primes the whole bank before anything has spoken; elements past the last inlet are "
      "dropped, a short list leaves the rest alone, and a non-number element spends its position "
      "without writing, .spray's discipline for a positional message. Both set and offset are "
      "matched in any inlet as Max scopes them, and a bare message word is the method with nothing "
      "to do rather than a symbol to forward. Values keep the spelling they arrived with — Max's "
      "'in a list floats are not converted to ints' — since a list is forwarded by text, character "
      "for character, while the tag itself is always an int. At most 256 inlets, built once before "
      "the object is published; Calculate() does nothing, and no message path allocates, locks or "
      "blocks.");
  ADD_CATEGORY(pCategory::GENERIC);
  PARAM_DOC("inlets", "2",
            "Max's argument list, in Max's order. The first whole number is how many inlets to "
            "build, clamped to 1-256 and defaulting to 2. The second is the offset added to the "
            "inlet numbering, so inlet 0 stamps that value; it defaults to 0 and is the starting "
            "value of the 'offset' message. A float is truncated; anything that is not a whole "
            "finite number is ignored and leaves the defaults in place.",
            "1-256, optional offset");
}

void gFunnel::ShapePorts() {
  // Rebuilt rather than resized: the inlet *count* comes from the argument, so
  // the ports and the store have to agree. Safe because every caller runs before
  // the object is wired or published — the constructor, and the two parameter
  // callbacks, which patcherImplementation::CreateObjectUnlocked runs before
  // AssignGraphIds. A *live* SetParams never reaches here on a published object:
  // registering the callbacks makes ParamsNeedRebuild() true, so #234 replaces
  // the object instead.
  int count = DEFAULT_PORTS;
  int requested = DEFAULT_PORTS;
  bool clamped = false;
  int read = 0;

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

    if (read == 0) {
      requested = ExprToInt(number);
      count = requested;
      if (count > MAX_PORTS) count = MAX_PORTS;
      if (count < MIN_PORTS) count = MIN_PORTS;
      clamped = (requested != count);
      read++;
      continue;
    }

    // Max: "The second argument specifies an offset for the first inlet number."
    // Not clamped — a patch may legitimately want tags in any range, and the
    // addition is saturated where it is done rather than here.
    offset = ExprToInt(number);
    break;
  }

  // The control thread, before the object is wired or published, so this is the
  // one place a clamp can be *said* rather than merely made observable through
  // InletCount().
  if (clamped) {
    INTERNAL::LogImpl().emit(E_WARNING, "patcher: .funnel inlet count " + IntText(requested) +
                                            " is outside " + IntText(MIN_PORTS) + "-" +
                                            IntText(MAX_PORTS) + "; clamped to " + IntText(count));
  }

  inputs.clear();

  for (int i = 0; i < count; i++) {
    // Inlet 0 is the object's active one, as it is everywhere else in the
    // patcher; the rest are ordinary control inlets. That is a DSP-readiness
    // distinction, not a hot/cold one — Max says "In any inlet" on every method
    // this object has, so every inlet here both tags and forwards.
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
  }

  // Rebuilt with the ports so the two can never disagree on how many there are;
  // a re-parse deliberately forgets the stored numbers, since the inlets they
  // belonged to no longer exist.
  slots.clear();
  slots.resize((std::size_t)count);

  // The one allocation a send would otherwise need.
  listText.reserve(TEXT_CAPACITY);
}

PARM_CLEAR() {
  // Runs on the control thread before the parameter string is re-read, and is
  // the whole of `SetParams("")`: Parameters::Set returns without calling the
  // parse callback for an empty argument, so this has to leave Max's
  // no-argument object behind rather than one still holding the previous inlet
  // count.
  creationArgs.clear();
  ShapePorts();
}

PARM_PARSE() {
  ShapePorts();
}

int gFunnel::TagFor(int inlet) const {
  return SaturatingTag(inlet, offset);
}

bool gFunnel::StoredIsFloat(int index) const {
  if (index < 0 || index >= (int)slots.size()) return false;
  return slots[(std::size_t)index].isFloat;
}

int gFunnel::StoredInt(int index) const {
  if (index < 0 || index >= (int)slots.size()) return 0;
  return slots[(std::size_t)index].intValue;
}

float gFunnel::StoredFloat(int index) const {
  if (index < 0 || index >= (int)slots.size()) return 0.f;
  return slots[(std::size_t)index].floatValue;
}

void gFunnel::Emit(int inlet, const char* text, std::size_t length, YSE::THREAD thread) {
  char digits[FORMAT_INT_WIDTH];
  const std::size_t written = WriteInt(TagFor(inlet), digits);

  // Refilled immediately before the send rather than kept between them: the send
  // path is synchronous, so a patch looping the outlet back into an inlet
  // re-enters here inside the SendList below, and a buffer filled any earlier
  // would be the inner message's by the time this one was read. Into memory
  // reserved at construction, so nothing here allocates for text up to
  // TEXT_CAPACITY.
  listText.assign(digits, written);
  listText.push_back(' ');
  listText.append(text, length);

  outputs[0].SendList(listText, thread);
}

void gFunnel::EmitStored(int inlet, YSE::THREAD thread) {
  const Slot& slot = slots[(std::size_t)inlet];

  char text[kExprValueTextMax];
  // Replayed as the kind it was stored as, so a bang after `1` gives `0 1` and
  // not `0 1.`. No allocation and no locale on either branch.
  const int length =
      slot.isFloat ? ExprFormatValue(ExprValue::Float(slot.floatValue), text, kExprValueTextMax)
                   : ExprFormatValue(ExprValue::Int(slot.intValue), text, kExprValueTextMax);
  Emit(inlet, text, (std::size_t)length, thread);
}

void gFunnel::Store(const std::string& text, std::size_t at) {
  const std::size_t size = text.size();
  const std::size_t count = slots.size();
  std::size_t i = at;
  std::size_t index = 0;

  // Max: "set followed by a list of numbers which correspond with the number of
  // inlets". Element k is inlet k's, so the walk stops at the last inlet and
  // anything past it is dropped.
  while (i < size && index < count) {
    while (i < size && (text[i] == ' ' || text[i] == '\t'))
      i++;
    if (i >= size) break;

    const std::size_t tokenStart = i;
    while (i < size && text[i] != ' ' && text[i] != '\t')
      i++;

    const char* token = text.c_str() + tokenStart;
    const std::size_t length = i - tokenStart;

    float number = 0.f;
    // A non-number spends its position and leaves that inlet's store alone,
    // rather than closing the gap: this is a positional message, and shifting
    // the rest would write every later value into the wrong inlet.
    if (ReadNumericToken(token, length, number)) {
      Slot& slot = slots[index];
      slot.isFloat = TokenLooksLikeFloat(token, length);
      slot.floatValue = number;
      slot.intValue = ExprToInt(number);
    }

    index++;
  }
}

BANG_IN(SetBang) {
  if (inlet < 0 || inlet >= (int)slots.size()) return;
  // Max: "The number of the inlet and the stored (most recently received) number
  // in that inlet are sent out as a two-item list." A poll, not a trigger for
  // anything else.
  EmitStored(inlet, thread);
}

INT_IN(SetInt) {
  if (inlet < 0 || inlet >= (int)slots.size()) return;

  Slot& slot = slots[(std::size_t)inlet];
  slot.isFloat = false;
  slot.intValue = value;
  slot.floatValue = (float)value;

  char text[kExprValueTextMax];
  const int length = ExprFormatValue(ExprValue::Int(value), text, kExprValueTextMax);
  Emit(inlet, text, (std::size_t)length, thread);
}

FLOAT_IN(SetFloat) {
  if (inlet < 0 || inlet >= (int)slots.size()) return;

  Slot& slot = slots[(std::size_t)inlet];
  // "float: Performs the same function as int" — kept as a float rather than
  // truncated, since what leaves is what arrived.
  slot.isFloat = true;
  slot.floatValue = value;
  slot.intValue = ExprToInt(value);

  char text[kExprValueTextMax];
  const int length = ExprFormatValue(ExprValue::Float(value), text, kExprValueTextMax);
  Emit(inlet, text, (std::size_t)length, thread);
}

LIST_IN(SetList) {
  if (inlet < 0 || inlet >= (int)slots.size()) return;

  std::size_t at = 0;

  if (MatchWord(value, "offset", 6, at)) {
    // Max: "The word offset followed by a number will offset the numbering of
    // inlets by the number given." The same value the second creation argument
    // sets. Run-time state: it does not write back to the parameters, so it does
    // not survive a save. A malformed argument is a method call that does
    // nothing rather than data to forward.
    int shift = 0;
    if (ReadIntArg(value, at, shift)) offset = shift;
    return;
  }

  if (MatchWord(value, "set", 3, at)) {
    // Max: "The word set followed by a list of numbers which correspond with the
    // number of inlets, will set the input list of numbers without sending them
    // through the outputs."
    Store(value, at);
    return;
  }

  // The bare word is the method with nothing to do. Forwarding it as the symbol
  // it also is would answer a method call with `0 set`, which no patch means.
  if (value == "offset" || value == "set") return;

  // Max: "The number of the inlet is prepended to the list, and the new list is
  // sent out", and `anything` "functions the same as list" — so a symbol-leading
  // message is tagged exactly as a numeric one is. Forwarded by text, which is
  // what makes "in a list floats are not converted to ints" true character for
  // character.
  const std::size_t size = value.size();
  std::size_t start = 0;
  while (start < size && (value[start] == ' ' || value[start] == '\t'))
    start++;
  std::size_t end = size;
  while (end > start && (value[end - 1] == ' ' || value[end - 1] == '\t'))
    end--;

  // Nothing to tag. Max has no message with no content, and `0` alone would be
  // a tag claiming an arrival that carried nothing.
  if (end == start) return;

  Emit(inlet, value.c_str() + start, end - start, thread);
}
