#include "gRateLimit.h"
#include "../math/gExprEval.h"
#include "../pListArgs.h"
#include "../pObjectList.hpp"
#include "../pSelector.h"
#include "timeValue.h"
#include <cmath>
#include <cstddef>
#include <cstdint>

using namespace YSE::PATCHER;

#define className gRateLimitBase

namespace {

  // A float interval as the milliseconds it means. Negatives and NaN become 0 —
  // no limiting, which is Max's default — and anything past the int range
  // saturates rather than being cast, since casting a float outside that range
  // is undefined behaviour. `.delay`, `.pipe`, `.mtr` and `.seq` decide the same
  // question the same way.
  int MillisFromFloat(float value) {
    // Written as a failed `>` rather than `<=` so a NaN takes this branch too.
    if (!(value > 0.f)) return 0;
    if (value >= 2147483647.f) return 2147483647;
    return (int)value;
  }

  constexpr char kTimeInletDoc[] =
      "Sets the minimum time between outputs — Max's 'the number is stored as the minimum amount "
      "of "
      "time, in milliseconds, between successive outputs'. Ints, floats and a list whose leading "
      "token is a number all set it in milliseconds; a negative or NaN time counts as 0, which "
      "means no limiting at all rather than a one-block wait. Since issue #728 the interval may "
      "also be tempo-relative, Max's 'the time can be specified in milliseconds or using a "
      "tempo-relative interval': a note value ('4n', '4nd', '8nt') or a tick count ('1440 ticks') "
      "sets it in beats on the domain clock named by the left inlet's 'clock <name>' message, and "
      "any plain number puts it back on milliseconds — the unit travels with the value, which is "
      "Max's model. A beat interval with no clock bound limits nothing rather than closing the "
      "window for good; bind a clock and it starts limiting. bars.beats.units is not read, needing "
      "a meter no domain clock has. The new interval applies to the next message to arrive and is "
      "measured from the last output, so shortening it can open the window immediately. There is "
      "no "
      "bang method on this inlet.";

} // namespace

gRateLimitBase::gRateLimitBase() : pObject(false) {
  ADD_IN_0;
  REG_BANG_IN(BangIn);
  REG_INT_IN(IntIn);
  REG_FLOAT_IN(FloatIn);
  REG_LIST_IN(ListIn);

  ADD_IN_1;
  REG_INT_IN(IntIn);
  REG_FLOAT_IN(FloatIn);
  REG_LIST_IN(ListIn);

  // ANY rather than a fixed type: the object hands back whichever of bang, int,
  // float and text it was given, and a limiter that retyped its payload would
  // not be limiting the message a patch sent.
  ADD_OUT_ANY;

  ADD_PARAM(interval);
  ADD_PARAM(intervalbeats);
  ADD_PARAM(threshold);
  ADD_PARAM(thresholdbeats);
  ADD_PARAM(quantize);
  interval = DEFAULT_INTERVAL;
  intervalbeats = 0.f;
  threshold = 0;
  thresholdbeats = 0.f;
  quantize = 0.f;

  ADD_CATEGORY(pCategory::TIME);
}

void gRateLimitBase::Document(const char* summary, const char* inletDoc, const char* outletDoc,
                              const char* paramDoc) {
  ADD_DESCRIPTION(summary);
  INLET_DOC(0, "in", inletDoc,
            "bang, int, float, list, 'clock <name>', 'threshold <time>', "
            "'quantize <time>'");
  INLET_DOC(1, "interval", kTimeInletDoc, "0+ ms, or a note value / tick count");
  OUTLET_DOC(0, "out", outletDoc, "any");
  PARAM_DOC("interval", "0", paramDoc, "0+ ms");
  PARAM_DOC(
      "intervalbeats", "0",
      "The initial interval in beats, for the tempo-relative unit issue #728 added. 0 means "
      "the interval is the millisecond one above, which is what a Max limiter is until it is "
      "given a tempo-relative time; any positive value makes it a beat count on the domain "
      "clock a 'clock <name>' message names, and a note value or tick count in the right "
      "inlet overwrites it afterwards. A plain number in the right inlet clears it back to 0. "
      "A beat interval with no clock bound limits nothing, rather than closing the window for "
      "good the way an unmeasurable wait would. The clock binding itself is run-time state "
      "and is not saved. A negative value counts as 0.",
      "0+ beats");
  PARAM_DOC(
      "threshold", "0",
      "The initial threshold in milliseconds — Max's 'time threshold under which only one "
      "message may pass' (issue #728), and 0, the default, is off. It is a second window "
      "measured from the same output as the interval, and a message arriving inside it is "
      "dropped outright on both objects: not passed, not held, not queued. Turned on it "
      "composes with the interval rather than replacing it, so the effective floor on the "
      "output rate is the longer of the two. The 'threshold <time>' message in the left inlet "
      "overwrites it afterwards. A negative value counts as 0.",
      "0+ ms");
  PARAM_DOC("thresholdbeats", "0",
            "The initial threshold in beats, the tempo-relative spelling of the threshold above "
            "(issue #728). 0 means the threshold is the millisecond one; any positive value makes "
            "it a beat count on the bound domain clock, and 'threshold <time>' with a plain number "
            "clears it back to 0. A beat threshold with no clock bound does not bite. A negative "
            "value counts as 0.",
            "0+ beats");
  PARAM_DOC(
      "quantize", "0",
      "The initial output grid in beats — Max's 'send output only on the specified "
      "time-boundary if appropriate' (issue #728). 0, the default, is no quantizing. Beats "
      "only, and inert until a clock is bound: a grid is a musical grid, and Max's other "
      "spelling for it, bars.beats.units, needs a meter no domain clock has. With a grid set, "
      "a held message is released on the first grid line at or after the moment its interval "
      "is up, and an arriving message may pass only once the grid has moved past the line the "
      "last output sat on — at most one output per line, and never one before it. The "
      "'quantize <time>' message in the left inlet overwrites it afterwards, taking a note "
      "value or a tick count; 'quantize 0' clears the grid. A negative value counts as 0.",
      "0+ beats");
}

