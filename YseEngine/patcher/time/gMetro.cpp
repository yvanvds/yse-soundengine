
#include "gMetro.h"
#include "../pListArgs.h"
#include "../pObjectList.hpp"
#include "../pSelector.h"
#include "timeValue.h"
#include <cmath>
#include <cstddef>

using namespace YSE::PATCHER;

#define className gMetro

namespace {

  constexpr char kHotInletDoc[] =
      "Non-zero int starts the metronome; 0 stops it. Max: 'any number other than 0 starts the "
      "metro object. At regular intervals, metro sends a bang out the outlet. 0 stops metro.' The "
      "message 'clock <name>' names the YSE domain clock a tempo-relative interval is counted on "
      "and a bare 'clock' takes it away again, which is Max's own method for this object — "
      "setclock names metro explicitly as one of the objects a 'clock' message controls (issue "
      "#705). Whether a run uses that clock or the millisecond timer is decided when the metro is "
      "started, so a 'clock' message reaches the next start rather than the run in progress.";

  constexpr char kColdInletDoc[] =
      "Sets the bang interval. An int or a float sets it in milliseconds — Max: 'the number is "
      "the time interval, in milliseconds, at which metro sends out a bang' — and retimes a "
      "running millisecond metro immediately, without restarting it. A list may carry one of "
      "Max's time formats instead, and since issue #705 the tempo-relative ones are read here: a "
      "note value ('4n', '4nd', '8nt') or a tick count ('1440 ticks') sets the interval in beats "
      "on the clock named by 'clock <name>', which retimes a running beat metro by rebasing the "
      "grid at the current beat, keeping the phase rather than re-triggering. Any plain number "
      "puts the interval back on milliseconds. bars.beats.units stays out, needing a meter no "
      "domain clock has.";

} // namespace

CONSTRUCT() {
  ADD_IN_0;
  REG_INT_IN(Toggle);
  REG_LIST_IN(ListIn);

  ADD_IN_1;
  REG_FLOAT_IN(SetFloatPeriod);
  REG_INT_IN(SetIntPeriod);
  REG_LIST_IN(ListIn);

  ADD_OUT_BANG;

  ADD_PARAM(period);
  ADD_PARAM(periodbeats);

  period = 1000;
  periodbeats = 0.f;
  id = 0;

  ADD_DESCRIPTION(
      "Periodic bang generator. Once toggled on, emits a bang every 'period' milliseconds (and "
      "immediately on start). Toggle off to stop. Since issue #705 the interval may be "
      "tempo-relative instead: a note value ('4n', '4nd', '8nt') or a tick count ('1440 ticks') "
      "in the right inlet sets it in beats, and 'clock <name>' names the YSE domain clock those "
      "beats are counted on — Max's own method, setclock naming metro as one of the objects a "
      "'clock' message controls. The metro then follows that domain's tempo changes and ramps, "
      "stays in step with every clip on it, and holds where it stands when the domain pauses. Any "
      "plain number puts it back on milliseconds and a bare 'clock' gives the millisecond clock "
      "back, so a metro never sent a beat interval is Max's object exactly. The bang count of a "
      "beat run is read off the clock's beat position rather than counted from the wakeups that "
      "deliver it, which is load-bearing rather than fastidious: a beat deadline is armed "
      "relative to the beat it was armed at but delivered at the first audio block boundary past "
      "it, so counting deliveries would drop that overshoot every bang and run the metro slow — "
      "and once the interval is shorter than a block it would cap at one bang per block however "
      "fast the domain ran. quantize, transport and bars.beats.units stay out, all three needing "
      "a meter a domain clock does not have. Calculate() does nothing and no message or delivery "
      "path allocates, locks or blocks.");
  ADD_CATEGORY(pCategory::TIME);
  INLET_DOC(0, "on/off", kHotInletDoc, "0 or 1, 'clock <name>'");
  INLET_DOC(1, "period", kColdInletDoc, "1+ ms, or a note value / tick count");
  OUTLET_DOC(0, "out", "Periodic bang.", "");
  PARAM_DOC("period", "1000",
            "Interval in milliseconds. Re-editing it while the metronome runs takes effect from "
            "the next bang.",
            "1+ ms");
  PARAM_DOC("periodbeats", "0",
            "Interval in beats, for the tempo-relative unit issue #705 added. 0 means the "
            "interval is the millisecond one above, which is what a Max metro always is; any "
            "positive value makes it a beat count on the domain clock a 'clock <name>' message "
            "names, and a note value or tick count in the right inlet overwrites it afterwards. A "
            "plain number in the right inlet clears it back to 0. Re-editing it while the "
            "metronome runs on a clock rebases the grid at the current beat, keeping the phase. "
            "The clock binding itself is run-time state and is not saved.",
            "0+ beats");
}

