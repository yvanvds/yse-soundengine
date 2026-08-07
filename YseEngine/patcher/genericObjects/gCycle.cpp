#include "gCycle.h"
#include "../../implementations/logImplementation.h"
#include "../math/gExprEval.h"
#include "../pListArgs.h"
#include "../pObjectList.hpp"
#include <cstddef>
#include <cstdint>
#include <string>

using namespace YSE::PATCHER;

#define className gCycle

namespace {

  // An int as text, through the patcher's one int formatter rather than
  // std::to_string. Control-thread only (the construction-time clamp log).
  std::string IntText(int value) {
    char digits[YSE::PATCHER::FORMAT_INT_WIDTH];
    const std::size_t written = YSE::PATCHER::WriteInt(value, digits);
    return std::string(digits, written);
  }

  constexpr char kInletDoc[] =
      "Everything that arrives leaves by the next outlet in the rotation, which then moves on and "
      "wraps round to outlet 0 after the last. A bang, int or float goes out one outlet whole. A "
      "message whose first token is a number is Max's list and is dealt one token per outlet, each "
      "forwarded as the kind it was spelled as; anything else is Max's 'anything' and leaves one "
      "outlet verbatim. 'set <n>' points the rotation at outlet n and emits nothing; 'thresh <n>' "
      "turns event mode on (non-zero) or off.";

  constexpr char kOutletDoc[] =
      "Every nth message, counting from wherever the rotation currently stands — bang, int, float "
      "or text, whichever arrived. Only one outlet fires per message, so there is no firing order "
      "to respect.";

} // namespace

CONSTRUCT() {
  // Every outlet is built by ShapePorts(), so a saved `.cycle 4` comes back with
  // four of them. The clear callback is what makes `SetParams("")` return the
  // object to Max's no-argument shape rather than leaving the previous outlet
  // count in place.
  REG_PARM_CLEAR;
  REG_PARM_PARSE;

  ADD_PARAM(creationArgs);

  // One inlet, as Max has: `set` and `thresh` arrive here with everything else.
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
      "Deals successive messages to successive outlets, wrapping round to the first after the last "
      "— Max's cycle, 'each incoming number is sent to the next outlet, wrapping around to the "
      "first outlet after the last has been reached'. The dealer: one cord in, N cords out, and "
      "consecutive messages land on consecutive outlets, which is the natural way to spread notes "
      "across a bank of voices, alternate between two effect chains, or split one stream into "
      "parallel processing paths. Built by hand it is a .counter wrapped at N into a .gate's "
      "select "
      "inlet with the value routed through the other one — three boxes and an inlet-ordering "
      "hazard "
      "to say one thing. It is not .poly, which allocates by voice availability: this rotates "
      "blindly, so the same outlet comes round every N messages whatever the receivers are doing, "
      "which is right where they are stateless or identical and wrong where they are a bank of "
      "voices that may still be sounding. It is not .gate or .switch either, which route by an "
      "index the patch supplies, where here the index is the object's own and the traffic advances "
      "it; and unlike .trigger, which fans one message out every outlet, this sends each message "
      "out exactly one, so there is no right-to-left order to state. The outlet is taken and the "
      "position advanced before the message is sent, which is correctness and not tidiness: the "
      "send path is synchronous, so a patch looping an outlet back into the inlet re-enters inside "
      "the Send and must find the rotation already moved on — advancing afterwards would hand "
      "every "
      "message of the loop the same outlet. A message whose first token is a number is Max's list "
      "and is dealt one token per outlet ('the stream of ints, floats, or symbols to be directed "
      "to "
      "successive outlets'), each token forwarded as the kind it was spelled as so that 1 2 3 does "
      "not come back out as 1. 2. 3.; anything else is Max's 'anything' and leaves one outlet "
      "verbatim, the same leading-token rule .bondo uses, because splitting 'note 60 100' across "
      "three outlets would arrive downstream as three unrelated fragments and every object "
      "matching "
      "on a leading word would stop seeing it. 'set <n>' moves the rotation to outlet n and emits "
      "nothing, so the next message lands where it was told and the rotation carries on from "
      "there; "
      "an index outside the outlet range is ignored rather than wrapped, since a set 4 on a "
      "three-outlet object is a miscount and silently redirecting the stream would hide it. The "
      "second creation argument and the 'thresh' message set Max's output mode, which restarts the "
      "rotation at outlet 0 for each new logical event: not a clock and not an elapsed time — this "
      "reads the same logical-event ids .next does, so two host calls are two events however close "
      "together and a whole .trigger fan-out is one however slow. With mode on, a three-note chord "
      "arriving as one burst is dealt 0, 1, 2 and the next chord starts at 0 again however long "
      "the "
      "first was, which is the difference between a voice bank that stays aligned with the music "
      "and one that drifts by a voice per odd-sized chord; with mode off, the default, the "
      "rotation "
      "ignores event boundaries and keeps counting. 'set' is honoured for the rest of its own "
      "event "
      "and a later event restarts over the top of it, which falls out of the ordering rather than "
      "being a special case. 'thresh' is run-time state rather than a parameter, so it does not "
      "survive a save; the creation argument is what a patch carries. At most 256 outlets, built "
      "once before the object is published — Calculate() does nothing, and no message path "
      "allocates, locks or blocks.");
  ADD_CATEGORY(pCategory::GENERIC);
  PARAM_DOC("outlets", "1",
            "Max's argument list, in Max's order. The first whole number is how many outlets to "
            "build, clamped to 1-256 and defaulting to 1. A second whole number is Max's output "
            "mode: non-zero restarts the rotation at outlet 0 for each new logical event, 0 (the "
            "default) ignores event boundaries. A float is truncated; anything that is not a whole "
            "finite number is ignored and leaves the defaults in place.",
            "1-256, optional mode");
}

