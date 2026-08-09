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
  OUTLET_DOC(
      1, "done",
      "A bang the moment the last segment reaches its target — Max's 'when line has arrived "
      "at its target value, bang is sent out'. It is what chains ramps together: wire it to "
      "whatever should start once this one has finished. A ramp cut short by 'stop' does not "
      "bang, not having arrived; a target sent with no time does, having arrived "
      "immediately.",
      "");
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
      "Ramps a control value toward a target over a given time, emitting a number every 'grain' "
      "milliseconds and banging the right outlet on arrival. Max's line, 'generate ramps and line "
      "segments from one value to another within a specified amount of time' — and the "
      "control-rate counterpart of the audio-rate ~line this patcher already had. The difference "
      "is the whole point: ~line writes a DSP buffer and can only be read by an audio inlet, so "
      "anything that is a number rather than a waveform — a .metro interval, a MIDI velocity, a "
      "mix weight, a filter cutoff behind a .mtof — needs this one. A number in the left inlet is "
      "the target, the middle inlet is how long to take getting there, and the right inlet is the "
      "grain. The ramp time is consumed by the target that uses it, which is Max's rule and the "
      "one that surprises people: 'if no time has been specified since the last target value, the "
      "time is considered 0 and line immediately outputs the target value', so a patch that wants "
      "every target ramped sends the pair as a list, 'target time'. A three-element list adds the "
      "grain, which unlike the time persists once set. Four or more numbers are a breakpoint "
      "function of target/time pairs — '1 1000 0 1000' climbs to 1 over a second and comes back "
      "down over the next — with an odd trailing element ignored, as in Max; each segment starts "
      "from the value the object currently stands at, so there are no discontinuities, and a new "
      "target arriving mid-ramp clears every segment still to come. The list holds 64 segments and "
      "reads 129 numbers, which is Max's maxpoints default; anything longer keeps its head, loses "
      "its tail and counts the refusal rather than allocating on the thread the message arrived "
      "on. Max's four words are all here on the left inlet: 'stop' freezes at the current value "
      "and forgets the queue without banging, 'pause' and 'resume' hold and release a ramp with "
      "its queue intact, and 'set <number>' moves the stored value silently and stops a ramp in "
      "progress. The output is an int or a float by Max's rule — the creation argument's spelling "
      "makes the object one or the other, and an int object emits floats for a segment shorter "
      "than one unit whose steps are smaller than 0.4, which is his floatoutput 'auto' default. "
      "The wait runs on the patcher's deferred-message scheduler (issue #628) rather than on the "
      "timer thread behind .metro, so arming a step allocates nothing and takes no lock, and every "
      "emitted value arrives inside the patcher's own dispatch as one logical event caused by the "
      "message that started the ramp. That clock is the block counter, so it stops when the engine "
      "does — a paused patch holds a ramp where it stands — and its resolution is one audio block, "
      "so a grain shorter than a block emits one value per block; a grain below Max's 1 ms minimum "
      "reads as his default 20. Each value is computed from the time elapsed since the segment "
      "began rather than accumulated step by step, and each step is armed at the absolute next "
      "grain boundary, so a late wakeup skips a value instead of running the ramp long and a "
      "segment lands on its target on time. Milliseconds are the only unit — a note value or tick "
      "count in a time inlet is refused rather than misread as milliseconds — and Max's 'clock' "
      "message, which would hand the object to a setclock, is not ported. Calculate() does nothing "
      "and no message path allocates, locks or blocks.",
      "The target inlet, and where every message word goes. An int or a float is a target value, "
      "reached over whatever ramp time the middle inlet was last given — and immediately, Max's "
      "'time is considered 0', when none has been set since the previous target. A list of two "
      "numbers is a target and its time; three adds the grain; four or more are breakpoint "
      "target/time pairs travelled one after another, with an odd trailing element ignored. "
      "Whatever arrives clears the segments still queued and starts from the value the object "
      "stands at now, so a ramp redirected mid-flight has no jump in it. The word 'stop' freezes "
      "the ramp where it is and forgets the queue, without a bang on the right outlet; 'pause' "
      "holds it with the queue intact and 'resume' travels what was left of the segment; 'set' "
      "followed by a number moves the stored value without emitting anything and stops a ramp in "
      "progress. There is no bang method — Max's line has none, and the bang-driven ramp is the "
      "sibling object .bline.",
      "The ramp, one value every grain milliseconds from where the object stood to the target, "
      "ending on the target exactly. An int or a float by Max's typing rule: the object is a float "
      "one when its creation argument was spelled as a float, and an int object still emits floats "
      "for a segment that covers one unit or less in steps smaller than 0.4, since a ramp from 0 "
      "to 1 in ints is not a ramp.",
      "The value the object starts at, and — by its spelling — the output type, which is Max's "
      "'an argument may be used to set the initial value to be stored in line and the output type "
      "for the object'. An argument written as a float ('0.', '1.5') makes a float object that "
      "always emits floats; one written as an int, or no argument at all, makes an int object, "
      "which is Max's 'if there is no argument, the initial value is 0 and the output type is "
      "int'. An argument that is not a number leaves both at that default. The stored value moves "
      "as the object ramps and with every 'set'; it is run-time state and is not saved.");

  INLET_DOC(
      1, "time",
      "Sets how long the next target takes to reach, in milliseconds — Max's 'the number is "
      "the time, in milliseconds, in which to arrive at the target value'. It is consumed by "
      "the target that uses it and falls back to 0 afterwards, so a single time followed by "
      "two targets ramps to the first and jumps to the second; send 'target time' as a list "
      "in the left inlet to ramp every one. Ints, floats and a list whose leading token is a "
      "number all set it; a negative or NaN time counts as 0, which arrives immediately. Max's "
      "tempo-relative time syntax is not read here — this object has no clock to measure a "
      "beat against — so a note value ('4nd') or tick count ('1440 ticks') is refused rather "
      "than misread as the milliseconds it is not. A ramp already running keeps the time it "
      "started with; this reaches the next target.",
      "0+ ms");
  INLET_DOC(2, "grain",
            "Sets the interval at which intermediate values are sent out, in milliseconds — Max's "
            "'the number is the interval at which intermediary numbers are regularly sent out'. "
            "Unlike the ramp time this persists: 'once grains are set in a list, they will "
            "override the default until manually reset'. Max's minimum is 1 ms and anything below "
            "it reads as his default of 20, which is also what an object with no second creation "
            "argument has. The block clock is the floor in practice — a grain shorter than one "
            "audio block emits one value per block — and a grain longer than the ramp itself still "
            "gives the arrival value, the last step being clamped to the end of the segment. Ints, "
            "floats and a list whose leading token is a number all set it; a note value or tick "
            "count is refused rather than misread as milliseconds. A segment already being "
            "travelled keeps the grain it began with.",
            "1+ ms");

  PARAM_DOC("grain", "20",
            "The initial grain in milliseconds — Max's second creation argument, whose documented "
            "default is 20: 'if the grain is not specified, line outputs a number every 20 "
            "milliseconds'. Max's minimum is 1 ms and 'any number less than 1 will be set to 20', "
            "which is applied here on every read, so a saved 0 behaves as 20 rather than as a "
            "value per block. The right inlet and a three-element list overwrite it afterwards.",
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