// A period change must reach a *running* metro, not just the next start
// (issue #625). Two routes write `period` and neither can be dropped:
//
//  - the cold inlet runs on the caller's thread, so it reschedules eagerly and
//    the new interval is in force before the call returns;
//  - a live SetParams re-parse stores straight into `period` from the audio
//    thread (patcherImplementation::ApplyPendingParams) and notifies nobody,
//    so Bang() re-asserts the interval on every tick as well. That route
//    therefore lands one tick later — the running cycle plays out at the old
//    interval and every cycle after it uses the new one.
//
// Neither path may restart the metro: rescheduling keeps the phase, so a tempo
// tweak does not re-trigger whatever the bang drives.
//
// The beat clock (issue #705) covers the same two routes the same way:
// RetimeBeats is the eager one, and a wakeup comparing the live `periodbeats`
// against the grid's `gridBeats` is the SetParams one.

timerThread::millisec gMetro::Interval() const {
  const Int ms = period.load();
  return ms > 0 ? static_cast<timerThread::millisec>(ms) : 1;
}

double gMetro::PeriodBeats() const {
  const Flt beats = periodbeats.load();
  // Written as a failed `>` so a NaN — which a live SetParams re-parse could
  // store — reads as "no beat interval" rather than as a grid nothing can land
  // on. A metro on a NaN interval would divide by it forever.
  return beats > 0.f ? (double)beats : 0.0;
}

void gMetro::ApplyPeriod() {
  const timerThread::timerID running = id.load();
  if (running != 0) TimerThread().SetPeriod(running, Interval());
}

// ─── the domain clock (issue #705) ──────────────────────────────────────────

const char* gMetro::ClockName() const {
  const clockBridge::Handle bound = binding.load(std::memory_order_relaxed);
  if (bound == 0) return "";
  const clockBridge* clocks = Clocks();
  if (clocks == nullptr) return "";
  return clocks->NameOf(bound);
}

void gMetro::SetClock(const char* name, std::size_t length) {
  // Max's bare `clock`: back to "Max's regular millisecond clock". A run in
  // progress keeps the engine it started on — the unit of a run is fixed at the
  // toggle — so this reaches the next start.
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
  // back to milliseconds, which would change what a stored beat interval means.
  if (bound == 0) return;
  binding.store(bound, std::memory_order_relaxed);
}

void gMetro::CancelWakeup() {
  if (pending == 0) return;
  messageScheduler* scheduler = Scheduler();
  if (scheduler != nullptr) scheduler->Cancel(pending);
  pending = 0;
}

void gMetro::ArmNext(clockBridge::Handle bound) {
  messageScheduler* scheduler = Scheduler();
  if (scheduler == nullptr) return;

  CancelWakeup();

  double ahead = gridBeats;
  const clockBridge* clocks = Clocks();
  double beat = 0.0;
  if (beatBased && clocks != nullptr && clocks->Beat(bound, beat)) {
    // The *absolute* next grid point, not one interval from here. A delivery
    // lands at the first audio block boundary at or after its deadline, so
    // asking for a fixed interval every time would hand back that overshoot on
    // every bang and run the metro slow. Asking for the distance to the grid
    // point instead absorbs it. (The delivery still takes its bang count off
    // the clock rather than from this arm — see the header. Both halves are
    // needed: this one keeps the wakeups on the grid, that one keeps the count
    // right when a wakeup is late or covers several intervals.)
    ahead = (beatBase + (double)(emitted + 1) * gridBeats) - beat;
    // Already past it — a wakeup that covered more than one interval. 0 beats
    // is due at the next drain, which is the next block, so the catch-up
    // continues one block at a time instead of stalling.
    if (!(ahead > 0.0)) ahead = 0.0;
  }
  // An unresolved binding leaves `ahead` at one plain interval, which the
  // scheduler baselines at the beat the binding resolves on: the run starts
  // when the clock starts existing.
  pending = scheduler->ScheduleBangOnClock(this, 0, bound, ahead);
}

void gMetro::StartBeats(clockBridge::Handle bound, YSE::THREAD thread) {
  {
    storeGuard guard(busy);
    // Losing the guard means another thread is inside the grid right now. This
    // run does not start rather than starting on a half-written one; that other
    // thread is a toggle or a retime, and it arms whatever it decides on.
    if (!guard.Held()) {
      beatOn.store(false, std::memory_order_relaxed);
      return;
    }

    gridBeats = PeriodBeats();
    emitted = 0;
    beatBased = false;
    const clockBridge* clocks = Clocks();
    double beat = 0.0;
    // Bang 0 stands on the beat this toggle landed on, so the run begins where
    // the message did. An unresolved binding has no beat to take; the first
    // wakeup that finds a clock takes it instead — messageScheduler's
    // ResolveBeat rule, arrived at for the same reason.
    if (clocks != nullptr && clocks->Beat(bound, beat)) {
      beatBase = beat;
      beatBased = true;
    }
    ArmNext(bound);
  }

  // Max's metro bangs the moment it is started, on either clock. Sent outside
  // the guard: a patch that wires this outlet back into this object must not
  // find the grid held.
  outputs[0].SendBang(thread);
}