int gRateLimitBase::Interval() const {
  const Int ms = interval.load();
  return ms > 0 ? (int)ms : 0;
}

double gRateLimitBase::IntervalBeats() const {
  const Flt beats = intervalbeats.load();
  // Written as a failed `>` so a NaN — which a live SetParams re-parse could
  // store — reads as "no beat interval" rather than as a window nothing can
  // satisfy.
  return beats > 0.f ? (double)beats : 0.0;
}

int gRateLimitBase::Threshold() const {
  const Int ms = threshold.load();
  return ms > 0 ? (int)ms : 0;
}

double gRateLimitBase::ThresholdBeats() const {
  const Flt beats = thresholdbeats.load();
  return beats > 0.f ? (double)beats : 0.0;
}

double gRateLimitBase::Quantize() const {
  const Flt beats = quantize.load();
  return beats > 0.f ? (double)beats : 0.0;
}

const char* gRateLimitBase::ClockName() const {
  const clockBridge::Handle bound = binding.load(std::memory_order_relaxed);
  if (bound == 0) return "";
  const clockBridge* clocks = Clocks();
  if (clocks == nullptr) return "";
  return clocks->NameOf(bound);
}

void gRateLimitBase::SetClock(const char* name, std::size_t length) {
  // Max's bare `clock`: back to the patcher's own millisecond clock. Whatever
  // is already waiting keeps the deadline it was armed with — a clock change is
  // not a retiming, the way the interval inlet is not one either.
  if (name == nullptr || length == 0) {
    binding.store(0, std::memory_order_relaxed);
    return;
  }

  // A standalone object has no patcher and so no bridge, exactly as it has no
  // scheduler to defer into. Silent, since this may be the audio thread.
  clockBridge* clocks = Clocks();
  if (clocks == nullptr) return;

  // Wait-free: a bounded walk over the patcher's binding table and a memcpy of
  // the name into a slot that already exists. The name is *not* looked up here
  // — that takes the clock manager's mutex and happens on the background pool.
  const clockBridge::Handle bound = clocks->Bind(name, length);
  // A refusal (the table is full, or the name is longer than a slot holds)
  // leaves the object on whatever clock it was on rather than silently falling
  // back to milliseconds, which would change what a stored beat time means.
  if (bound == 0) return;
  binding.store(bound, std::memory_order_relaxed);
}

std::uint64_t gRateLimitBase::NowBlock() const {
  // A standalone object has no patcher and so no clock. Everything then measures
  // at 0 and passes straight through, which is the only honest answer: there is
  // no time for an interval to be measured against. `.mtr`'s and `.seq`'s
  // arrangement.
  const messageScheduler* scheduler = Scheduler();
  return scheduler == nullptr ? 0 : scheduler->Now();
}

int gRateLimitBase::SinceLastOutput() const {
  if (!hasOutput.load(std::memory_order_relaxed)) return -1;
  if (Scheduler() == nullptr) return -1;
  const std::uint64_t now = NowBlock();
  const std::uint64_t last = lastBlock.load(std::memory_order_relaxed);
  // The comparison is defensive: the block clock is monotonic, but an object
  // that output before the patcher started would otherwise read a gap backwards.
  return messageScheduler::MillisForBlocks(now > last ? now - last : 0);
}

double gRateLimitBase::BeatsForMillis(int millis, clockBridge::Handle bound) const {
  const clockBridge* clocks = Clocks();
  float bpm = 0.f;
  // Two acquire loads through the bridge, and false for a binding that has not
  // resolved — in which case the millisecond part of a quantized deadline is
  // simply 0 and the release lands on the next grid line.
  if (clocks == nullptr || !clocks->Tempo(bound, bpm) || !(bpm > 0.f)) return 0.0;
  return ((double)millis * (double)bpm) / 60000.0;
}

void gRateLimitBase::Emit(Held kind, int intValue, float floatValue, const std::string* text,
                          YSE::THREAD thread) {
  // The window is opened *before* the send rather than after. A send runs the
  // whole subgraph behind the outlet, which may come back into this object, and
  // a re-entrant message must measure itself against the window this output just
  // started — not against the previous one, which would let it straight through.
  lastBlock.store(NowBlock(), std::memory_order_relaxed);

  // And the same instant on the bound clock, which is what a beat window is
  // measured from and which grid line this output used. 0 when there is no
  // clock or its binding has not resolved: `Beat` then leaves the value alone.
  double beat = 0.0;
  const clockBridge::Handle bound = binding.load(std::memory_order_relaxed);
  const clockBridge* clocks = Clocks();
  if (bound != 0 && clocks != nullptr) clocks->Beat(bound, beat);
  lastBeat.store(beat, std::memory_order_relaxed);

  hasOutput.store(true, std::memory_order_relaxed);

  switch (kind) {
  case Held::BANG:
    outputs[0].SendBang(thread);
    break;
  case Held::INT:
    outputs[0].SendInt(intValue, thread);
    break;
  case Held::FLOAT:
    outputs[0].SendFloat(floatValue, thread);
    break;
  case Held::LIST:
    if (text != nullptr) outputs[0].SendList(*text, thread);
    break;
  }
}

