#include "gBucket.h"
#include "../../implementations/logImplementation.h"
#include "../math/gExprEval.h"
#include "../pListArgs.h"
#include "../pObjectList.hpp"
#include <cstddef>
#include <string>

using namespace YSE::PATCHER;

#define className gBucket

namespace {

  // An int as text, through the patcher's one int formatter rather than
  // std::to_string. Control-thread only (the construction-time clamp log).
  std::string IntText(int value) {
    char digits[YSE::PATCHER::FORMAT_INT_WIDTH];
    const std::size_t written = YSE::PATCHER::WriteInt(value, digits);
    return std::string(digits, written);
  }

  // Read the one number a `set` or a token carries, keeping the int/float
  // spelling so the register can hand it back out as the kind it arrived as.
  // No allocation, no locale beyond the strtof the family already accepts.
  bool ReadValueToken(const char* token, std::size_t length, float& value, bool& isFloat) {
    if (!YSE::PATCHER::ReadNumericToken(token, length, value)) return false;
    isFloat = YSE::PATCHER::TokenLooksLikeFloat(token, length);
    return true;
  }

  // The first whitespace-separated token of `text` at or after `offset`, as a
  // number. Used for `set <n>`, whose argument may be spelled either way.
  bool ReadValueArg(const std::string& text, std::size_t offset, float& value, bool& isFloat) {
    const std::size_t size = text.size();
    std::size_t i = offset;
    while (i < size && (text[i] == ' ' || text[i] == '\t'))
      i++;
    const std::size_t start = i;
    while (i < size && text[i] != ' ' && text[i] != '\t')
      i++;
    if (i == start) return false;
    return ReadValueToken(text.c_str() + start, i - start, value, isFloat);
  }

  constexpr char kInletDoc[] =
      "A number shifts the register one stage and every outlet fires, right to left. A bang sends "
      "the stored values without shifting. 'set <n>' fills every stage with n and sends it out "
      "every outlet; 'clear' resets every stage to 0 silently. 'freeze' suspends the output while "
      "incoming numbers go on shifting, 'thaw' resumes it. 'L2R' and 'R2L' choose which end new "
      "values enter. 'roll' feeds the value at the far end back in, rotating the register. A "
      "message whose first token is a number is fed in token by token; anything else is ignored.";

  constexpr char kOutletDoc[] =
      "The value this stage holds — outlet 0 the most recent, each outlet to its right one step "
      "older (reversed after 'R2L'). Every outlet fires on every input, in Max's order: the last "
      "outlet first, outlet 0 last. Each value leaves as the kind it arrived as.";

} // namespace

