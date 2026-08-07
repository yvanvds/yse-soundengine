#include "gUzi.h"
#include "../../implementations/logImplementation.h"
#include "../pListArgs.h"
#include "../pObjectList.hpp"
#include <cmath>
#include <cstddef>
#include <limits>
#include <string>

using namespace YSE::PATCHER;

#define className gUzi

namespace {

  // Truncate a float to an int the way C would, but answer what C leaves
  // undefined. `ExprToInt` is deliberately not used here: it folds *everything*
  // outside the int range to 0, which is right for an expression result and
  // wrong for a count — `.uzi 1e30` is a huge number of bangs and belongs at the
  // ceiling, not at the floor, and reporting it as "0 requested" would make the
  // clamp report a lie.
  int SaturateToInt(float value) {
    if (std::isnan(value)) return 0;
    if (value >= 2147483648.f) return std::numeric_limits<int>::max();
    if (value <= -2147483648.f) return std::numeric_limits<int>::min();
    return static_cast<int>(value);
  }

  // An int as text, through the patcher's one int formatter rather than
  // std::to_string. Control-thread only (the clamp log and GetGuiValue).
  std::string IntText(int value) {
    char digits[YSE::PATCHER::FORMAT_INT_WIDTH];
    const std::size_t written = YSE::PATCHER::WriteInt(value, digits);
    return std::string(digits, written);
  }

} // namespace