gRateLimitBase::Verdict gRateLimitBase::Evaluate(int elapsedMs, double elapsedBeats, bool onClock,
                                                 double beatNow, double lastBeatValue,
                                                 clockBridge::Handle bound, bool applyThreshold,
                                                 Wait& wait) const {
  // Max, on both objects: "time threshold under which only one message may
  // pass." A second window measured from the same output, inside which nothing
  // passes and nothing is held either. A beat threshold with no clock bound
  // cannot be measured, so it does not bite — see the header.
  const double thresholdBeats = ThresholdBeats();
  bool underThreshold = false;
  if (applyThreshold) {
    underThreshold =
        thresholdBeats > 0.0 ? (onClock && elapsedBeats < thresholdBeats) : elapsedMs < Threshold();
  }

  // The interval, in whichever unit it was given. A beat interval with no clock
  // bound limits nothing rather than never opening: a limiter whose window
  // never opens is a black hole, which is the one failure this object must not
  // have. See the header.
  const double intervalBeats = IntervalBeats();
  const bool beatInterval = intervalBeats > 0.0 && onClock;
  const double remainingBeats = beatInterval ? intervalBeats - elapsedBeats : 0.0;
  const int remainingMs = intervalBeats > 0.0 ? 0 : Interval() - elapsedMs;
  // `<= 0` rather than `< 0`, so an interval of 0 — Max's default, and no
  // limiting — passes everything: a rate limiter set to no limit is a wire.
  const bool intervalUp = beatInterval ? !(remainingBeats > 0.0) : remainingMs <= 0;

  const double grid = Quantize();
  if (onClock && grid > 0.0) {
    // Max: "send output only on the specified time-boundary if appropriate."
    // The line the last output sat on is spoken for; a second output may not
    // share it, so what a grid promises is at most one output per line.
    const double usedLine = std::floor(lastBeatValue / grid);
    if (intervalUp && std::floor(beatNow / grid) > usedLine) {
      return underThreshold ? Verdict::DROP : Verdict::PASS;
    }
    if (underThreshold) return Verdict::DROP;

    // Where the interval is up, in beats, and then the first grid line at or
    // after it. A millisecond interval is converted at the clock's current
    // tempo, which is the one approximation here: the line itself is exact in
    // beats, so a tempo change during the wait bends the wait rather than the
    // grid.
    double deadline = beatNow;
    if (beatInterval) {
      if (remainingBeats > 0.0) deadline += remainingBeats;
    } else if (remainingMs > 0) {
      deadline += BeatsForMillis(remainingMs, bound);
    }
    double line = std::ceil(deadline / grid);
    if (line <= usedLine) line = usedLine + 1.0;
    const double nowLine = std::ceil(beatNow / grid);
    if (line < nowLine) line = nowLine;
    wait.binding = bound;
    wait.beats = (line * grid) - beatNow;
    if (!(wait.beats > 0.0)) wait.beats = 0.0;
    return Verdict::DEFER;
  }

  if (intervalUp) return underThreshold ? Verdict::DROP : Verdict::PASS;
  if (underThreshold) return Verdict::DROP;

  if (beatInterval) {
    wait.binding = bound;
    wait.beats = remainingBeats;
  } else {
    wait.millis = remainingMs;
  }
  return Verdict::DEFER;
}

gRateLimitBase::Verdict gRateLimitBase::Test(Wait& wait) const {
  if (Scheduler() == nullptr) {
    // No patcher, no clock, no "since the previous output". See the header.
    return Verdict::PASS;
  }

  // Max: "provided that a certain minimum time has elapsed since the previous
  // output". Before the first output there is no previous one to measure
  // against, so the first message always passes.
  if (!hasOutput.load(std::memory_order_relaxed)) return Verdict::PASS;

  const clockBridge::Handle bound = binding.load(std::memory_order_relaxed);
  const clockBridge* clocks = Clocks();
  double beatNow = 0.0;
  const bool onClock = bound != 0 && clocks != nullptr && clocks->Beat(bound, beatNow);

  const std::uint64_t now = NowBlock();
  const std::uint64_t last = lastBlock.load(std::memory_order_relaxed);
  const int elapsedMs = messageScheduler::MillisForBlocks(now > last ? now - last : 0);

  const double lastBeatValue = lastBeat.load(std::memory_order_relaxed);
  const double elapsedBeats = onClock && beatNow > lastBeatValue ? beatNow - lastBeatValue : 0.0;

  return Evaluate(elapsedMs, elapsedBeats, onClock, beatNow, lastBeatValue, bound, true, wait);
}

gRateLimitBase::Wait gRateLimitBase::ReopenWait() const {
  Wait wait;
  const clockBridge::Handle bound = binding.load(std::memory_order_relaxed);
  const clockBridge* clocks = Clocks();
  double beatNow = 0.0;
  const bool onClock = bound != 0 && clocks != nullptr && clocks->Beat(bound, beatNow);
  // Nothing has elapsed: the output this is measured from has only just
  // happened. The threshold is deliberately not applied — it gates *arrivals*,
  // and a message already waiting was accepted when it arrived; a limiter that
  // dropped one it had already promised to send would be neither policy.
  Evaluate(0, 0.0, onClock, beatNow, lastBeat.load(std::memory_order_relaxed), bound, false, wait);
  return wait;
}

void gRateLimitBase::Offer(Held kind, int intValue, float floatValue, const std::string* text,
                           YSE::THREAD thread) {
  Wait wait;
  switch (Test(wait)) {
  case Verdict::PASS:
    Emit(kind, intValue, floatValue, text, thread);
    return;
  case Verdict::DROP:
    // The threshold window: "only one message may pass", and one already has.
    // Neither object holds anything here.
    CountDrop();
    return;
  case Verdict::DEFER:
    // Too soon. What that means is the whole difference between the two
    // objects.
    Blocked(kind, intValue, floatValue, text, wait, thread);
    return;
  }
}