void gCycle::ShapePorts() {
  // Rebuilt rather than resized: the outlet *count* comes from the argument.
  // Safe because every caller runs before the object is wired or published —
  // the constructor, and the two parameter callbacks, which
  // patcherImplementation::CreateObjectUnlocked runs before AssignGraphIds. A
  // *live* SetParams never reaches here on a published object: registering the
  // callbacks makes ParamsNeedRebuild() true, so #234 replaces the object.
  int count = DEFAULT_PORTS;
  int requested = DEFAULT_PORTS;
  bool clamped = false;
  bool haveCount = false;

  eventMode = false;

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

    // Max's second argument: "Sets the output mode. If it is non-zero, cycle
    // detects separate 'events' and restarts at the leftmost outlet when a new
    // event occurs." Any non-zero value means on, as `thresh` does.
    eventMode = (ExprToInt(number) != 0);
    break;
  }

  // The control thread, before the object is wired or published, so this is the
  // one place a clamp can be *said* rather than merely made observable through
  // OutletCount().
  if (clamped) {
    INTERNAL::LogImpl().emit(E_WARNING, "patcher: .cycle outlet count " + IntText(requested) +
                                            " is outside " + IntText(MIN_PORTS) + "-" +
                                            IntText(MAX_PORTS) + "; clamped to " + IntText(count));
  }

  outputs.clear();
  for (int i = 0; i < count; i++) {
    // ANY rather than a fixed type: whatever arrived leaves as itself, so a
    // single outlet may carry bangs, ints, floats and text at different times.
    ADD_OUT_ANY;
    outputs.back().SetDoc(OutletLabel(i), kOutletDoc, "any");
  }

  // A re-parse deliberately restarts the deal: the outlet the old position
  // named may not exist any more, and a patch that has just changed the shape
  // of the object has no expectation of where the rotation stood.
  next = 0;
  lastEvent = 0;
  seen = false;

  // The one allocation a dealt text token would otherwise need.
  tokenText.reserve(TEXT_CAPACITY);
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

void gCycle::NoteEvent() {
  // The dispatch layer's answer to "which stimulus is being delivered right
  // now". Opaque and unique: only ever compared, never interpreted.
  const std::uint64_t event = CurrentMessageEvent();

  // The same three conditions `.next` (#471) tests, and each is load-bearing
  // here for the same reason: the first message ever has no previous message to
  // share an event with, and a 0 id means "no dispatch in progress" — two
  // handler calls made directly rather than through an inlet are two stimuli
  // wearing the same non-id, and folding them together would report a burst
  // that never happened.
  const bool same = seen && event != 0 && event == lastEvent;

  lastEvent = event;
  seen = true;

  // Max: "cycle detects separate 'events' and restarts at the leftmost outlet
  // when a new event occurs." Applied on arrival rather than after the send, so
  // a `set` in the same event is honoured over the top of it and the next
  // event's restart is honoured over the top of the `set`.
  if (eventMode && !same) next = 0;
}

int gCycle::TakeOutlet() {
  const int outlet = next;

  // Max: "wrapping around to the first outlet after the last has been reached."
  // Advanced *before* the caller sends: the send path is synchronous and
  // re-entrant, so a patch looping an outlet back into this inlet arrives here
  // again inside the Send and has to find the rotation already moved on.
  next++;
  if (next >= (int)outputs.size()) next = 0;

  return outlet;
}