CONSTRUCT() {
  // The start inlet. Max: bang begins the run, int sets the count and then
  // begins it, and the five words (pause / break / resume / continue / offset)
  // are scoped here.
  ADD_IN_0;
  REG_BANG_IN(SetBang);
  REG_INT_IN(SetInt);
  REG_FLOAT_IN(SetFloat);
  REG_LIST_IN(SetList);

  // The count inlet. Cold, and int-only in Max — "In right inlet: Sets the
  // number of bang messages to send, without causing output." No bang and no
  // list handler is registered, so the object reports honestly through
  // GetAcceptedTypes() that neither means anything here, rather than accepting
  // them and doing nothing.
  ADD_IN_1;
  REG_INT_IN(SetInt);
  REG_FLOAT_IN(SetFloat);

  // Max's three, left to right. The middle one is the carry, as on `.counter`;
  // the right one is the index, and it is sent *before* each bang.
  ADD_OUT_BANG;
  ADD_OUT_BANG;
  ADD_OUT_INT;

  // Both creation arguments are read in the parse callback rather than written
  // straight into scalar fields, because both are clamped and the count's clamp
  // is reported. The clear callback is what makes `SetParams("")` return the
  // object to Max's no-argument shape (one bang, base 1) rather than leaving the
  // previous count in place.
  REG_PARM_CLEAR;
  REG_PARM_PARSE;

  ADD_PARAM(countArg);
  ADD_PARAM(baseArg);

  count = DEFAULT_COUNT;
  requestedCount = DEFAULT_COUNT;
  base = DEFAULT_BASE;

  ADD_DESCRIPTION(
      "Sends a burst of bangs immediately, with a running index — Max's uzi, and the patcher's "
      "loop. Every other object answers one message with one message, so until now filling a "
      "table, "
      "building a chord or spawning a burst of grains had to come from outside the patch; .uzi 16 "
      "is sixteen iterations with an index, in one box, inside one stimulus. Three outlets, and "
      "their order is the object: per iteration the index goes out the right outlet first and the "
      "bang out the left outlet second, which is Max's right-to-left order and here the whole "
      "point "
      "— the idiom wires the index into whatever holds the loop variable and the bang into "
      "whatever "
      "reads it, so an index arriving after its bang would make every iteration read the previous "
      "one's number. Max's wording is 'the number of each bang is sent out': the number and the "
      "bang are one pair. The middle outlet is the carry, and Max states its position explicitly "
      "rather than leaving it to the rule — 'after the last bang is sent out its left outlet, uzi "
      "sends one bang out its middle outlet [...] much like the carry outlet on the counter "
      "object' "
      "— so .uzi 3 produces index 1, bang, index 2, bang, index 3, bang, carry, in that order, "
      "each "
      "send completing in full (the whole subgraph behind it, depth first) before the next starts. "
      "The index is 1-based, Max's 'numbering begins from 1', and the optional second creation "
      "argument moves the base, so .uzi 8 0 is the 0-based loop a table index usually wants. The "
      "whole burst is one logical event — Max's own next reference cites this object as the "
      "example "
      "— so .uzi into .next gives one bang out the separated outlet and the rest out the continued "
      "one, whatever the count. Where this departs from Max is bounding, and it has to: the send "
      "path is synchronous, so an unbounded .uzi is not a slow object but a hang. The count is "
      "clamped to 4096, the ceiling .urn already uses, so one stimulus can never cost more than "
      "4096 iterations of this object; a larger count is reported rather than silently truncated, "
      "logged when it comes from the creation argument and readable as the gap between the "
      "requested and effective counts when it comes from a message. A start arriving while a run "
      "is "
      "in progress is refused and counted, so a bang outlet wired back into the start inlet "
      "terminates by construction rather than recursing N-deep per level until the stack goes. And "
      "the loop bound is pinned when the run starts, so a patch raising the count from the right "
      "inlet once per iteration — which is not a start and so never meets the guard — changes the "
      "next run rather than extending this one forever. pause (Max's break) stops a run from "
      "inside it, which is how a patch breaks out early: search a table with .uzi 4096 and pause "
      "the moment the value is found. It is read at the top of each iteration, so an iteration is "
      "atomic and an index is never left without its bang, and a paused run emits no carry, since "
      "the carry means the bangs have all been sent. resume (Max's continue) picks up where it "
      "stopped, numbering from wherever it left off and against the same pinned bound. offset n "
      "skips the first n iterations, Max's 'the number is subtracted from the previously assigned "
      "number of bangs'. An unrecognised symbol is ignored rather than converted to a bang: Max "
      "lists no anything method here, and where a stray message costs .bangbang one bang it would "
      "cost this object up to 4096. Floats are truncated towards zero on both inlets, as Max "
      "converts a float to an int. Calculate() does nothing — an emitting one would run the whole "
      "loop again on every DSP block from a stimulus no patch sent, which is the family's rule and "
      "here the most expensive possible way to break it.");
  ADD_CATEGORY(pCategory::GENERIC);
  INLET_DOC(0, "start",
            "A bang runs the loop; an int or float sets the count and then runs it, truncating "
            "towards zero. Also takes five words: 'pause' (or 'break') stops a run that is in "
            "progress, which only a message caused by the run's own output can do, and is silent "
            "and a no-op otherwise; 'resume' (or 'continue') sends the rest of a paused run, "
            "numbering from where it left off; 'offset n' skips the first n iterations of the next "
            "run. Any other symbol is ignored — this object does not convert anything to a bang. A "
            "start that arrives while a run is already in progress is refused, which is what makes "
            "an outlet wired back to here terminate.",
            "any");
  INLET_DOC(1, "count",
            "An int or float sets how many bangs the next run emits, without producing any output "
            "— Max's right inlet. Clamped to 0-4096. Setting it during a run applies to the next "
            "run, never to the one already going, so a patch feeding this inlet from the bang "
            "outlet cannot extend the loop it is inside.",
            "0-4096");
  OUTLET_DOC(0, "bang",
             "One bang per iteration, sent immediately after that iteration's index. The subgraph "
             "behind it runs to completion before the next iteration starts.",
             "bang");
  OUTLET_DOC(1, "carry",
             "One bang, after the last bang of a completed run — 'all the bang messages have been "
             "sent', like the carry outlet on .counter. Wire it to whatever should happen once the "
             "loop is done. A run stopped by 'pause' does not fire it; a run of zero bangs does, "
             "so an after-the-loop branch is not silently skipped when the count computes to 0.",
             "bang");
  OUTLET_DOC(2, "index",
             "The number of each bang, sent just before it. Counts from 1 by default, or from the "
             "base creation argument, and continues from where it left off after a 'pause' and "
             "'resume'.",
             "base to base+count-1");
  PARAM_DOC("count", "1",
            "How many bangs a run emits. With no argument the object sends one, as Max's does. "
            "Clamped to 0-4096: 4096 is the documented ceiling, and a larger argument is clamped "
            "and logged rather than silently truncated. A float is truncated towards zero; an "
            "argument that is not a whole finite number leaves the default in place.",
            "0-4096");
  PARAM_DOC("base", "1",
            "What the index counts from. Max: 'the base value defaults to 1 when no second "
            "argument is given', so .uzi 8 0 gives the 0-based indices a table wants. Clamped to "
            "+/- 1000000000, which is what keeps base+index inside the int range for any count.",
            "-1000000000 to 1000000000");
}

