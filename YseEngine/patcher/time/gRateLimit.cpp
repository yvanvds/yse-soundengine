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
      "Sets the minimum time between outputs, in milliseconds or as a beat time. 0 lets "
      "everything through.";

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
  PARAM_DOC("intervalbeats", "0",
            "The interval in beats on the bound clock. 0 uses the millisecond interval instead. A "
            "plain number in the right inlet sets it back to 0.",
            "0+ beats");
  PARAM_DOC("threshold", "0",
            "A window in milliseconds after each output in which every message is dropped. 0 is "
            "off.",
            "0+ ms");
  PARAM_DOC("thresholdbeats", "0",
            "The threshold in beats on the bound clock. 0 uses the millisecond threshold instead.",
            "0+ beats");
  PARAM_DOC("quantize", "0",
            "A beat grid that output lands on, at most one message per grid line. 0 is off. Needs "
            "a bound clock.",
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
      "Limits how often messages pass. A message that arrives before the interval since the last "
      "output is up is dropped, so a burst comes out as its first message.",
      "Any message, passed or dropped. 'clock <name>', 'threshold <time>' and 'quantize <time>' "
      "are settings, not data.",
      "The messages that got through, unchanged.",
      "The minimum time between outputs in milliseconds. 0 lets everything through.");
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
      "Limits how often messages pass. A message that arrives before the interval since the last "
      "output is up is held and sent when it is up; a newer message replaces it, or with 'usurp "
      "0' joins a queue.",
      "Any message, passed or held. 'clock <name>', 'threshold <time>', 'quantize <time>' and "
      "'usurp <0|1>' are settings, not data.",
      "The messages that got through, unchanged. A held message goes out one interval after the "
      "previous output.",
      "The minimum time between outputs in milliseconds. 0 lets everything through.");

  PARAM_DOC("usurp", "1",
            "1: a new message replaces the one waiting. 0: messages queue, up to 32, and go out "
            "one per interval.",
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
