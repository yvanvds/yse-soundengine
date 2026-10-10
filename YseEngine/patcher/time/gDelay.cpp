#include "gDelay.h"
#include "../pListArgs.h"
#include "../pObjectList.hpp"
#include "../pSelector.h"
#include "timeValue.h"
#include <cstddef>

using namespace YSE::PATCHER;

#define className gDelay

namespace {

  // A float delay time as the milliseconds it means. Negatives and NaN become
  // 0 — the shortest wait there is, which the scheduler's one-block floor then
  // turns into "next block" — and anything past the int range saturates rather
  // than being cast, since casting a float outside that range is undefined
  // behaviour. `.mtr` and `.seq` decide the same question the same way.
  int MillisFromFloat(float value) {
    // Written as a failed `>` rather than `<=` so a NaN takes this branch too.
    if (!(value > 0.f)) return 0;
    if (value >= 2147483647.f) return 2147483647;
    return (int)value;
  }

  constexpr char kHotInletDoc[] =
      "A bang starts the wait, forgetting a bang already waiting. A number or a beat time sets "
      "the time and starts the wait. 'stop' cancels the waiting bang; 'clock <name>' sets the "
      "clock for beat times.";

  constexpr char kColdInletDoc[] =
      "Sets the time for the next bang, in milliseconds or as a beat time. A bang already "
      "waiting keeps its time.";

} // namespace

CONSTRUCT() {
  ADD_IN_0;
  REG_BANG_IN(BangIn);
  REG_INT_IN(IntIn);
  REG_FLOAT_IN(FloatIn);
  REG_LIST_IN(ListIn);

  ADD_IN_1;
  REG_INT_IN(IntIn);
  REG_FLOAT_IN(FloatIn);
  REG_LIST_IN(ListIn);

  ADD_OUT_BANG;

  ADD_PARAM(delaytime);
  ADD_PARAM(delaybeats);
  delaytime = DEFAULT_DELAY;
  delaybeats = 0.f;

  ADD_DESCRIPTION(
      "Sends a bang a set time after it receives one: 5 ms by default, or a beat time on a "
      "domain clock. It holds one bang at a time; a new bang restarts the wait and 'stop' "
      "cancels it.");
  ADD_CATEGORY(pCategory::TIME);
  INLET_DOC(0, "bang", kHotInletDoc, "bang, int, float, list, 'stop', 'clock <name>'");
  INLET_DOC(1, "time", kColdInletDoc, "0+ ms, or a note value / tick count");
  OUTLET_DOC(0, "out", "The delayed bang.", "");
  PARAM_DOC("delaytime", "5", "The delay in milliseconds. A negative value counts as 0.", "0+ ms");
  PARAM_DOC("delaybeats", "0",
            "The delay in beats on the bound clock. 0 uses the millisecond delay instead. A plain "
            "number in either inlet sets it back to 0.",
            "0+ beats");
}

int gDelay::DelayTime() const {
  const Int ms = delaytime.load();
  return ms > 0 ? (int)ms : 0;
}

double gDelay::DelayBeats() const {
  const Flt beats = delaybeats.load();
  // Written as a failed `>` so a NaN — which a live SetParams re-parse could
  // store — reads as "no beat time" rather than as a wait nothing can satisfy.
  return beats > 0.f ? (double)beats : 0.0;
}

const char* gDelay::ClockName() const {
  const clockBridge::Handle bound = binding.load(std::memory_order_relaxed);
  if (bound == 0) return "";
  const clockBridge* clocks = Clocks();
  if (clocks == nullptr) return "";
  return clocks->NameOf(bound);
}