bool gRateLimitBase::ReadTime(const std::string& value, std::size_t begin, int& millis,
                              double& beats) {
  // Max's tempo-relative syntax is read *first* and as a whole, because its
  // tick spelling starts with a number: `1440 ticks` taken for its leading
  // token would silently become 1440 ms, which is the mistake `.clocker` made
  // before #725.
  double asBeats = 0.0;
  if (ReadBeatTime(value.c_str() + begin, value.size() - begin, asBeats)) {
    // The millisecond half is left where it was, which is `.delay`'s
    // arrangement: a beat time replaces the unit, and switching back with a
    // plain number is what rewrites the milliseconds.
    beats = asBeats;
    return true;
  }

  std::size_t end = begin;
  while (end < value.size() && !IsSelectorSeparator(value[end]))
    end++;
  float number = 0.f;
  if (!ReadNumericToken(value.c_str() + begin, end - begin, number)) return false;
  millis = MillisFromFloat(number);
  beats = 0.0;
  return true;
}

bool gRateLimitBase::Command(const std::string& value, std::size_t begin, std::size_t end) {
  const std::size_t length = end - begin;

  // Max's `clock` (issue #728), the shape `.delay` (#705) already has: "the
  // word clock, followed by the name of an existing setclock object ... the
  // word clock by itself sets the object back to using Max's regular
  // millisecond clock." The whole remainder is the name, so a clock named with
  // spaces still works.
  if (length == 5 && value.compare(begin, length, "clock", 5) == 0) {
    std::size_t nameBegin = end;
    while (nameBegin < value.size() && IsSelectorSeparator(value[nameBegin]))
      nameBegin++;
    std::size_t nameEnd = value.size();
    while (nameEnd > nameBegin && IsSelectorSeparator(value[nameEnd - 1]))
      nameEnd--;
    SetClock(value.c_str() + nameBegin, nameEnd - nameBegin);
    return true;
  }

  std::size_t argBegin = end;
  while (argBegin < value.size() && IsSelectorSeparator(value[argBegin]))
    argBegin++;

  // Max's `threshold` attribute: "time threshold under which only one message
  // may pass. Time can be specified in any of the time formats used in Max."
  if (length == 9 && value.compare(begin, length, "threshold", 9) == 0) {
    if (argBegin < value.size()) {
      int millis = (int)threshold.load();
      double beats = 0.0;
      // A `threshold` followed by something that is not a time leaves it where
      // it was rather than clearing it by accident.
      if (ReadTime(value, argBegin, millis, beats)) {
        threshold = millis;
        thresholdbeats = (Flt)beats;
      }
    }
    return true;
  }

  // Max's `quantize` attribute: "send output only on the specified
  // time-boundary if appropriate." A beat grid only — see the header on why
  // bars.beats.units cannot come in, and why a millisecond grid is not a grid.
  if (length == 8 && value.compare(begin, length, "quantize", 8) == 0) {
    if (argBegin >= value.size()) return true;
    double beats = 0.0;
    if (ReadBeatTime(value.c_str() + argBegin, value.size() - argBegin, beats)) {
      quantize = (Flt)beats;
      return true;
    }
    std::size_t argEnd = argBegin;
    while (argEnd < value.size() && !IsSelectorSeparator(value[argEnd]))
      argEnd++;
    float number = 0.f;
    // Of the plain numbers only 0 means anything, and it means "no grid".
    // Anything else is refused rather than read as some musical time it is not.
    if (ReadNumericToken(value.c_str() + argBegin, argEnd - argBegin, number) && number == 0.f) {
      quantize = 0.f;
    }
    return true;
  }

  return false;
}

BANG_IN(BangIn) {
  // Registered on inlet 0 only, which is where Max documents it: "performs the
  // same function as an anything message applied to the passing of bang
  // messages". A bang is a message like any other here — it is rate-limited and
  // comes back out as a bang.
  if (inlet != 0) return;
  Offer(Held::BANG, 0, 0.f, nullptr, thread);
}

INT_IN(IntIn) {
  if (inlet == 0) {
    Offer(Held::INT, value, 0.f, nullptr, thread);
    return;
  }
  // Max's right inlet: "the number is stored as the minimum amount of time, in
  // milliseconds, between successive outputs." That is a statement about the
  // *unit*, so a plain number puts the interval back on milliseconds whatever
  // tempo-relative time it was carrying (issue #728).
  interval = value;
  intervalbeats = 0.f;
}

FLOAT_IN(FloatIn) {
  if (inlet == 0) {
    Offer(Held::FLOAT, 0, value, nullptr, thread);
    return;
  }
  interval = MillisFromFloat(value);
  intervalbeats = 0.f;
}

