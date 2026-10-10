#include "gLine.h"
#include "../math/gExprEval.h"
#include "../pListArgs.h"
#include "../pObjectList.hpp"
#include "../pSelector.h"
#include "timeValue.h"
#include <cmath>
#include <cstddef>
#include <cstdint>

using namespace YSE::PATCHER;

#define className gLineBase

namespace {

  // A float time as the milliseconds it means. Negatives and NaN become 0 — no
  // wait at all — and anything past the int range saturates rather than being
  // cast, since casting a float outside that range is undefined behaviour.
  // `.pipe`, `.qlim`, `.thresh` and `.delay` decide the same question the same
  // way.
  int MillisFromFloat(float value) {
    // Written as a failed `>` rather than `<=` so a NaN takes this branch too.
    if (!(value > 0.f)) return 0;
    if (value >= 2147483647.f) return 2147483647;
    return (int)value;
  }

  // A span as the object will travel it: negative and NaN spans are no distance
  // at all, which is Max's "the time is considered 0" and arrives immediately.
  double SpanFromFloat(float value) {
    const double span = (double)value;
    return span > 0.0 ? span : 0.0;
  }

  double AbsDouble(double value) {
    return value < 0.0 ? -value : value;
  }

} // namespace

gLineBase::gLineBase() : pObject(false) {
  ADD_IN_0;
  REG_INT_IN(IntIn);
  REG_FLOAT_IN(FloatIn);
  REG_LIST_IN(ListIn);

  // ANY rather than FLOAT: the output type is Max's, decided by the creation
  // argument's spelling and by the auto rule, and this patcher does no coercion
  // at an inlet — a ramp that always retyped its output to a float would stop it
  // from reaching the `.i` a patch wired it to.
  ADD_OUT_ANY;
  ADD_OUT_BANG;

  // The creation argument is kept as text because its *spelling* is what
  // decides the output type, so the value has to be read out of it afterwards —
  // which is what the parse callback is for. The clear callback is what makes
  // `SetParams("")` return the object to Max's no-argument shape (0, and an int
  // object) rather than leaving the previous argument's typing in place.
  REG_PARM_CLEAR;
  REG_PARM_PARSE;

  ADD_PARAM(initialArg);

  ADD_CATEGORY(pCategory::TIME);
}

void gLineBase::Document(const char* summary, const char* inletDoc, const char* valueOutDoc,
                         const char* initialParamDoc) {
  ADD_DESCRIPTION(summary);
  INLET_DOC(0, "in", inletDoc, "any");
  OUTLET_DOC(0, "out", valueOutDoc, "any number");
  OUTLET_DOC(1, "done",
             "A bang when the last target is reached. A ramp cut short by 'stop' sends none.", "");
  PARAM_DOC("initial", "0", initialParamDoc, "any number");
}

void gLineBase::ClearParams() {
  initialArg.clear();
  floatObject = false;
  current.store(0.0, std::memory_order_relaxed);
}

void gLineBase::ParseParams() {
  floatObject = false;
  float number = 0.f;
  // Strict on purpose: `ExprParseFloatList` would read `0abc` as 0, and an
  // argument that is not a number should leave Max's documented default rather
  // than half of itself. See the header on why the *spelling* is read too.
  if (!ReadNumericToken(initialArg, number)) {
    current.store(0.0, std::memory_order_relaxed);
    return;
  }
  floatObject = TokenLooksLikeFloat(initialArg);
  current.store((double)number, std::memory_order_relaxed);
}

void gLineBase::Send(double value, bool isFloat, YSE::THREAD thread) {
  if (isFloat) {
    outputs[0].SendFloat((float)value, thread);
    return;
  }
  // Truncated toward zero through the range-checked conversion rather than
  // cast: the value is a double and the outlet takes an int.
  outputs[0].SendInt(ExprToInt((float)value), thread);
}

