#include "gDelay.h"
#include "../pListArgs.h"
#include "../pObjectList.hpp"
#include "../pSelector.h"
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
      "The bang inlet. A bang starts the wait: Max's 'a bang is delayed a certain number of "
      "milliseconds before being sent out the outlet'. Only one bang is held at a time — a second "
      "one cancels the first and restarts the wait, Max's 'if a bang is already in delay when a "
      "new bang is received in the left inlet, the first bang is forgotten'. An int or a float "
      "sets the delay time and then starts the wait, which is Max's own wording: the number 'is "
      "stored as the number of milliseconds to delay a bang received in the left inlet. It then "
      "automatically sends a bang message to itself to start the delay.' The message 'stop' "
      "cancels the held bang without sending it. A list whose first item is a number sets the "
      "time and starts the wait, the same as a bare number; Max's list method exists to carry its "
      "time-format syntax (notevalues, ticks, bars.beats.units, samples), and milliseconds are "
      "the only unit here because everything else is tempo-relative and this object was written "
      "before the patcher had any bridge to a domain clock. Issue #688 has since built one, and "
      "adopting it here — 'clock <name>' plus a beat unit — is filed as issue #705. Until then "
      "anything else, 'clock' included, does nothing.";

  constexpr char kColdInletDoc[] =
      "Sets the delay time in milliseconds without starting anything. Max: 'a number received in "
      "the right inlet changes the delay time of the next bang received -- it does not modify the "
      "time of a bang currently being delayed', so a bang already waiting still leaves at the "
      "time it was armed with. Ints, floats and a list whose first item is a number all set it; a "
      "negative time counts as 0. There is no bang method here, as there is none in Max.";

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
  delaytime = DEFAULT_DELAY;

  ADD_DESCRIPTION(
      "Delays a bang. A bang in the left inlet comes back out the outlet a settable number of "
      "milliseconds later, which is the patcher's most basic scheduling primitive: .metro can "
      "only repeat, and until this object there was no way to defer anything at all. Only one "
      "bang is held at a time — a new one cancels the pending one and restarts the wait, Max's "
      "'the first bang is forgotten' — and 'stop' cancels it outright. A number in the left inlet "
      "sets the time and starts the wait; a number in the right inlet sets the time for the next "
      "bang only, leaving a bang already in flight alone. The wait runs on the patcher's "
      "deferred-message scheduler (issue #628) rather than on the timer thread behind .metro, so "
      "arming allocates nothing and takes no lock, and the released bang arrives inside the "
      "patcher's own dispatch as one fresh logical event rather than as an unrelated stimulus. "
      "That clock is the block counter, so it stops when the engine does and a paused patch holds "
      "a delayed bang where it stands, and its resolution is one audio block. A delay of 0 still "
      "defers to the next block rather than firing immediately, which is Max's behaviour and the "
      "reason a delay wired back into itself is a fast metronome instead of a stack overflow. "
      "Milliseconds only: Max's notevalue, tick and bars.beats.units formats are tempo-relative, "
      "and this object was written before the patcher had a bridge to a domain clock. Issue #688 "
      "has since built one — the same one .qlist's 'clock <name>' plays on — and adopting it here "
      "is filed as issue #705. Calculate() does nothing and no "
      "message path allocates, locks or blocks.");
  ADD_CATEGORY(pCategory::TIME);
  INLET_DOC(0, "bang", kHotInletDoc, "bang, int, float, list, 'stop'");
  INLET_DOC(1, "time", kColdInletDoc, "0+ ms");
  OUTLET_DOC(0, "out", "The delayed bang, one audio block or more after the bang that caused it.",
             "");
  PARAM_DOC("delaytime", "5",
            "The initial delay in milliseconds — Max's creation argument. Max's reference page "
            "disagrees with itself here: its Arguments section says 'if there is no argument, the "
            "initial time interval is 5 milliseconds' while its Attributes table lists delaytime "
            "as defaulting to 0 ms. The argument prose wins, being the statement about this "
            "object's constructor rather than about the attribute's declared default. Either "
            "inlet overwrites it afterwards. A negative value counts as 0.",
            "0+ ms");
}

int gDelay::DelayTime() const {
  const Int ms = delaytime.load();
  return ms > 0 ? (int)ms : 0;
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
  // The tag is unused: this object has only one kind of pending message.
  const messageScheduler::Handle armed = scheduler->ScheduleBang(this, 0, DelayTime());
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

  // The millisecond subset of Max's list / anything method, which exists to
  // carry its time-format syntax. A leading number means the same thing a bare
  // number does; everything else — a notevalue, `clock`, an unknown word — does
  // nothing, deliberately, rather than being read as some number it is not.
  // `clock <name>` has a meaning in the patcher since #688 built the bridge;
  // teaching this object to answer it is #705, not a line to sneak in here.
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