void gCycle::DealTokens(const std::string& text, YSE::THREAD thread) {
  // Max, for `list`: "The stream of ints, floats, or symbols to be directed to
  // successive outlets" — plural outlets for one message, so the elements are
  // dealt one apiece and the rotation wraps through them like any other run of
  // messages.
  const std::size_t size = text.size();
  std::size_t i = 0;

  while (i < size) {
    while (i < size && (text[i] == ' ' || text[i] == '\t'))
      i++;
    if (i >= size) break;

    const std::size_t start = i;
    while (i < size && text[i] != ' ' && text[i] != '\t')
      i++;

    const char* token = text.c_str() + start;
    const std::size_t length = i - start;
    const int outlet = TakeOutlet();

    float number = 0.f;
    if (ReadNumericToken(token, length, number)) {
      // The int-atom / float-atom test lives in pListArgs.h next to the reader
      // that agreed the token is a number; `.trigger` classifies its constants
      // and `.bondo` stores its spread elements by the same answer, so an int
      // token is not widened into a float on its way through.
      if (TokenLooksLikeFloat(token, length)) {
        outputs[(std::size_t)outlet].SendFloat(number, thread);
      } else {
        // Spelled as an int, so it is an int atom — but the token may still be
        // wider than an int (ReadNumericToken only promised a finite float), so
        // truncate through the range-checked conversion rather than casting.
        outputs[(std::size_t)outlet].SendInt(ExprToInt(number), thread);
      }
      continue;
    }

    // A symbol inside an otherwise numeric list. Into the buffer reserved when
    // the ports were shaped: no allocation up to TEXT_CAPACITY, one beyond it.
    tokenText.assign(token, length);
    outputs[(std::size_t)outlet].SendList(tokenText, thread);
  }
}

BANG_IN(SetBang) {
  if (inlet != 0) return;
  NoteEvent();
  // Max: "Sends a bang to the next outlet." A bang is a message like any other
  // here — it takes its turn and moves the rotation on.
  outputs[(std::size_t)TakeOutlet()].SendBang(thread);
}

INT_IN(SetInt) {
  if (inlet != 0) return;
  NoteEvent();
  outputs[(std::size_t)TakeOutlet()].SendInt(value, thread);
}

FLOAT_IN(SetFloat) {
  if (inlet != 0) return;
  NoteEvent();
  // Kept as a float rather than truncated: the object forwards, it does not
  // compute, so widening or narrowing a value in transit would be a side effect
  // visible to every downstream object that tells the two apart.
  outputs[(std::size_t)TakeOutlet()].SendFloat(value, thread);
}

LIST_IN(SetList) {
  if (inlet != 0) return;

  // Before the two messages, so that a `set` and the message it was meant for
  // share an event and an event-mode restart cannot land between them.
  NoteEvent();

  std::size_t at = 0;
  if (MatchWord(value, "set", 3, at)) {
    // Max: "The word set, followed by a number, specifies an outlet to which
    // the next input should be directed." Emits nothing — it moves the
    // position, and the rotation carries on from there.
    int outlet = 0;
    if (!ReadIntArg(value, at, outlet)) return;

    // Out of range is ignored, as `.gate` ignores a select index it has no
    // outlet for. Wrapping would be defensible in an object that wraps
    // everything else, but a `set 4` on a three-outlet object is a miscount and
    // quietly dealing its stream to outlet 1 would hide it.
    if (outlet < 0 || outlet >= (int)outputs.size()) return;

    next = outlet;
    return;
  }

  at = 0;
  if (MatchWord(value, "thresh", 6, at)) {
    // Max: "The word thresh, followed by a number, sets the output mode."
    // Non-zero is on, matching the second creation argument. Run-time state:
    // it does not write back to the parameters, so it does not survive a save.
    int mode = 0;
    if (!ReadIntArg(value, at, mode)) return;
    eventMode = (mode != 0);
    return;
  }

  // Which of Max's two methods this message would have reached. Max's parser
  // calls a message a `list` when its first atom is a number and an `anything`
  // when it is a symbol; this patcher has one message type behind both, so the
  // leading token has to answer it — the same question Max's own parser asks,
  // and the same rule `.bondo` applies.
  std::size_t i = 0;
  const std::size_t size = value.size();
  while (i < size && (value[i] == ' ' || value[i] == '\t'))
    i++;
  const std::size_t start = i;
  while (i < size && value[i] != ' ' && value[i] != '\t')
    i++;

  float number = 0.f;
  const bool leadsWithNumber =
      (i > start) && ReadNumericToken(value.c_str() + start, i - start, number);

  if (leadsWithNumber) {
    DealTokens(value, thread);
    return;
  }

  // Max's `anything`: out one outlet, verbatim. Splitting it would arrive
  // downstream as unrelated fragments and would hide the leading word from
  // every object that matches on one.
  outputs[(std::size_t)TakeOutlet()].SendList(value, thread);
}