bool gLineBase::BeginSegment() {
  if (nextPoint >= pointCount) {
    pointCount = 0;
    nextPoint = 0;
    pending.store(0, std::memory_order_relaxed);
    return false;
  }

  // Every segment starts from where the object stands, never from where the
  // previous segment was *supposed* to end: Max's "the new line starts from the
  // most recent output value, in order to avoid discontinuities".
  segFrom = current.load(std::memory_order_relaxed);
  segTo = points[nextPoint].target;
  segSpan = points[nextPoint].span;
  nextPoint++;
  pending.store(pointCount - nextPoint, std::memory_order_relaxed);

  // Max's output typing, decided once per segment. See the class notes: a float
  // object always emits floats, and an int object emits them for a segment too
  // small for an int to show.
  const double distance = AbsDouble(segTo - segFrom);
  segFloat = floatObject || (distance <= 1.0 && StepSize() < 0.4);

  // May collapse `segSpan` to 0 — a subclass with no clock to travel the
  // segment on says so here, and the caller then arrives immediately.
  OnSegmentBegin();
  return true;
}

void gLineBase::StartRamp(const float* pairs, std::size_t count, YSE::THREAD thread) {
  bool immediate = false;
  {
    storeGuard guard(busy);
    if (!guard.Held()) {
      // Another thread is inside the ramp. Refused rather than made to spin,
      // this being a path the audio callback takes.
      CountDrop();
      return;
    }

    for (std::size_t i = 0; i < count; i++) {
      points[i].target = (double)pairs[2 * i];
      points[i].span = SpanFromFloat(pairs[(2 * i) + 1]);
    }
    pointCount = count;
    nextPoint = 0;
    paused.store(false, std::memory_order_relaxed);

    if (!BeginSegment()) {
      running.store(false, std::memory_order_relaxed);
      OnRampStop();
      return;
    }
    running.store(true, std::memory_order_relaxed);
    // Max: "if a value is received in the left inlet without an accompanying
    // time value, it is sent out immediately." Sent by Run, outside the guard.
    immediate = segSpan <= 0.0;
  }

  if (immediate) Run(thread);
}

void gLineBase::Run(YSE::THREAD thread) {
  // Bounded by the queue: every iteration past the first has completed a
  // zero-span segment, and there are at most CAPACITY of those.
  for (std::size_t step = 0; step <= CAPACITY; step++) {
    double value = 0.0;
    bool isFloat = false;
    bool arrived = false;
    bool again = false;

    {
      storeGuard guard(busy);
      // A lost guard drops this step without re-arming, which is safe here for
      // `.metro`'s reason: the thread holding the ramp is a message handler,
      // and every one of them either arms a step of its own or stops the ramp.
      if (!guard.Held()) return;
      if (!running.load(std::memory_order_relaxed)) return;
      if (paused.load(std::memory_order_relaxed)) return;

      // A segment with no distance in time is already over; anything else asks
      // the subclass where its cursor now stands.
      double progress = (segSpan > 0.0) ? AdvanceCursor() : 1.0;
      // Also the NaN case, no comparison accepting one.
      if (!(progress > 0.0)) progress = 0.0;
      const bool done = progress >= 1.0;

      // The endpoint is the stored target rather than an interpolation of it,
      // so a ramp lands exactly where it was told to whatever the arithmetic
      // did on the way.
      value = done ? segTo : segFrom + ((segTo - segFrom) * progress);
      isFloat = segFloat;
      current.store(value, std::memory_order_relaxed);

      if (done) {
        if (BeginSegment()) {
          // The next segment may have no distance in time either, in which
          // case it arrives on this same message rather than one grain later.
          again = segSpan <= 0.0;
        } else {
          running.store(false, std::memory_order_relaxed);
          OnRampStop();
          arrived = true;
        }
      }
    }

    // Outside the guard, always: an outlet wired back into this object's own
    // inlet must find the ramp consistent rather than half-written.
    Send(value, isFloat, thread);
    if (arrived) outputs[1].SendBang(thread);
    if (!again) return;
  }
}

void gLineBase::Halt() {
  running.store(false, std::memory_order_relaxed);
  paused.store(false, std::memory_order_relaxed);
  pointCount = 0;
  nextPoint = 0;
  segSpan = 0.0;
  pending.store(0, std::memory_order_relaxed);
  OnRampStop();
}