void gDelay::SetClock(const char* name, std::size_t length) {
  // Max's bare `clock`: "the word clock by itself sets the delay object back to
  // using Max's regular millisecond clock." A bang already in flight is left
  // alone — it leaves on the clock it was armed on, the way the cold inlet
  // leaves a bang in flight at the time it was armed with.
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

bool gDelay::IsPending() const {
  return pending.load(std::memory_order_relaxed) != 0;
}

void gDelay::CancelPending() {
  const messageScheduler::Handle held = pending.exchange(0, std::memory_order_relaxed);
  if (held == 0) return;
  messageScheduler* scheduler = Scheduler();
  if (scheduler != nullptr) scheduler->Cancel(held);
}

void gDelay::Start(YSE::THREAD thread) {
  messageScheduler* scheduler = Scheduler();
  if (scheduler == nullptr) {
    // A standalone object has no patcher and so no clock at all: "later" has no
    // referent here, and the only alternatives are now or never. `.bondo`'s
    // answer to the same dead end.
    outputs[0].SendBang(thread);
    return;
  }

  // Max: "only one bang at a time can be delayed by delay ... the first bang is
  // forgotten." Cancel-then-arm, both wait-free, so the wait is always measured
  // from the newest bang.
  CancelPending();

  // Milliseconds or beats — the unit travels with the value, which is Max's
  // model: a plain number says "milliseconds" and a note value says "beats"
  // (issue #705). `clock` only decides which clock the beats are counted on.
  const double beats = DelayBeats();
  messageScheduler::Handle armed = 0;
  if (beats > 0.0) {
    const clockBridge::Handle onClock = binding.load(std::memory_order_relaxed);
    // A tempo-relative wait with no clock bound has nothing to be measured
    // against — this patcher has no transport for a note value to fall back on
    // — so nothing is armed and nothing goes out. Re-reading the beat count as
    // milliseconds would turn a `4n` into a wait of 1 ms; the honest answer is
    // the one the bridge gives for a clock that does not exist, which is that
    // the wait never comes due. A later `clock <name>` makes the next bang work.
    if (onClock == 0) return;
    // The tag is unused: this object has only one kind of pending message.
    armed = scheduler->ScheduleBangOnClock(this, 0, onClock, beats);
  } else {
    armed = scheduler->ScheduleBang(this, 0, DelayTime());
  }

  pending.store(armed, std::memory_order_relaxed);
  // armed == 0 means the patcher-wide pending set is full. The bang is dropped
  // — counted by messageScheduler::Dropped() — rather than sent immediately;
  // see the header on why this edge answers differently from the standalone
  // one.
}

BANG_IN(BangIn) {
  // Registered on inlet 0 only, which is where Max documents it: the right
  // inlet has no bang method.
  (void)inlet;
  Start(thread);
}

INT_IN(IntIn) {
  // Max, for both inlets: "the number is stored as the number of milliseconds
  // to delay a bang received in the left inlet." Only the left inlet then goes
  // on to "automatically send a bang message to itself to start the delay" — a
  // number in the right inlet "does not modify the time of a bang currently
  // being delayed", which falls out of not touching the pending handle.
  delaytime = value;
  // "The number of milliseconds" is a statement about the *unit*, so a plain
  // number puts the object back on milliseconds whatever tempo-relative time it
  // was carrying (issue #705). The unit travels with the value, Max's model.
  delaybeats = 0.f;
  if (inlet == 0) Start(thread);
}

FLOAT_IN(FloatIn) {
  IntIn(MillisFromFloat(value), inlet, thread);
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

  // Max's stop, left inlet only: "stops delay from outputting the bang it is
  // currently delaying."
  if (inlet == 0 && length == 4 && value.compare(begin, length, "stop", 4) == 0) {
    CancelPending();
    return;
  }

  // Max's `clock` (issue #705), left inlet only, where `stop` is: "the word
  // clock, followed by the name of an existing setclock object, sets the delay
  // object to be controlled by that setclock object rather than by Max's
  // internal millisecond clock. The word clock by itself sets the delay object
  // back to using Max's regular millisecond clock." The whole remainder is the
  // name, so a clock named with spaces still works.
  if (inlet == 0 && length == 5 && value.compare(begin, length, "clock", 5) == 0) {
    std::size_t nameBegin = end;
    while (nameBegin < value.size() && IsSelectorSeparator(value[nameBegin]))
      nameBegin++;
    std::size_t nameEnd = value.size();
    while (nameEnd > nameBegin && IsSelectorSeparator(value[nameEnd - 1]))
      nameEnd--;
    SetClock(value.c_str() + nameBegin, nameEnd - nameBegin);
    return;
  }

  // Max's list / anything method, which exists to carry its time-format syntax.
  // The tempo-relative half of that syntax is read first, because its tick
  // spelling *starts* with a number — `1440 ticks` read as a leading number
  // would silently become 1440 ms. Note values and tick counts set the delay in
  // beats (issue #705); a leading number on its own means the same thing a bare
  // number does, which is milliseconds; everything else — bars.beats.units, an
  // unknown word — does nothing, deliberately, rather than being read as some
  // number it is not.
  double beats = 0.0;
  if (ReadBeatTime(value.c_str() + begin, value.size() - begin, beats)) {
    delaybeats = (Flt)beats;
    // Max's left inlet "then automatically sends a bang message to itself to
    // start the delay", exactly as it does for a plain number.
    if (inlet == 0) Start(thread);
    return;
  }

  float number = 0.f;
  if (!ReadNumericToken(value.c_str() + begin, length, number)) return;
  IntIn(MillisFromFloat(number), inlet, thread);
}

void gDelay::DeliverDeferred(const deferredMessage& msg, YSE::THREAD thread) {
  (void)msg;
  // The wait has elapsed. The scheduler wraps this in a fresh messageEventScope,
  // so everything the released bang goes on to cause is one logical event
  // (#628) — the causal chain the bang came in on, resumed rather than
  // replaced, which is the whole reason this object does not use TimerThread.
  //
  // The tag is passed straight through. It is T_GUI — "let the block's own
  // traversal render it", the right reading for an outlet send. A `.s` wired to
  // that outlet used to read the same tag as "the caller is the control thread"
  // and take `mtx` on this very callback; since #690 `PassData` decides that
  // from `CallingThread` instead, so the tag travels unaltered.
  pending.store(0, std::memory_order_relaxed);
  outputs[0].SendBang(thread);
}