CONSTRUCT() {
  // Every outlet and every stage is built by ShapePorts(), so a saved
  // `.bucket 4` comes back with four of them. The clear callback is what makes
  // `SetParams("")` return the object to Max's no-argument shape rather than
  // leaving the previous outlet count in place.
  REG_PARM_CLEAR;
  REG_PARM_PARSE;

  ADD_PARAM(creationArgs);

  // One inlet, as Max has: every word message arrives here with the numbers.
  ADD_IN_0;
  REG_BANG_IN(SetBang);
  REG_INT_IN(SetInt);
  REG_FLOAT_IN(SetFloat);
  REG_LIST_IN(SetList);
  inputs.back().SetDoc("in", kInletDoc, "any");

  // Max: "If there is no argument, there will be one outlet." Also the shape
  // ClearParams() restores.
  ShapePorts();

  ADD_DESCRIPTION(
      "Passes numbers from outlet to outlet, shifting on every input — Max's bucket, which "
      "'outputs incoming values to outlets in bucket-brigade fashion' and 'acts as an n-stage "
      "shift register which can shift its contents from outlet to outlet in either direction'. A "
      "delay line for events rather than for samples: outlet 1 is whatever arrived one step ago, "
      "outlet 2 two steps ago, and so on for as many outlets as the creation argument asked for, "
      "which is the canonical shape of a canon, an arpeggio echo, a comparison of a value against "
      "its own recent history, and anything that has to reappear N steps later. It is not .past, "
      ".peak or .trough, which each remember one thing about the stream; this remembers the last N "
      "values positionally and hands all of them out at once. Nor is it .cycle, which it resembles "
      "from the outside and is the opposite of: .cycle sends one message out one outlet and moves "
      "a cursor, where this sends the whole register out every outlet on each input — 'spread "
      "these notes over four voices' is .cycle, 'play these notes again four steps later' is this. "
      "And unlike .trigger, which fans one value out every outlet as copies, every outlet here "
      "carries a different, older value: the fan-out is across time. By default the register is "
      "one step behind the input, Max's 'the numbers currently stored in bucket are sent out, then "
      "each number is moved one outlet to the right and the new number is stored to be sent out "
      "the left outlet the next time a number is received'; a second non-zero creation argument is "
      "Max's 'echo to output' mode, which swaps the order of the shift and the send so the value "
      "that arrived is in this burst rather than the next. 'L2R' and 'R2L' choose which end new "
      "values enter, and outlets always fire in Max's universal right-to-left order whichever way "
      "the storage is shifting, so a downstream collector sees the oldest value before the newest. "
      "The burst is captured before the state changes and the state is settled before the first "
      "send, the settle-first argument .onebang, .togedge, .next, .buddy and .cycle make: the send "
      "path is synchronous, so a patch looping an outlet back into the inlet re-enters inside the "
      "Send and must find a register that has already shifted, while the interrupted burst goes on "
      "emitting the values it started with. 'set <n>' fills every stage with n and sends it out "
      "every outlet — it emits, unlike .cycle's and .past's silent sets, because it changes the "
      "whole visible contents of the register rather than a cursor; 'clear' is the silent one and "
      "resets every stage to int 0. 'freeze' suspends the output while incoming numbers go on "
      "shifting internally and 'thaw' resumes it, so a frozen bucket records quietly and a thaw "
      "plus a bang shows what it collected; the gag covers bang, set and roll too. 'roll' feeds "
      "the value at the far end back in as if it had been received, a rotation in which nothing "
      "enters and nothing is lost, which turns the register into a phrase loop the patch can step; "
      "Max documents an argument that is not read, so a bare roll is the same message. Each stored "
      "value leaves as the kind it was spelled as, so 1 2 3 does not come back out as 1. 2. 3. A "
      "bang sends the stored values without shifting. Max documents no list method; here a message "
      "whose first token is a number is fed in token by token, each numeric token one full input, "
      "because a shift register is a thing you push a sequence through and dropping all but the "
      "first element would throw data away at the object least able to afford it — a one-element "
      "list still behaves exactly as Max's int. Anything else is ignored. At most 256 stages, "
      "built once before the object is published; Calculate() does nothing, and no message path "
      "allocates, locks or blocks.");
  ADD_CATEGORY(pCategory::GENERIC);
  PARAM_DOC("outlets", "1",
            "Max's argument list, in Max's order. The first whole number is how many outlets — and "
            "therefore stages — to build, clamped to 1-256 and defaulting to 1. A second whole "
            "number is Max's 'echo to output' flag: non-zero puts the value that just arrived in "
            "the same burst rather than the next one, 0 (the default) keeps the register one step "
            "behind. A float is truncated; anything that is not a whole finite number is ignored "
            "and leaves the defaults in place.",
            "1-256, optional echo flag");
}

void gBucket::ShapePorts() {
  // Rebuilt rather than resized: the stage *count* comes from the argument.
  // Safe because every caller runs before the object is wired or published —
  // the constructor, and the two parameter callbacks, which
  // patcherImplementation::CreateObjectUnlocked runs before AssignGraphIds. A
  // *live* SetParams never reaches here on a published object: registering the
  // callbacks makes ParamsNeedRebuild() true, so #234 replaces the object.
  int count = DEFAULT_PORTS;
  int requested = DEFAULT_PORTS;
  bool clamped = false;
  bool haveCount = false;

  echo = false;

  for (const std::string& token : creationArgs) {
    // Parameters::Set splits on single spaces, so a run of them yields empty
    // tokens; an empty argument is not a number.
    if (token.empty()) continue;

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
      clamped = (requested != count);
      haveCount = true;
      continue;
    }

    // Max's second argument: "A second non-zero argument sets the bucket object
    // to 'echo to output' mode." Any non-zero value means on.
    echo = (ExprToInt(number) != 0);
    break;
  }

  // The control thread, before the object is wired or published, so this is the
  // one place a clamp can be *said* rather than merely made observable through
  // OutletCount().
  if (clamped) {
    INTERNAL::LogImpl().emit(E_WARNING, "patcher: .bucket outlet count " + IntText(requested) +
                                            " is outside " + IntText(MIN_PORTS) + "-" +
                                            IntText(MAX_PORTS) + "; clamped to " + IntText(count));
  }

  outputs.clear();
  for (int i = 0; i < count; i++) {
    // ANY rather than a fixed type: a stage hands back the kind it was given, so
    // one outlet may carry ints at one moment and floats at another.
    ADD_OUT_ANY;
    outputs.back().SetDoc(OutletLabel(i), kOutletDoc, "any");
  }

  // Max starts a bucket holding zeroes, and `clear` returns it to exactly this.
  // Sized here and never again, so no message path allocates.
  slots.assign((std::size_t)count, Slot());

  // A re-parse also drops the run-time state: the object that comes back is the
  // one the arguments describe, and a direction or a freeze left over from the
  // previous shape would be invisible state a patch never asked for.
  rightToLeft = false;
  frozen = false;
}