void gLineBase::Pause() {
  storeGuard guard(busy);
  if (!guard.Held()) {
    CountDrop();
    return;
  }
  if (!running.load(std::memory_order_relaxed)) return;
  if (paused.load(std::memory_order_relaxed)) return;

  // The segment is rewritten as what is left of it, measured from where the
  // object now stands, so a resume travels the remainder rather than restarting
  // the whole thing.
  segFrom = current.load(std::memory_order_relaxed);
  OnPause();
  paused.store(true, std::memory_order_relaxed);
}

void gLineBase::Resume(YSE::THREAD thread) {
  bool immediate = false;
  {
    storeGuard guard(busy);
    if (!guard.Held()) {
      CountDrop();
      return;
    }
    if (!running.load(std::memory_order_relaxed)) return;
    if (!paused.load(std::memory_order_relaxed)) return;

    paused.store(false, std::memory_order_relaxed);
    // Rebase and arm, exactly as a fresh segment does — what is left of the
    // segment is a segment.
    OnSegmentBegin();
    immediate = segSpan <= 0.0;
  }
  if (immediate) Run(thread);
}

bool gLineBase::Command(const std::string& value, std::size_t begin, std::size_t end, YSE::THREAD) {
  const std::size_t length = end - begin;

  // Max: "stops line from sending out numbers, until a new target value is
  // received." Frozen where it stands, with the queue forgotten, which is
  // `~line`'s convention too — and no arrival bang, the ramp not having
  // arrived.
  if (length == 4 && value.compare(begin, length, "stop", 4) == 0) {
    storeGuard guard(busy);
    if (guard.Held())
      Halt();
    else
      CountDrop();
    return true;
  }

  // Max: "the word set, followed by a number, makes that number the new
  // starting value from which to proceed to the next received target value. The
  // set message also stops line if it is in the process of sending out
  // numbers." Silent — a `set` is a move, not an output.
  if (length == 3 && value.compare(begin, length, "set", 3) == 0) {
    float number = 0.f;
    const int read = ExprParseFloatList(value.c_str() + end, &number, 1);
    storeGuard guard(busy);
    if (!guard.Held()) {
      CountDrop();
      return true;
    }
    Halt();
    if (read > 0) current.store((double)number, std::memory_order_relaxed);
    return true;
  }

  return false;
}

void gLineBase::Target(double value, YSE::THREAD thread) {
  const float pair[2] = {(float)value, (float)TakeSpan()};
  StartRamp(pair, 1, thread);
}

void gLineBase::TakeList(const float* numbers, int count, YSE::THREAD thread) {
  if (count <= 0) return;

  // One number is a bare target, travelled over whatever time was set since the
  // last one.
  if (count == 1) {
    Target((double)numbers[0], thread);
    return;
  }

  // Max: "the first number specifies a target value ... and the second number
  // specifies a total amount of time in which line should reach the target
  // value."
  if (count == 2) {
    StartRamp(numbers, 1, thread);
    return;
  }

  // Max: "the third number, which is optional, sets the grain." Only for an
  // object that has one — `.bline` reads three numbers as a pair with a
  // trailing element ignored, which is the rule below.
  if (count == 3 && TakeGrain((double)numbers[2])) {
    StartRamp(numbers, 1, thread);
    return;
  }

  // Max: "if the list has an even number of elements greater than three, each
  // pair of elements is considered a destination-ramptime pair in a breakpoint
  // function. If the list has an odd number of elements greater than three, the
  // last element will be ignored."
  std::size_t pairs = (std::size_t)(count / 2);
  if (pairs > CAPACITY) {
    // Truncated rather than allocated for: the arriving thread is routinely the
    // audio callback. The ramp keeps its head and loses its tail, which is the
    // failure that costs a patch least.
    pairs = CAPACITY;
    CountDrop();
  }
  StartRamp(numbers, pairs, thread);
}

INT_IN(IntIn) {
  if (inlet == 0) {
    Target((double)value, thread);
    return;
  }
  SetTime(inlet, value);
}