void gMetro::RetimeBeats() {
  if (!beatOn.load(std::memory_order_relaxed)) return;
  const clockBridge::Handle bound = binding.load(std::memory_order_relaxed);
  if (bound == 0) return;

  const double interval = PeriodBeats();
  // The interval stopped being tempo-relative. The unit of a run is fixed at
  // the toggle, so the grid stays where it is and the millisecond interval
  // takes effect at the next start.
  if (!(interval > 0.0)) return;

  storeGuard guard(busy);
  if (!guard.Held()) return;
  if (interval == gridBeats) return;

  const clockBridge* clocks = Clocks();
  double beat = 0.0;
  if (clocks != nullptr && clocks->Beat(bound, beat)) {
    // Rebase rather than rescale: #625's rule for the millisecond path is that
    // retiming keeps the phase and does not re-trigger, and measuring the new
    // grid from *here* is what that means on a beat clock. The bang already
    // emitted at this instant is not emitted again, `emitted` starting at 0
    // against a base that has just moved to now.
    beatBase = beat;
    beatBased = true;
    emitted = 0;
  }
  gridBeats = interval;
  ArmNext(bound);
}

void gMetro::StopRun() {
  const timerThread::timerID running = id.exchange(0);
  if (running != 0) TimerThread().ClearTimer(running);

  beatOn.store(false, std::memory_order_relaxed);
  storeGuard guard(busy);
  // A lost guard leaves the wakeup armed; it finds `beatOn` false and stops
  // there, which is why that flag is outside the guard.
  if (!guard.Held()) return;
  CancelWakeup();
}

INT_IN(Toggle) {
  // Stop first on either edge: a restart while running must not leak the
  // previous timer or the previous wakeup (the double-start case).
  StopRun();

  if (value == 0) return;

  // Which engine this run uses is decided here, once, and does not change under
  // it (issue #705).
  if (PeriodBeats() > 0.0) {
    const clockBridge::Handle bound = binding.load(std::memory_order_relaxed);
    // A tempo-relative interval with no clock bound has nothing to be measured
    // against — this patcher has no transport for a note value to fall back on
    // — so the metro does not run. Falling back to the millisecond interval
    // would be a metronome at a tempo nobody asked for; a later `clock <name>`
    // makes the next toggle work.
    if (bound == 0) return;
    beatOn.store(true, std::memory_order_relaxed);
    StartBeats(bound, thread);
    return;
  }

  const timerThread::millisec ms = Interval();
  id.store(TimerThread().Add(ms, ms, std::bind(&gMetro::Bang, this)));
  // send first bang instantly
  Bang();
}

INT_IN(SetIntPeriod) {
  period = value;
  // Max: "the number is the time interval, in milliseconds", which is a
  // statement about the *unit*, so a plain number puts the object back on
  // milliseconds whatever tempo-relative interval it was carrying (issue #705).
  // A beat run in progress keeps its grid; the unit of a run is fixed at the
  // toggle.
  periodbeats = 0.f;
  ApplyPeriod();
}

FLOAT_IN(SetFloatPeriod) {
  SetIntPeriod((int)value, inlet, thread);
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

  // Max's `clock` (issue #705), left inlet, where this object's other command
  // would go: setclock's name is "passed as the argument to a 'clock' message
  // to numerous objects that use timing in Max, such as metro, line and pipe".
  // The whole remainder is the name, so a clock named with spaces still works.
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

  // Max's list / anything method on the right inlet: "a list may be used to
  // specify time in one of the Max time formats." The tempo-relative half is
  // read first, because its tick spelling *starts* with a number — `1440
  // ticks` read as a leading number would silently become 1440 ms.
  if (inlet != 1) return;

  double beats = 0.0;
  if (ReadBeatTime(value.c_str() + begin, value.size() - begin, beats)) {
    periodbeats = (Flt)beats;
    RetimeBeats();
    return;
  }

  // A leading number means the same thing a bare number does; everything else —
  // bars.beats.units, an unknown word — does nothing, deliberately, rather than
  // being read as some number it is not.
  float number = 0.f;
  if (!ReadNumericToken(value.c_str() + begin, length, number)) return;
  SetFloatPeriod(number, inlet, thread);
}