PARM_CLEAR() {
  // Runs on the control thread before the parameter string is re-read, and is
  // the whole of `SetParams("")`: Parameters::Set returns without calling the
  // parse callback for an empty argument, so this has to leave Max's
  // no-argument object behind rather than one still holding the previous stage
  // count.
  creationArgs.clear();
  ShapePorts();
}

PARM_PARSE() {
  ShapePorts();
}

void gBucket::Capture(Slot* out) const {
  const std::size_t count = slots.size();
  for (std::size_t i = 0; i < count; i++)
    out[i] = slots[i];
}

void gBucket::SendBurst(const Slot* burst, std::size_t count, YSE::THREAD thread) {
  // Max: "freeze — suspends the bucket output". The object's output, not one
  // message's, so bang, `set` and `roll` are gagged along with the shift.
  if (frozen) return;

  // Max's universal order: the last outlet first, outlet 0 last. Signed index
  // rather than a reverse iterator so the "n-1 down to 0" reads the way the
  // guarantee is stated. Each Send returns only once the whole subgraph behind
  // that outlet has run.
  for (int i = (int)count - 1; i >= 0; i--) {
    const Slot& slot = burst[(std::size_t)i];
    if (slot.isFloat) {
      outputs[(std::size_t)i].SendFloat(slot.value, thread);
    } else {
      // Spelled as an int, so it leaves as an int atom. Through the
      // range-checked conversion rather than a cast: the stored float may be
      // wider than an int if it arrived as one of the wide integer spellings
      // ReadNumericToken accepts.
      outputs[(std::size_t)i].SendInt(ExprToInt(slot.value), thread);
    }
  }
}

void gBucket::Shift(float value, bool isFloat) {
  const std::size_t count = slots.size();

  if (rightToLeft) {
    // Max: "R2L — sets bucket to shift its stored values from right to left ...,
    // placing the incoming number in the rightmost outlet."
    for (std::size_t i = 0; i + 1 < count; i++)
      slots[i] = slots[i + 1];
    slots[count - 1].value = value;
    slots[count - 1].isFloat = isFloat;
    return;
  }

  // The default: "each number is moved one outlet to the right and the new
  // number is stored to be sent out the left outlet".
  for (std::size_t i = count - 1; i > 0; i--)
    slots[i] = slots[i - 1];
  slots[0].value = value;
  slots[0].isFloat = isFloat;
}

void gBucket::Accept(float value, bool isFloat, YSE::THREAD thread) {
  const std::size_t count = slots.size();

  // On the stack, not in a member: a patch that loops an outlet back into the
  // inlet re-enters inside SendBurst below, and a shared member buffer would be
  // overwritten by the inner burst before the outer one had finished with it.
  // Bounded by MAX_PORTS, so no allocation either way.
  Slot burst[MAX_PORTS];

  if (echo) {
    // Max: "the number received in the inlet is stored and sent out the left
    // outlet when it is received" — the shift happens first, so the value that
    // arrived is part of this burst.
    Shift(value, isFloat);
    Capture(burst);
  } else {
    // Max: "the numbers currently stored in bucket are sent out, then each
    // number is moved one outlet to the right" — the register stays one step
    // behind the input.
    Capture(burst);
    Shift(value, isFloat);
  }

  // The state is settled before the first send either way, so a re-entrant
  // message finds a register that has already shifted rather than shifting the
  // same step for every trip round the loop.
  SendBurst(burst, count, thread);
}

void gBucket::FeedTokens(const std::string& text, YSE::THREAD thread) {
  const std::size_t size = text.size();
  std::size_t i = 0;

  while (i < size) {
    while (i < size && (text[i] == ' ' || text[i] == '\t'))
      i++;
    if (i >= size) break;

    const std::size_t start = i;
    while (i < size && text[i] != ' ' && text[i] != '\t')
      i++;

    float value = 0.f;
    bool isFloat = false;
    // A non-numeric token inside an otherwise numeric message is skipped: the
    // register stores numbers, and there is no sensible stage value for a word.
    if (ReadValueToken(text.c_str() + start, i - start, value, isFloat))
      Accept(value, isFloat, thread);
  }
}