FLOAT_IN(FloatIn) {
  if (inlet == 0) {
    Target((double)value, thread);
    return;
  }
  SetTime(inlet, MillisFromFloat(value));
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

  if (inlet == 0) {
    if (Command(value, begin, end, thread)) return;

    // Read into a stack array, so a breakpoint list of any length allocates
    // nothing. Tokens that are not numbers are skipped, which is what makes a
    // stray word in a list harmless rather than fatal. One slot past Max's
    // maxpoints, purely so an over-long list can be *noticed* rather than
    // silently cut by the reader's own cap.
    float numbers[MAX_POINTS + 1];
    int count = ExprParseFloatList(value.c_str() + begin, numbers, MAX_POINTS + 1);
    if (count > MAX_POINTS) {
      count = MAX_POINTS;
      CountDrop();
    }
    TakeList(numbers, count, thread);
    return;
  }

  // A time inlet. Max's tempo-relative syntax is read *first* and refused as a
  // whole, because its tick spelling starts with a number: `1440 ticks` taken
  // for its leading token would silently become 1440 ms, which is the mistake
  // `.clocker` made before #725. This object has no clock to measure a beat
  // against, so the honest answer is to leave the time where it was.
  double beats = 0.0;
  if (ReadBeatTime(value.c_str() + begin, value.size() - begin, beats)) return;

  float number = 0.f;
  if (!ReadNumericToken(value.c_str() + begin, end - begin, number)) return;
  SetTime(inlet, MillisFromFloat(number));
}

// ─── .line — the cursor is the block clock ───────────────────────────────────

#undef className
#define className gLine

gLine::gLine() : gLineBase() {
  ADD_IN_1;
  REG_INT_IN(IntIn);
  REG_FLOAT_IN(FloatIn);
  REG_LIST_IN(ListIn);

  ADD_IN_2;
  REG_INT_IN(IntIn);
  REG_FLOAT_IN(FloatIn);
  REG_LIST_IN(ListIn);

  ADD_PARAM(grain);

  grain = DEFAULT_GRAIN;

  Document(
      "Ramps a number to a target over a given time, sending a value every grain (20 ms by "
      "default) and a bang from the right outlet on arrival. A list of target/time pairs makes a "
      "ramp of several segments.",
      "A target, '<target> <time> [<grain>]', or target/time pairs. 'stop', 'pause', 'resume' "
      "and 'set <value>' control the ramp.",
      "The ramp, one value per grain, ending exactly on the target. An int or a float: see the "
      "notes.",
      "The starting value. Written as a float ('0.') it makes the object send floats.");

  INLET_DOC(1, "time",
            "The time in milliseconds the next target takes. It is used by one target only, then "
            "goes back to 0.",
            "0+ ms");
  INLET_DOC(2, "grain",
            "The time in milliseconds between values. Below 1 it counts as 20. It stays set.",
            "1+ ms");

  PARAM_DOC("grain", "20", "The time in milliseconds between values. Below 1 it counts as 20.",
            "1+ ms");
}

int gLine::Grain() const {
  const Int ms = grain.load();
  // Max: "the minimum grain allowed is 1 millisecond; any number less than 1
  // will be set to 20." Applied on read rather than on write, which is
  // `.metro`'s and `.pipe`'s arrangement and for their reason — a live
  // SetParams re-parse stores straight into the field from the audio thread and
  // notifies nobody, so a clamp applied at the inlet would not cover that route.
  return ms >= 1 ? (int)ms : DEFAULT_GRAIN;
}

double gLine::TakeSpan() {
  // Consumed: Max's "if no time has been specified since the last target value,
  // the time is considered 0".
  return (double)ramptime.exchange(0, std::memory_order_relaxed);
}

bool gLine::TakeGrain(double value) {
  grain = MillisFromFloat((float)value);
  return true;
}

void gLine::SetTime(int inlet, int millis) {
  if (inlet == 1) {
    ramptime.store(millis > 0 ? millis : 0, std::memory_order_relaxed);
    return;
  }
  if (inlet == 2) grain = millis;
}