void gMetro::Bang() {
  // Timer thread. Picks up an interval stored by the parameter path, which has
  // no way to call in here itself. SetPeriod is safe from inside the callback:
  // the worker holds no lock across it, and the timer is not queued at this
  // point, so this only updates the value the worker reschedules with.
  ApplyPeriod();
  outputs[0].SendBang(T_GUI);
}

void gMetro::DeliverDeferred(const deferredMessage& msg, YSE::THREAD thread) {
  (void)msg;
  std::int64_t bangs = 0;
  {
    storeGuard guard(busy);
    // A lost guard drops this wakeup without re-arming, which is safe here and
    // only here: the thread that holds the grid is a toggle or a retime, and
    // both arm a wakeup of their own before they let go. `.seq`'s rule — a
    // message path may never wait for a guard.
    if (!guard.Held()) return;

    // Whatever armed this wakeup has fired; the handle it left behind is stale.
    pending = 0;
    if (!beatOn.load(std::memory_order_relaxed)) return;
    const clockBridge::Handle bound = binding.load(std::memory_order_relaxed);
    if (bound == 0) return;

    const clockBridge* clocks = Clocks();
    double beat = 0.0;
    if (clocks == nullptr || !clocks->Beat(bound, beat)) {
      // The clock does not exist yet, so no time has passed on it. Re-arm and
      // let the bridge's own Poll resolve the name; this is what makes a clock
      // named before the host creates it start the run when it appears.
      ArmNext(bound);
      return;
    }

    // The run started before its clock resolved; this is the wakeup that found
    // it, and bang 0 stands here.
    if (!beatBased) {
      beatBase = beat;
      beatBased = true;
      emitted = 0;
    }

    // The SetParams route into the interval (issue #625's second route), which
    // stores from the audio thread and notifies nobody. Rebasing rather than
    // rescaling keeps the phase, as it does in RetimeBeats.
    const double interval = PeriodBeats();
    if (interval > 0.0 && interval != gridBeats) {
      gridBeats = interval;
      beatBase = beat;
      emitted = 0;
    }
    if (!(gridBeats > 0.0)) {
      // Nothing to divide by. Cannot happen from a toggle, which refuses a
      // non-positive interval, but a grid is a grid only while it has a step.
      beatOn.store(false, std::memory_order_relaxed);
      return;
    }

    // The time base: how many intervals the *clock* has moved, never how many
    // wakeups have arrived. See the header — this is the whole reason the run
    // does not drift, and the reason it does not cap at one bang per block
    // when the interval is shorter than a block.
    double elapsed = (beat - beatBase) / gridBeats;
    // Also the NaN case, no comparison accepting one. A beat position that went
    // backwards emits nothing rather than winding the count back.
    if (!(elapsed > 0.0)) elapsed = 0.0;
    // Far past any grid index a running clock can reach, and small enough that
    // the int64 below cannot overflow.
    if (elapsed > 1.0e12) elapsed = 1.0e12;

    const std::int64_t target = (std::int64_t)std::floor(elapsed);
    bangs = target - emitted;
    if (bangs < 0) bangs = 0;
    // A domain that jumped many intervals in one block skips what it missed
    // rather than emptying an unbounded burst onto the audio thread. The count
    // still advances to `target`, so nothing accumulates into the next wakeup.
    if (bangs > MAX_CATCHUP) bangs = MAX_CATCHUP;
    emitted = target;

    // Re-armed before anything is emitted, so the next wakeup is on the grid
    // whatever this one turns out to send. A send that stops the metro — a
    // toggle coming back through a cord — takes it out again on the way past.
    ArmNext(bound);
  }

  for (std::int64_t i = 0; i < bangs; i++)
    outputs[0].SendBang(thread);
}

gMetro::~gMetro() {
  const timerThread::timerID running = id.exchange(0);
  if (running != 0) {
    // `TimerThread()` (capital) is the singleton that owns the timer; lowercase
    // `timerThread` is the class, and `using namespace YSE::PATCHER` puts both
    // in scope. This used to read `timerThread()`, which value-constructed an
    // empty instance on the stack, asked *it* to drop an id it had never issued
    // and threw it away — leaving the real timer alive with a std::bind to a
    // dying `this` (issue #663).
    //
    // ClearTimer may block until an in-flight Bang() returns; that handshake is
    // the point. Everything Bang() touches is still alive here — `outputs` is a
    // base-class member, destroyed only after this body — and a destructor
    // never runs on the audio thread.
    TimerThread().ClearTimer(running);
  }
  // The beat wakeup needs no such handshake: a pending scheduler message is
  // re-resolved against the block's pinned GraphState before it is delivered,
  // and an object being destroyed is absent from it, so the message is dropped
  // rather than delivered to a dying object. `.delay` relies on the same rule.
}