LIST_IN(ListIn) {
  // The leading token, matched in place: a substr here would allocate on
  // whichever thread the message arrived on.
  std::size_t begin = 0;
  while (begin < value.size() && IsSelectorSeparator(value[begin]))
    begin++;
  std::size_t end = begin;
  while (end < value.size() && !IsSelectorSeparator(value[end]))
    end++;
  if (end <= begin) return;
  const std::size_t length = end - begin;

  if (inlet == 0) {
    // The four command words (issue #728). Max has the same collision — they
    // are attribute messages there — and everything they do not match is data.
    if (Command(value, begin, end)) return;

    // A message whose whole text is one number goes on as that number, an int or
    // a float by its spelling — the leading-token test `.bondo`, `.trigger` and
    // `.pipe` already use, applied here so a limited value reaches the same
    // inlets an unlimited one would have. Anything else travels whole, as text.
    std::size_t after = end;
    while (after < value.size() && IsSelectorSeparator(value[after]))
      after++;
    float number = 0.f;
    if (after == value.size() && ReadNumericToken(value.c_str() + begin, length, number)) {
      if (TokenLooksLikeFloat(value.c_str() + begin, length)) {
        Offer(Held::FLOAT, 0, number, nullptr, thread);
      } else {
        // ReadNumericToken only promised a finite float, so the token may still
        // be wider than an int: truncate through the range-checked conversion
        // rather than casting.
        Offer(Held::INT, ExprToInt(number), 0.f, nullptr, thread);
      }
      return;
    }

    Offer(Held::LIST, 0, 0.f, &value, thread);
    return;
  }

  // The interval inlet, and Max's whole time-value syntax on it since #728: a
  // note value or a tick count sets the interval in beats, a plain number sets
  // it in milliseconds and puts the object back on that unit. Anything else —
  // bars.beats.units, an unknown word — leaves the interval where it was rather
  // than being read as some number it is not.
  int millis = (int)interval.load();
  double beats = 0.0;
  if (!ReadTime(value, begin, millis, beats)) return;
  interval = millis;
  intervalbeats = (Flt)beats;
}

// ─── .speedlim — drop what arrives too soon ──────────────────────────────────

gSpeedlim::gSpeedlim() : gRateLimitBase() {
  Document(
      "Limits the rate of message throughput by **dropping** what arrives too soon. A message in "
      "the left inlet goes straight out the outlet if at least the interval has elapsed since the "
      "last one this object let out, and is discarded if it has not — nothing is stored and "
      "nothing is deferred, so what comes out of a burst is its *first* message, at the moment it "
      "arrived. That is the leading-edge throttle: 'redraw at most 20 times a second', 'one "
      "trigger per beat however hard the pad is hit', 'stop this sensor from flooding the patch'. "
      "Its sibling .qlim is the other answer to the same problem and holds the newest value "
      "instead of dropping it — reach for .qlim whenever losing the last value of a gesture would "
      "leave something set to the wrong number, and for this object whenever a skipped message "
      "costs nothing. Everything passes through as the kind of message it went in as: a bang out "
      "for a bang in, an int for an int, text for text, so the limiter can sit anywhere in a chain "
      "without retyping what flows through it. A number in the right inlet sets the interval in "
      "milliseconds; 0, the Max default, is no limiting at all and every message passes. The first "
      "message after creation always passes, there being no previous output to measure against. "
      "Elapsed time is measured on the patcher's block counter, the same clock the deferred-"
      "message scheduler waits on, so it stops when the engine does and its resolution is one "
      "audio block: two messages arriving in the same dispatch are zero milliseconds apart however "
      "far apart they really were. Since issue #728 the interval may instead be tempo-relative, "
      "Max's 'the time can be specified in milliseconds or using a tempo-relative interval': a "
      "note value ('4n', '4nd', '8nt') or a tick count ('1440 ticks') in the right inlet sets it "
      "in "
      "beats on the YSE domain clock named by 'clock <name>' in the left inlet, so the window then "
      "follows that domain's tempo changes and ramps and holds where it stands when the domain "
      "pauses. Max's other two time attributes came with it: 'threshold <time>' is his 'time "
      "threshold under which only one message may pass', a second window measured from the same "
      "output inside which a message is dropped outright, and 'quantize <time>' is his 'send "
      "output only on the specified time-boundary if appropriate', a beat grid that limits this "
      "object to at most one message per grid line. Both default to 0, which is off, so an object "
      "never sent either message behaves exactly as it did before. A beat time with no clock bound "
      "is inert rather than fatal — the window still opens, because a limiter that stopped opening "
      "would swallow the whole stream — and bars.beats.units stays out, needing a meter no domain "
      "clock has. Dropped messages are counted rather than logged, a log line "
      "being an allocation on whichever thread the message arrived on and that thread routinely "
      "being the audio callback. A standalone object outside any patcher has no clock at all and "
      "passes everything. Note that this is deliberately *not* Max's own speedlim/qlim split: Max "
      "documents the same holding behaviour for both names and separates them by scheduler "
      "priority — qlim being 'an interrupt safe replacement' for Jitter traffic — which is a "
      "distinction a headless patcher with one dispatch model cannot express and which would leave "
      "the two objects identical. Issue #508 gives the two names the two overflow policies "
      "instead, and Max's usurp attribute lives on .qlim alone because it says what happens to a "
      "message that is *waiting* and this object never has one. Calculate() does nothing and no "
      "message path allocates, locks or blocks.",
      "The message inlet. A bang, int, float or list arriving here goes straight out the outlet "
      "when the interval has elapsed since the last output, and is dropped and counted when it has "
      "not. Max's 'the message is passed out the outlet, provided that a certain minimum time has "
      "elapsed since the previous output' — with this object's answer to the other case, which is "
      "to discard it rather than hold it. A list whose whole text is one number passes as that "
      "number, an int or a float by its spelling, so a .m 5 reaches an int inlet downstream; "
      "anything else is carried whole as text. Three command words are matched here rather than "
      "carried (issue #728), which is where Max puts the same three as attribute messages: 'clock "
      "<name>' binds the domain clock a tempo-relative time is measured on and a bare 'clock' "
      "takes it away, 'threshold <time>' sets Max's threshold window, and 'quantize <time>' sets "
      "the beat grid output lands on ('quantize 0' clears it). Everything else is data.",
      "The messages that got through, unchanged and as the kind of message they went in as. "
      "Silent for anything dropped.",
      "The initial minimum time between outputs in milliseconds — Max's creation argument, whose "
      "documented default is 0: 'if there is no argument, the minimum time is 0 milliseconds', "
      "which limits nothing. The right inlet overwrites it afterwards. A negative value counts as "
      "0.");
}