bool gLine::Command(const std::string& value, std::size_t begin, std::size_t end,
                    YSE::THREAD thread) {
  const std::size_t length = end - begin;

  // Max: "pauses the internal ramp but does not change the target value nor
  // clear pending target-time pairs."
  if (length == 5 && value.compare(begin, length, "pause", 5) == 0) {
    Pause();
    return true;
  }
  // Max: "resumes the internal ramp and subsequent pending target-time pairs if
  // the line object was paused as a result of the pause message."
  if (length == 6 && value.compare(begin, length, "resume", 6) == 0) {
    Resume(thread);
    return true;
  }

  return gLineBase::Command(value, begin, end, thread);
}

int gLine::Elapsed() const {
  const messageScheduler* scheduler = Scheduler();
  if (scheduler == nullptr) return 0;
  const std::uint64_t now = scheduler->Now();
  // The comparison is defensive: the block clock is monotonic, but a segment
  // begun before the patcher started would otherwise read its age backwards.
  return messageScheduler::MillisForBlocks(now > segBlock ? now - segBlock : 0);
}

bool gLine::Arm(int delayMs) {
  messageScheduler* scheduler = Scheduler();
  if (scheduler == nullptr) return false;

  // Whatever was armed before is retired first, so a step cannot be waiting
  // twice over and spend two slots of the patcher-wide budget on one segment.
  const messageScheduler::Handle previous = handle.exchange(0, std::memory_order_relaxed);
  if (previous != 0) scheduler->Cancel(previous);

  // The tag names the segment, not the slot: a step that comes due for a
  // segment already finished finds a generation that has moved on and does
  // nothing.
  const int tag = (int)(generation.load(std::memory_order_acquire) & TAG_MASK);
  const messageScheduler::Handle armed = scheduler->ScheduleBang(this, tag, delayMs);
  if (armed == 0) return false;
  handle.store(armed, std::memory_order_relaxed);
  return true;
}

void gLine::Cancel() {
  // The bump is what makes a pending step harmless; the cancel is best effort
  // on top of it, purely to hand the patcher-wide budget back early.
  generation.fetch_add(1, std::memory_order_acq_rel);
  const messageScheduler::Handle armed = handle.exchange(0, std::memory_order_relaxed);
  if (armed == 0) return;
  messageScheduler* scheduler = Scheduler();
  if (scheduler != nullptr) scheduler->Cancel(armed);
}

void gLine::OnSegmentBegin() {
  Cancel();
  if (segSpan <= 0.0) return;

  messageScheduler* scheduler = Scheduler();
  if (scheduler == nullptr) {
    // A standalone object has no patcher and so no clock: "over 500 ms" has no
    // referent, and the only alternatives are *now* or *never*. Arriving now is
    // `.pipe`'s and `.thresh`'s answer to the same dead end, and the one that
    // keeps a standalone object testable rather than a black hole.
    segSpan = 0.0;
    return;
  }

  segBlock = scheduler->Now();
  // The first step is one grain in, unless the whole segment is shorter than a
  // grain — in which case it is the arrival.
  double first = (double)Grain();
  if (first > segSpan) first = segSpan;
  if (Arm((int)first)) return;

  // The patcher-wide pending set is full, so this segment has no way to be
  // travelled. Arriving now is the least-bad answer: the target is reached,
  // everything downstream ends up consistent, and the arrival bang still fires
  // — where leaving the segment armed on nothing would strand the ramp forever.
  CountDrop();
  segSpan = 0.0;
}

void gLine::OnRampStop() {
  Cancel();
}

void gLine::OnPause() {
  // What is left of the segment becomes the segment; `segFrom` has already been
  // moved to where the ramp stands, so a resume travels the remainder at the
  // same speed.
  const double elapsed = (double)Elapsed();
  segSpan -= elapsed;
  if (!(segSpan > 0.0)) segSpan = 0.0;
  Cancel();
}

double gLine::StepSize() const {
  const double distance = segTo > segFrom ? segTo - segFrom : segFrom - segTo;
  if (!(segSpan > 0.0)) return distance;
  return distance * (double)Grain() / segSpan;
}