PARM_CLEAR() {
  // Runs on the control thread before the parameter string is re-read, and is
  // the whole of `SetParams("")`: Parameters::Set returns without calling the
  // parse callback for an empty argument, so this has to leave Max's
  // no-argument object behind rather than one still holding the previous count.
  // `refusedStarts` is deliberately not reset — it is a lifetime diagnostic, not
  // part of the object's configured state.
  countArg.clear();
  baseArg.clear();
  count = DEFAULT_COUNT;
  requestedCount = DEFAULT_COUNT;
  base = DEFAULT_BASE;
  offset = 0;
  runLength = 0;
  progress = 0;
  paused = false;
}

PARM_PARSE() {
  count = DEFAULT_COUNT;
  requestedCount = DEFAULT_COUNT;
  base = DEFAULT_BASE;
  offset = 0;
  runLength = 0;
  progress = 0;
  paused = false;

  float number = 0.f;
  // Strict on purpose, as the rest of the family is: `ExprParseFloatList` would
  // read `16abc` as 16 and fold `1e999` to 0, and neither answers "is this
  // creation argument a number at all".
  if (ReadNumericToken(countArg, number)) {
    ApplyCount(SaturateToInt(number));
    if (CountWasClamped()) {
      // The control thread, before the object is wired or published, so this is
      // the one place a clamp can be *said* rather than merely made observable.
      // A message arriving later cannot log — it may be on the audio thread —
      // which is why RequestedCount() exists alongside Count().
      INTERNAL::LogImpl().emit(E_WARNING, "patcher: .uzi count " + IntText(requestedCount) +
                                              " is outside " + IntText(MIN_COUNT) + "-" +
                                              IntText(MAX_COUNT) + "; clamped to " +
                                              IntText(count));
    }
  }

  if (ReadNumericToken(baseArg, number)) {
    int wanted = SaturateToInt(number);
    if (wanted > BASE_LIMIT) wanted = BASE_LIMIT;
    if (wanted < -BASE_LIMIT) wanted = -BASE_LIMIT;
    base = wanted;
  }
}

void gUzi::ApplyCount(int value) {
  // Both halves matter. `requestedCount` is what the patch asked for and is the
  // report; `count` is what will actually fire. Never logs: this runs on
  // whichever thread sent the message, which may be the audio thread, and
  // building a message there would allocate.
  requestedCount = value;
  if (value > MAX_COUNT) value = MAX_COUNT;
  if (value < MIN_COUNT) value = MIN_COUNT;
  count = value;
}

void gUzi::Start(YSE::THREAD thread) {
  // **The recursion guard.** A start that arrives while a run is in progress
  // came from inside that run — the send path is synchronous with no queue in
  // between, so an outlet wired back to this inlet re-enters here inside the
  // SendBang. Starting a nested run there is unbounded recursion with a
  // branching factor of `count`, which the send-depth ceiling in outlet.cpp
  // would only cut off after count^64 iterations. Refusing is not a stopgap: a
  // restart would reset the counter under the running loop and never terminate,
  // and a second interleaved loop over one shared counter is neither loop.
  if (running) {
    refusedStarts++;
    return;
  }

  // **The bound is pinned here, and this is the line the termination argument
  // rests on.** `count` is read exactly once per run. The count inlet is not a
  // start, so it never meets the guard above, and a patch that raises the count
  // from inside the loop would otherwise extend the loop it is inside, forever.
  runLength = count;

  // Max's `offset`: the first `offset` iterations are skipped, so the run emits
  // `count - offset` bangs. Clamped rather than treated as an error, so a
  // computed offset past the end is an empty run.
  progress = offset;
  if (progress > runLength) progress = runLength;

  Emit(thread);
}

void gUzi::Resume(YSE::THREAD thread) {
  if (running) {
    // A `resume` from inside a run is the same hazard as a `bang` from inside
    // one, and takes the same answer.
    refusedStarts++;
    return;
  }
  // Max: "If uzi has been stopped by a pause message in the midst of sending
  // its output, resume causes it to send out the rest of its output." Only
  // then: without this a `resume` on a finished object would emit no bangs and
  // a second carry for a run nobody started.
  if (!paused) return;
  Emit(thread);
}