void gSpeedlim::Blocked(Held kind, int intValue, float floatValue, const std::string* text,
                        const Wait& wait, YSE::THREAD thread) {
  // The whole object. There is nothing to store and nothing to arm: the message
  // arrived inside the window and is gone, counted on the drop counter so a host
  // can see how much of the stream this object is removing.
  (void)kind;
  (void)intValue;
  (void)floatValue;
  (void)text;
  (void)wait;
  (void)thread;
  CountDrop();
}

// ─── .qlim — hold the newest, or queue them all, and send on the window ──────

#undef className
#define className gQlim

gQlim::gQlim() : gRateLimitBase() {
  // The only allocation the object ever performs, and it happens here rather
  // than on an arrival: a held list is assigned into storage that is already
  // long enough, on whichever thread the message came in on.
  for (std::size_t s = 0; s < CAPACITY; s++)
    slots[s].text.reserve(TEXT_CAPACITY);

  ADD_PARAM(usurp);
  usurp = 1;

  Document(
      "Limits the rate of message throughput by **holding** what arrives too soon and sending it "
      "when the window opens. A message in the left inlet goes straight out the outlet if at least "
      "the interval has elapsed since the last one this object let out; if it has not, the message "
      "is kept and sent the moment the interval is up. What happens to a *second* message arriving "
      "while the first waits is Max's usurp attribute, and it is this object's one real switch. "
      "With 'usurp 1', the default, 'the most recently received message replaces any currently "
      "queued message', so what comes out of a burst is its *last* message, one interval after the "
      "previous output — which is what makes this the right object for anything whose value "
      "matters: a fader driving a filter cutoff ends up where the fader stopped rather than "
      "wherever the message that happened to fit the window left it. With 'usurp 0' (issue #728) "
      "'all messages received will be sent out': the object becomes a queue and every message "
      "leaves in turn, one per window, which is the right shape when the messages are events "
      "rather than samples of one value — notes to space out, cues to pace, a burst to replay at a "
      "rate something downstream can take. Up to 32 messages may wait; a queue that is full drops "
      "the newest and counts it rather than growing or discarding history a patch is relying on. "
      "Its sibling .speedlim is the other answer and drops what arrives too soon instead — "
      "cheaper, since it never defers anything, and correct whenever a skipped message costs "
      "nothing. Everything passes through as the kind of message it went in as: a bang out for a "
      "bang in, an int for an int, text for text. A number in the right inlet sets the interval in "
      "milliseconds; 0, the Max default, is no limiting at all and every message passes straight "
      "through without ever being held. The first message after creation always passes, there "
      "being no previous output to measure against. The wait runs on the patcher's deferred-"
      "message scheduler (issue #628) rather than on the timer thread behind .metro, so holding a "
      "message allocates nothing and takes no lock, and the released message arrives inside the "
      "patcher's own dispatch as one fresh logical event rather than as an unrelated stimulus. "
      "However long the queue is it costs one slot of that patcher-wide scheduler, because the "
      "release re-arms itself rather than arming a deadline per message. That clock is the block "
      "counter, so it stops when the engine does — a paused patch holds the waiting message where "
      "it stands — and its resolution is one audio block. Since issue #728 the interval may "
      "instead be tempo-relative: a note value ('4n', '4nd', '8nt') or a tick count ('1440 ticks') "
      "in the right inlet sets it in beats on the YSE domain clock named by 'clock <name>' in the "
      "left inlet, and the wait then follows that domain's tempo changes and ramps. Max's other "
      "two time attributes came with it: 'threshold <time>' is his 'time threshold under which "
      "only one message may pass', a second window inside which a message is dropped outright "
      "rather than held — which is also what keeps a usurp 0 queue from filling on a flood — and "
      "'quantize <time>' is his 'send output only on the specified time-boundary if appropriate', "
      "a beat grid the release lands on. Both default to 0, which is off. A beat time with no "
      "clock bound is inert rather than fatal, the window still opening, and bars.beats.units "
      "stays out because it needs a meter no domain clock has. A message that cannot be held — "
      "over-long text, a full queue, a full patcher-wide scheduler, or the narrow race with a "
      "delivery already in progress — is dropped and counted rather than sent early or written to "
      "the log. A standalone object outside any patcher has no clock at all and passes everything. "
      "Note that this is deliberately not a reproduction of Max's own speedlim/qlim split: Max "
      "documents this same holding behaviour for both names and separates them by scheduler "
      "priority — qlim being 'an interrupt safe replacement' for Jitter traffic — which a headless "
      "patcher with one dispatch model cannot express and which would leave the two objects "
      "identical. Issue #508 gives the two names the two overflow policies instead, and this one "
      "is Max's. Calculate() does nothing and no message path allocates, locks or blocks.",
      "The message inlet. A bang, int, float or list arriving here goes straight out the outlet "
      "when the interval has elapsed since the last output, and is held until it has when it has "
      "not — Max's 'otherwise, the message is held until that amount of time has passed (or until "
      "it is overwritten by another incoming message)'. With usurp on, only the newest waiting "
      "message survives; a message replaced that way is not counted as dropped, that being usurp "
      "working rather than a refusal. With 'usurp 0' every message joins a queue and leaves in "
      "turn, one per window. A list whose whole text is one number is carried as that number, an "
      "int or a float by its spelling, so a .m 5 reaches an int inlet downstream; anything else is "
      "carried whole as text, up to 256 characters, beyond which it cannot be held and is refused. "
      "Four command words are matched here rather than carried (issue #728), which is where Max "
      "puts the same four as attribute messages: 'clock <name>' binds the domain clock a "
      "tempo-relative time is measured on and a bare 'clock' takes it away, 'threshold <time>' "
      "sets Max's threshold window, 'quantize <time>' sets the beat grid a release lands on "
      "('quantize 0' clears it), and 'usurp 0' / 'usurp 1' choose between the queue and the "
      "replace. Everything else is data.",
      "The messages that got through, unchanged and as the kind of message they went in as. A held "
      "message arrives one interval after the previous output, inside the patcher's own dispatch.",
      "The initial minimum time between outputs in milliseconds — Max's creation argument, whose "
      "documented default is 0: 'if there is no argument, the minimum time is 0 milliseconds', "
      "which limits nothing and holds nothing. The right inlet overwrites it afterwards. A "
      "negative value counts as 0. A message already waiting keeps the deadline it was held with.");

  PARAM_DOC("usurp", "1",
            "Max's usurp attribute, and this object's one real switch (issue #728). 1, the "
            "default, is Max's 'the most recently received message replaces any currently queued "
            "message': exactly one message ever waits, and what comes out of a burst is its last "
            "value. 0 is Max's 'when usurp is disabled, all messages received will be sent out': "
            "the object becomes a queue of up to 32 messages that leave in arrival order, one per "
            "window, so nothing is lost until the queue itself is full. Anything non-zero counts "
            "as 1. The 'usurp <0|1>' message in the left inlet overwrites it afterwards; switching "
            "it does not disturb whatever is already waiting.",
            "0 or 1");
}