double gLine::AdvanceCursor() {
  const messageScheduler* scheduler = Scheduler();
  if (scheduler == nullptr) return 1.0;

  const double elapsed = (double)Elapsed();
  if (elapsed >= segSpan) return 1.0;

  // The *absolute* next grain boundary, not one grain from here. A delivery
  // lands at the first audio block at or after its deadline, so asking for a
  // fixed grain every time would hand back that overshoot on every step and run
  // the ramp long. Asking for the distance to the boundary instead absorbs it —
  // `.metro`'s rule, and for `.metro`'s reason.
  const double grainMs = (double)Grain();
  double next = (std::floor(elapsed / grainMs) + 1.0) * grainMs;
  // The last step is the arrival, so it lands on the end of the segment rather
  // than past it. Max notes his own line arrives "in just under the amount of
  // time specified (time minus grain)"; this one arrives on time.
  if (next > segSpan) next = segSpan;
  double wait = next - elapsed;
  if (!(wait > 0.0)) wait = 0.0;

  if (Arm((int)wait)) return elapsed / segSpan;

  // The scheduler is full and there is no way to take another step. The target
  // is reached now and the shortened ramp counted, for OnSegmentBegin's reason.
  CountDrop();
  return 1.0;
}

void gLine::DeliverDeferred(const deferredMessage& msg, YSE::THREAD thread) {
  // A step has come due. The scheduler wraps this in a fresh messageEventScope,
  // so everything the emitted value goes on to cause is one logical event
  // (#628) — the causal chain the ramp was started on, resumed rather than
  // replaced, which is the whole reason this object does not use TimerThread.
  if ((std::uint32_t)msg.tag != (generation.load(std::memory_order_acquire) & TAG_MASK)) return;
  handle.store(0, std::memory_order_relaxed);
  Run(thread);
}

// ─── .bline — the cursor is the bang ─────────────────────────────────────────

#undef className
#define className gBline

gBline::gBline() : gLineBase() {
  // The one inlet the base already made, given a bang method: this object's
  // whole timebase. No time inlet and no grain inlet, there being no clock to
  // set — and so, unlike every other object in this directory, no scheduler
  // either.
  REG_BANG_IN(BangIn);

  Document(
      "Ramps a number to a target one step per bang, so whatever bangs it sets the pace. A list "
      "of target/count pairs makes a ramp of several segments; the right outlet bangs when the "
      "last target is reached.",
      "A bang sends the next step. A target, '<target> <bangs>', or target/bangs pairs set the "
      "ramp. 'stop' and 'set <value>' control it.",
      "The ramp, one value per bang, ending exactly on the target. An int or a float, as on "
      ".line.",
      "The starting value. Written as a float ('0.') it makes the object send floats.");
}

void gBline::OnSegmentBegin() {
  steps = 0.0;

  // The floor that makes this object bang-driven rather than instant. `.line`
  // reaches here and may collapse `segSpan` to 0 — its dead end is having no
  // clock, and its answer is *now*. This one goes the other way: a segment with
  // no bangs in it is one bang long, so Max's "the time is considered 0 and
  // bline outputs the target value when it receives a bang" holds for a bare
  // number, for a pair counted 0, and for a negative count alike. With
  // `segSpan` never 0, neither `StartRamp` nor `Run` ever takes its
  // emit-immediately branch, which is the whole of "nothing comes out except on
  // a bang".
  if (!(segSpan >= 1.0)) segSpan = 1.0;
}

double gBline::StepSize() const {
  // Called from BeginSegment *before* OnSegmentBegin has applied the floor, so
  // a bare target is still spanless here — and a one-bang segment's step is the
  // whole distance either way, which is what the fallback returns.
  const double distance = segTo > segFrom ? segTo - segFrom : segFrom - segTo;
  if (!(segSpan > 0.0)) return distance;
  return distance / segSpan;
}

double gBline::AdvanceCursor() {
  // The cursor is the bang count, and that is the entire clock. A fractional
  // span is travelled in whole bangs and so arrives on the first one at or past
  // the target.
  steps += 1.0;
  return steps / segSpan;
}

BANG_IN(BangIn) {
  if (inlet != 0) return;
  // Max: "sends a new step in the breakpoint list out the left outlet. If the
  // current list of ramp segments is finished, a bang message will be sent out
  // the right outlet." Run does both, and does nothing when no ramp is running.
  Run(thread);
}