void gUzi::Emit(YSE::THREAD thread) {
  // **The loop.** Bounded by `runLength`, which no handler can move while this
  // runs, so the iteration count is at most MAX_COUNT however the patch is
  // wired. No allocation, no lock, no I/O: integer arithmetic and two sends per
  // iteration, neither of which converts or formats anything.
  running = true;
  paused = false;

  while (progress < runLength && !paused) {
    // `base` is clamped to +/- BASE_LIMIT and `progress` to MAX_COUNT, so this
    // addition cannot leave the int range and needs no overflow check.
    const int index = base + progress;

    // Advanced *before* the sends, so a `pause` raised from inside them resumes
    // at the next iteration rather than repeating this one — Max: "numbering
    // begins wherever it left off".
    progress++;

    // The order is the object: index first, then the bang it numbers. The
    // `paused` test is at the top of the loop and not between these two, so an
    // iteration is atomic and an index is never left without its bang.
    outputs[2].SendInt(index, thread);
    outputs[0].SendBang(thread);
  }

  // Cleared before the carry, so anything the carry reaches sees a settled
  // object — and so a `pause` arriving from the carry's subgraph is the no-op
  // it should be rather than a pause of a run that has already ended.
  running = false;

  // Max: "After the last bang is sent out its left outlet, uzi sends one bang
  // out its middle outlet." A run stopped by `pause` has no last bang yet, so
  // no carry; a run of zero bangs has sent all of them, so it does carry.
  if (paused) return;
  outputs[1].SendBang(thread);
}

BANG_IN(SetBang) {
  // Max lists no bang method for the right inlet, and none is registered there.
  if (inlet != 0) return;
  Start(thread);
}

INT_IN(SetInt) {
  // Max: left inlet "Sets the number of bang messages to send, then begins
  // sending them out"; right inlet "without causing output". Taken as an int
  // rather than routed through the float handler, so a large base or count
  // keeps every bit it arrived with.
  ApplyCount(value);
  if (inlet == 0) Start(thread);
}

FLOAT_IN(SetFloat) {
  // Max's uzi takes an int; the patcher has one numeric type, so a float is
  // truncated towards zero here the way Max converts one, rather than ignored.
  ApplyCount(SaturateToInt(value));
  if (inlet == 0) Start(thread);
}

LIST_IN(SetList) {
  // Max scopes every word to the left inlet and gives the right one an int
  // method only, so a list arriving there means nothing.
  if (inlet != 0) return;

  // Bare, as `.onebang`'s `stop` and `.change`'s `mode` are: `pause 1` is a
  // list, not the message. No allocation — a length check and a memcmp against
  // a literal.
  if (value == "pause" || value == "break") {
    // Max: "Causes uzi to stop in the midst of sending its output. (Since uzi
    // sends its output as fast as possible, this message must be triggered in
    // some way by the output of uzi itself.)" So the only meaningful arrival is
    // re-entrant, and outside a run this is a no-op rather than a pause stored
    // for later — a stored one would make the next `resume` fire a carry for a
    // run nobody started.
    if (running) paused = true;
    return;
  }

  if (value == "resume" || value == "continue") {
    Resume(thread);
    return;
  }

  std::size_t at = 0;
  if (MatchWord(value, "offset", 6, at)) {
    // Max: "the number is subtracted from the previously assigned number of
    // bangs to equal the new total number of bangs" — so this is a start index,
    // and the run emits `count - offset` bangs. Read through the shared
    // allocation-free reader; a malformed argument leaves the previous offset
    // alone rather than resetting it to 0.
    int wanted = 0;
    if (ReadIntArgAt(value, at, wanted)) {
      if (wanted < 0) wanted = 0;
      if (wanted > MAX_COUNT) wanted = MAX_COUNT;
      offset = wanted;
    }
    return;
  }

  // Max lists no `anything` method for this object, so an unrecognised symbol
  // is ignored rather than converted to a bang. That is the opposite of
  // `.bangbang` and `.onebang`, deliberately: there a stray message costs one
  // bang, here it would cost up to MAX_COUNT of them.
}

GUI_VALUE() {
  // The one number that says what the box will do. Reporting the *effective*
  // count rather than the requested one makes a clamp visible from outside the
  // object without a C++ accessor.
  return IntText(count);
}