bool gQlim::Usurp() const {
  return usurp.load() != 0;
}

bool gQlim::Command(const std::string& value, std::size_t begin, std::size_t end) {
  // The three the base knows come first, so `clock`, `threshold` and
  // `quantize` mean the same thing on both objects.
  if (gRateLimitBase::Command(value, begin, end)) return true;

  const std::size_t length = end - begin;
  if (length != 5 || value.compare(begin, length, "usurp", 5) != 0) return false;

  std::size_t argBegin = end;
  while (argBegin < value.size() && IsSelectorSeparator(value[argBegin]))
    argBegin++;
  std::size_t argEnd = argBegin;
  while (argEnd < value.size() && !IsSelectorSeparator(value[argEnd]))
    argEnd++;

  // A bare `usurp` turns it on, which is what an attribute message with no
  // value does in Max and what the attribute's default already is.
  if (argEnd <= argBegin) {
    usurp = 1;
    return true;
  }
  float number = 0.f;
  // "When usurp is disabled, all messages received will be sent out." Anything
  // non-zero is Max's enabled; a word that is not a number leaves it alone.
  if (ReadNumericToken(value.c_str() + argBegin, argEnd - argBegin, number)) {
    usurp = number != 0.f ? 1 : 0;
  }
  return true;
}

std::size_t gQlim::Waiting() const {
  std::size_t count = 0;
  // seq_cst rather than acquire: this same walk is what a release does after
  // putting its armed flag down, and the total order is what stops an arrival
  // and that release from each deciding the other will arm. See the header.
  for (std::size_t s = 0; s < CAPACITY; s++)
    if (slots[s].state.load(std::memory_order_seq_cst) == STATE_ARMED) count++;
  return count;
}

void gQlim::Store(Slot& slot, Held kind, int intValue, float floatValue, const std::string* text) {
  slot.held = kind;
  slot.intValue = intValue;
  slot.floatValue = floatValue;
  // Into storage the constructor reserved, so holding text costs no allocation.
  if (kind == Held::LIST && text != nullptr) slot.text.assign(*text);
}

bool gQlim::Arm(const Wait& wait) {
  messageScheduler* scheduler = Scheduler();
  if (scheduler == nullptr) return false;
  // The tag is unused: this object has only one kind of pending message, and it
  // names no slot — the release pops whichever is oldest when it runs, which is
  // why nothing here needs a generation counter.
  const messageScheduler::Handle armedHandle =
      wait.binding != 0 ? scheduler->ScheduleBangOnClock(this, 0, wait.binding, wait.beats)
                        : scheduler->ScheduleBang(this, 0, wait.millis);
  return armedHandle != 0;
}

void gQlim::DropQueue() {
  for (std::size_t s = 0; s < CAPACITY; s++) {
    std::uint8_t state = STATE_ARMED;
    if (slots[s].state.compare_exchange_strong(state, STATE_FREE, std::memory_order_seq_cst,
                                               std::memory_order_relaxed)) {
      CountDrop();
    }
  }
}

void gQlim::EnsureArmed(const Wait& wait) {
  std::uint8_t idle = 0;
  if (!armed.compare_exchange_strong(idle, 1, std::memory_order_seq_cst,
                                     std::memory_order_seq_cst)) {
    // A release is already in flight, and it will take this message in its turn
    // — that is what makes one scheduler slot enough for a whole queue.
    return;
  }
  if (Arm(wait)) return;

  // The patcher-wide pending set is full, so nothing waiting has any deadline
  // to come out on. See the class notes on why they are dropped rather than
  // sent early.
  DropQueue();
  armed.store(0, std::memory_order_seq_cst);
  // One more pass now the flag is down: an arrival that published its slot and
  // then found the flag still up was told someone else would arm for it, and
  // that someone was this call. The seq_cst ordering is what makes its slot
  // visible here.
  DropQueue();
}