BANG_IN(SetBang) {
  if (inlet != 0) return;

  // Max: "bang — all stored values are sent out, but their position is not
  // shifted." A read of the register rather than a value, which is where this
  // object differs from `.buddy` and `.bondo`, whose bang is Max's zero.
  Slot burst[MAX_PORTS];
  Capture(burst);
  SendBurst(burst, slots.size(), thread);
}

INT_IN(SetInt) {
  if (inlet != 0) return;
  Accept((float)value, false, thread);
}

FLOAT_IN(SetFloat) {
  if (inlet != 0) return;
  Accept(value, true, thread);
}

LIST_IN(SetList) {
  if (inlet != 0) return;

  // The bare words first. Each is the whole message — there is no argument to
  // require, so these are compared rather than matched with MatchWord.
  if (value == "clear") {
    // Max: "The clear message resets the internal values of bucket without
    // causing any output." Back to the zeroes a fresh object holds.
    slots.assign(slots.size(), Slot());
    return;
  }

  if (value == "freeze") {
    // Max: "suspends the bucket output, but new incoming numbers continue to
    // shift the stored values internally."
    frozen = true;
    return;
  }

  if (value == "thaw") {
    frozen = false;
    return;
  }

  if (value == "L2R") {
    // Max: "sets bucket to shift its stored values from left to right (the
    // default)". Spelled exactly as Max spells it — Max's message names are
    // case-sensitive, and a lowercase alias would make `l2r` a message here and
    // a symbol there.
    rightToLeft = false;
    return;
  }

  if (value == "R2L") {
    rightToLeft = true;
    return;
  }

  // Max: "The word roll, followed by any number, causes bucket to use the value
  // stored in its rightmost outlet as input." *Any* number — the argument is
  // not read, so the bare word is the same message and is accepted too.
  std::size_t at = 0;
  if (value == "roll" || MatchWord(value, "roll", 4, at)) {
    // The far end: the stage the next input would push off, which is the last
    // outlet by default and outlet 0 after `R2L`. Taking the far end rather
    // than literally the rightmost is what keeps a rolling register rotating
    // instead of duplicating a value once the direction is reversed.
    const std::size_t far = rightToLeft ? 0 : slots.size() - 1;
    const Slot source = slots[far];
    Accept(source.value, source.isFloat, thread);
    return;
  }

  at = 0;
  if (MatchWord(value, "set", 3, at)) {
    // Max: "The word set, followed by a number, sends that number out each
    // outlet, and stores the number as the next value to be sent out each of
    // its outlets." It emits — the opposite of `.cycle`'s silent `set`, because
    // this one changes the whole visible contents of the register rather than a
    // cursor, and a register whose contents changed silently would leave every
    // downstream object holding a value the object no longer has.
    float number = 0.f;
    bool isFloat = false;
    if (!ReadValueArg(value, at, number, isFloat)) return;

    for (Slot& slot : slots) {
      slot.value = number;
      slot.isFloat = isFloat;
    }

    // Settled first, as everywhere else here, then sent from a private copy.
    Slot burst[MAX_PORTS];
    Capture(burst);
    SendBurst(burst, slots.size(), thread);
    return;
  }

  // Max documents no `list` method, so a multi-number message would reach the
  // `int` method with the extra atoms dropped. A shift register is a thing you
  // push a *sequence* through, so a number-leading message is fed in token by
  // token instead — a one-element list still behaves exactly as Max's `int`.
  // The leading-token test is Max's own parser rule, the same one `.bondo` and
  // `.cycle` apply to tell a `list` from an `anything`.
  std::size_t i = 0;
  const std::size_t size = value.size();
  while (i < size && (value[i] == ' ' || value[i] == '\t'))
    i++;
  const std::size_t start = i;
  while (i < size && value[i] != ' ' && value[i] != '\t')
    i++;

  float number = 0.f;
  if ((i > start) && ReadNumericToken(value.c_str() + start, i - start, number)) {
    FeedTokens(value, thread);
    return;
  }

  // Anything else is Max's `anything`, which bucket does not understand. Nothing
  // is stored and nothing is sent: forwarding a word through a register of
  // numbers would hand downstream objects a value the object cannot hold.
}