void gQlim::Blocked(Held kind, int intValue_, float floatValue_, const std::string* text_,
                    const Wait& wait, YSE::THREAD thread) {
  (void)thread;
  // Unreachable in practice — a standalone object never gets here, having no
  // clock and so no closed window — but a hold with nothing to arm it on would
  // be a message that never comes out.
  if (Scheduler() == nullptr) {
    CountDrop();
    return;
  }

  // Longer than a slot holds. Refused rather than truncated (a message this
  // object silently shortened would be a different message) and rather than
  // allocated for, since this may be the audio callback.
  if (kind == Held::LIST && (text_ == nullptr || text_->size() > TEXT_CAPACITY)) {
    CountDrop();
    return;
  }

  if (Usurp()) {
    // Max: "the most recently received message replaces any currently queued
    // message." The newest waiting slot is the one it replaces — the same slot
    // in the only state usurp can normally produce, which is exactly one.
    std::size_t newest = CAPACITY;
    std::uint64_t newestSeq = 0;
    for (std::size_t s = 0; s < CAPACITY; s++) {
      if (slots[s].state.load(std::memory_order_acquire) != STATE_ARMED) continue;
      const std::uint64_t seq = slots[s].seq.load(std::memory_order_relaxed);
      if (newest == CAPACITY || seq > newestSeq) {
        newest = s;
        newestSeq = seq;
      }
    }
    if (newest != CAPACITY) {
      std::uint8_t state = STATE_ARMED;
      if (!slots[newest].state.compare_exchange_strong(
              state, STATE_CLAIMED, std::memory_order_acq_rel, std::memory_order_relaxed)) {
        // A release took it out from under the usurp. Dropped rather than made
        // to spin on a path the audio callback takes.
        CountDrop();
        return;
      }
      Store(slots[newest], kind, intValue_, floatValue_, text_);
      slots[newest].state.store(STATE_ARMED, std::memory_order_release);
      // The deadline the replaced message was armed with is kept, which is what
      // makes the output land one interval after the *previous output* rather
      // than one interval after the last thing a flood happened to send.
      return;
    }
  }

  // A fresh hold, or — with usurp off — one more place in the queue.
  std::size_t index = CAPACITY;
  for (std::size_t s = 0; s < CAPACITY; s++) {
    std::uint8_t state = STATE_FREE;
    if (slots[s].state.compare_exchange_strong(state, STATE_CLAIMED, std::memory_order_acq_rel,
                                               std::memory_order_relaxed)) {
      index = s;
      break;
    }
  }
  if (index == CAPACITY) {
    // The queue is full, or every free slot was taken by another thread in the
    // walk. Either way this message has nowhere to wait.
    CountDrop();
    return;
  }

  // Written under CLAIMED, which no other thread will move: the payload is
  // never touched concurrently.
  Store(slots[index], kind, intValue_, floatValue_, text_);
  slots[index].seq.store(nextSeq.fetch_add(1, std::memory_order_relaxed),
                         std::memory_order_relaxed);
  slots[index].state.store(STATE_ARMED, std::memory_order_seq_cst);

  EnsureArmed(wait);
}

void gQlim::DeliverDeferred(const deferredMessage& msg, YSE::THREAD thread) {
  (void)msg;
  // The window has opened. The scheduler wraps this in a fresh
  // messageEventScope, so everything the released message goes on to cause is
  // one logical event (#628) — the causal chain the message came in on, resumed
  // rather than replaced, which is the whole reason this object does not use
  // TimerThread.
  //
  // Which message leaves is decided here rather than at arm time: the oldest
  // one waiting. That is what lets one scheduler message serve a whole queue,
  // and what makes a stale tag impossible — the tag names nothing.
  std::size_t oldest = CAPACITY;
  std::uint64_t oldestSeq = 0;
  for (std::size_t s = 0; s < CAPACITY; s++) {
    if (slots[s].state.load(std::memory_order_acquire) != STATE_ARMED) continue;
    const std::uint64_t seq = slots[s].seq.load(std::memory_order_relaxed);
    if (oldest == CAPACITY || seq < oldestSeq) {
      oldest = s;
      oldestSeq = seq;
    }
  }
  if (oldest != CAPACITY) {
    std::uint8_t state = STATE_ARMED;
    if (slots[oldest].state.compare_exchange_strong(state, STATE_FIRING, std::memory_order_acq_rel,
                                                    std::memory_order_relaxed)) {
      Slot& slot = slots[oldest];
      // The tag is passed straight through, as `.delay` and `.pipe` pass it:
      // T_GUI means "let the block's own traversal render what this caused",
      // which is the right reading for an outlet send.
      Emit(slot.held, slot.intValue, slot.floatValue, &slot.text, thread);
      // Freed only once the send has finished, so nothing can overwrite the
      // text the send is reading.
      slot.state.store(STATE_FREE, std::memory_order_release);
    }
  }

  // Whatever is still queued goes one window later — usurp 0's cadence, and a
  // no-op with usurp on, where the queue is never longer than one. The flag
  // stays up across the re-arm: exactly one release is in flight at any moment.
  if (Waiting() > 0) {
    if (Arm(ReopenWait())) return;
    DropQueue();
  }

  armed.store(0, std::memory_order_seq_cst);

  // The release has finished and the flag is down. An arrival that published a
  // slot while it was still up was told someone else would arm for it, and that
  // someone was this call — so look once more, now the total order makes its
  // slot visible.
  if (Waiting() == 0) return;
  std::uint8_t idle = 0;
  if (!armed.compare_exchange_strong(idle, 1, std::memory_order_seq_cst,
                                     std::memory_order_seq_cst)) {
    return;
  }
  if (Arm(ReopenWait())) return;
  DropQueue();
  armed.store(0, std::memory_order_seq_cst);
}
