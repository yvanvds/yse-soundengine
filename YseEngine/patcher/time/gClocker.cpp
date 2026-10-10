
#include "gClocker.h"
#include "../../clock/clockManager.h"
#include "../pListArgs.h"
#include "../pObjectList.hpp"
#include "../pSelector.h"
#include "../patcherImplementation.h"
#include "timeValue.h"
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>

using namespace YSE::PATCHER;

#define className gClocker

namespace {

  constexpr char kHotInletDoc[] =
      "Starts and stops the clocker, and carries its three messages. Max: 'any non-zero number "
      "starts the clocker object. The time elapsed since clocker was started is sent out the "
      "outlet at regular intervals. 0 stops the clocker object.' A float does the same — Max's "
      "'same as int' — and is compared against zero rather than cast, so 0.5 starts it where a "
      "cast to int would stop it. The message 'stop' is 0 under Max's own name. A bang is "
      "deliberately not the same method: Max gives it a rule of its own — 'if the clocker object "
      "is not running, a bang message will start the count. If the clocker object is running, a "
      "bang message will reset the count' — so a bang into a running clocker moves the baseline "
      "and leaves the tick where it is, where a non-zero int re-starts the run and re-phases the "
      "tick with it. The message 'reset' is that same reset spelled out: Max's 'resets the "
      "elapsed time to 0 without stopping or restarting the clock; clocker continues to report "
      "the new elapsed time at the same regular interval'. It does nothing while the clocker is "
      "stopped, Max's 'meaningless when the clocker is not running, since it always resets to 0 "
      "anyway when stopped'. 'clock <name>' names the YSE domain clock the beats are counted on — "
      "the same message '.metro', '.timepoint' and '.tempo' take — and a bare 'clock' gives the "
      "millisecond clock back, Max's 'sets the object back to using Max's regular millisecond "
      "clock'. Binding a clock never starts or stops anything, and never changes the unit of a "
      "run already going: which engine a run uses is settled at the toggle. Everything else does "
      "nothing, deliberately, rather than being read as some command it is not.";

  constexpr char kColdInletDoc[] =
      "Sets the reporting interval, in milliseconds or in beats. A plain number is Max's "
      "milliseconds — the 'interval' attribute and the object's first creation argument — and an "
      "int, a float or a list with a leading number all set it. A note value ('4n', '4nd', '8nt') "
      "or a tick count ('1440 ticks') sets it in beats instead, counted on the domain clock a "
      "'clock <name>' message names, and any plain number afterwards puts it back on "
      "milliseconds: Max's 'the number is the time interval, in milliseconds' is a statement about "
      "the unit. The tempo-relative form is read before the plain number, which is what stops "
      "'1440 ticks' from silently becoming 1440 milliseconds — a value wrong by whatever the tempo "
      "is, and looking like it worked. Setting the interval under a running clocker retimes "
      "without re-phasing, so the elapsed time the next report carries follows on from the last "
      "rather than jumping: the numbers are measured from the start of the run and nothing here "
      "moves that. A tempo-relative interval reaches a beat run by rebasing its grid at the "
      "current beat, keeping the phase; a millisecond one reaches a beat run only at the next "
      "start, the unit of a run being fixed at the toggle. bars.beats.units is not read at all — a "
      "bar needs a meter, and a domain clock is a bare beat accumulator.";

  constexpr char kOutletDoc[] =
      "The elapsed time in milliseconds, sent once per interval while the clocker runs. It is "
      "*measured* rather than counted: each report reads the engine's monotonic clock and "
      "subtracts the instant the run started, so a tick the OS delivered late reports the time it "
      "actually arrived at and the run never drifts away from real time. Multiplying a tick count "
      "by the interval — the obvious implementation — would report the nominal time forever and "
      "diverge a little further on every tick. This outlet carries milliseconds whether or not a "
      "clock is bound, so binding one never changes what a patch downstream is reading; the beats "
      "are a second outlet rather than a second meaning for this one. Nothing is sent while the "
      "clocker is stopped, and the first report comes one interval after the start rather than "
      "immediately: Max documents an immediate output for 'metro' and documents none here.";

  constexpr char kBeatsOutletDoc[] =
      "The same elapsed time in beats of the bound domain clock — issue #725's answer to what a "
      "'.clocker' on a clock reports, and the same two-outlet answer '.timer' gives (#506). The "
      "clock's beat position is read at the start of the run and at each report and subtracted, "
      "so a tempo change, a ramp or a pause is accounted for exactly; this is emphatically not the "
      "millisecond figure divided by a BPM number, and the two outlets are not two spellings of "
      "one value — on a domain that pauses, the milliseconds keep rising and the beats hold, and "
      "that difference is the information. Sent before the milliseconds, Max's outlets firing "
      "right to left. Nothing at all is emitted when there is no beat to report: no 'clock <name>' "
      "message, a clock nothing has created, or a standalone object with no patcher to bind "
      "through. A run that began before its clock existed starts measuring at the first report "
      "that finds one.";

  // The clocker whose timer callback this thread is currently inside, or null —
  // null both when no clocker is ticking on this thread and when the one that
  // is, is another object.
  //
  // `Tick()` runs on the timer worker *inside* `timer.func()`, and its outlet
  // can come straight back round to this object's own left inlet — directly, or
  // through anything that passes a number along. A stop arriving that way would
  // otherwise reach `timerBridge::ApplyStop`, which takes the slot mutex; and
  // the slot mutex may at that instant be held by a reconcile job on the
  // background pool that is itself blocked inside `timerThread::ClearTimer`
  // waiting for *this callback* to return. Neither side can move. (Issue #722
  // taught `ClearTimer` to recognise a stop issued from its own worker, which
  // covers the direct call; it cannot help here, because the thread doing the
  // waiting is the pool's and not the worker's.)
  //
  // So a handler that finds its own mark takes the **wait-free** front of the
  // bridge instead of the blocking one: it writes wanted state and lets the
  // pool reconcile a hop later. That is only ever a question of when the
  // *timer* moves. What the user sees does not wait for it — `running` and the
  // baselines are written here and now, and `Tick()` reads `running` before it
  // sends, so a clocker stopped from inside its own tick emits nothing further
  // whatever the timer is still doing.
  //
  // Thread-local, because the question is a property of the calling thread and
  // not of the object — `patcherImplementation::CallingThread` (#690) answers
  // the sibling question the same way — and saved and restored rather than set
  // and cleared, so no path can leave a stale answer behind. `.metro`'s bang
  // frame (#721) is the same instrument.
  thread_local const YSE::PATCHER::gClocker* tTickingClocker = nullptr;

  struct tickFrameGuard {
    const YSE::PATCHER::gClocker* previous;
    explicit tickFrameGuard(const YSE::PATCHER::gClocker* self) : previous(tTickingClocker) {
      tTickingClocker = self;
    }
    ~tickFrameGuard() {
      tTickingClocker = previous;
    }
    tickFrameGuard(const tickFrameGuard&) = delete;
    tickFrameGuard& operator=(const tickFrameGuard&) = delete;
    tickFrameGuard(tickFrameGuard&&) = delete;
    tickFrameGuard& operator=(tickFrameGuard&&) = delete;
  };

  bool InOwnTick(const YSE::PATCHER::gClocker* self) {
    return tTickingClocker == self;
  }

  // Widest millisecond figure the int outlet can carry. About 24.8 days of
  // running, past which the elapsed time is pinned rather than wrapped into a
  // negative number: a clocker that has been on for a month is more usefully
  // read as "a very long time" than as "minus three weeks", and the cast itself
  // would be undefined.
  constexpr std::int64_t MAX_REPORTED_MS = 2147483647;

  // No beat baseline. NaN rather than a flag: there is no beat position that
  // could not legitimately be one.
  constexpr double NO_BEAT = std::numeric_limits<double>::quiet_NaN();

} // namespace

CONSTRUCT() {
  ADD_IN_0;
  REG_INT_IN(Toggle);
  REG_FLOAT_IN(ToggleFloat);
  REG_BANG_IN(BangIn);
  REG_LIST_IN(Command);

  ADD_IN_1;
  REG_INT_IN(SetIntInterval);
  REG_FLOAT_IN(SetFloatInterval);
  REG_LIST_IN(Command);

  ADD_OUT_INT; // the elapsed time in milliseconds
  ADD_OUT_FLOAT; // the elapsed time in beats of the bound clock

  ADD_PARAM(interval);
  ADD_PARAM(intervalbeats);

  // Max: "if there is no argument, the initial time interval is set to 5
  // milliseconds."
  interval = 5;
  // Milliseconds until a note value or a tick count says otherwise, which is
  // what a Max clocker always is.
  intervalbeats = 0.f;

  // The slot this object's timer lives in for the rest of its life (issue
  // #718). Taken here, on the control thread, so no message handler ever has
  // to; given back in the destructor. A refusal — a process holding
  // timerBridge::CAPACITY timer owners at once — costs this one its millisecond
  // clock, silently, since a log line is not this constructor's to emit, and
  // leaves the beat engine unaffected.
  timerSlot = TimerBridge().Claim(&gClocker::TickTrampoline, this);

  ADD_DESCRIPTION(
      "Elapsed-time reporter — Max's clocker, 'a metronome that reports the time elapsed since it "
      "was started' (issues #505 and #725). Where '.metro' bangs at an interval, this sends a "
      "number, and the number is how long the run has been going. That is what takes an object out "
      "of the patch: an envelope, a ramp or a progress readout driven by a '.metro' needs a "
      "'.counter' behind it to turn ticks into a position, and that counter is wrong the moment a "
      "tick is late or dropped. The elapsed time here is measured rather than counted — each "
      "report reads the engine's monotonic clock (std::chrono::steady_clock, the same source the "
      "patcher's timer thread schedules on and the same one the engine times its own ticks with) "
      "and subtracts the instant the run started, so an OS timer that wakes late reports the time "
      "it really arrived at and the run never drifts. A non-zero int or float in the left inlet "
      "starts it and 0 stops it, 'stop' is Max's word for the same, and a bang starts a stopped "
      "clocker or resets a running one's count — Max gives bang a rule of its own here, so unlike "
      "'.metro' it is not the same method as int: a bang moves the baseline and leaves the tick "
      "alone, where a non-zero int re-starts the run and re-phases the tick with it. The message "
      "'reset' spells that reset out. The right inlet sets the interval in milliseconds (5 by "
      "default, as in Max), or tempo-relative as a note value or a tick count, and retimes a "
      "running clocker without re-phasing it. Since issue #725 'clock <name>' binds a YSE domain "
      "clock — Max's own method, setclock naming the objects a 'clock' message controls — and the "
      "object then answers the question a millisecond clocker never had to: whether 'elapsed' is "
      "milliseconds or beats. It is both, spelled by two outlets, which is what '.timer' does with "
      "the same question: the first outlet is always milliseconds, so binding a clock changes no "
      "patch's meaning, and the second carries the beats the clock has moved since the run "
      "started. The two are genuinely two measurements — a domain that ramps or pauses makes them "
      "diverge, and neither could be got by dividing the other by a BPM figure. A beat run is on "
      "the clock's grid, its index read off the beat position rather than counted from the wakeups "
      "that deliver it, and a wakeup that covered several grid points reports once: this object's "
      "number is the time now, and copies of one reading are not the reports that were missed. It "
      "never creates a clock and never destroys one — '.transport' is the sole creator (issue "
      "#513) — so a name nothing has claimed simply has no beats until something claims it. "
      "quantize, autostart, autostarttime, defer and bars.beats.units stay out. Calculate() does "
      "nothing, and no message or delivery path allocates, locks or blocks when it turns out to be "
      "running on the audio callback: the millisecond timer is armed and disarmed through "
      "timerBridge, which takes a wait-free request there and does the locking work on the "
      "background pool, binding and arming are wait-free, reading a beat is two acquire loads, and "
      "the command words are matched against the message in place.");
  ADD_CATEGORY(pCategory::TIME);
  INLET_DOC(0, "on/off", kHotInletDoc, "0 or 1, bang, 'stop', 'reset', 'clock <name>'");
  INLET_DOC(1, "interval", kColdInletDoc, "1+ ms, or a note value / tick count");
  OUTLET_DOC(0, "elapsed", kOutletDoc, "0+ ms");
  OUTLET_DOC(1, "beats", kBeatsOutletDoc, "0+ beats");
  PARAM_DOC("interval", "5",
            "The reporting interval in milliseconds — Max's 'interval' attribute and the object's "
            "first creation argument, whose default Max documents as 5 ms. Floored at 1, which is "
            "the finest interval the patcher's millisecond timer keeps. Re-editing it while the "
            "clocker runs retimes the tick without re-phasing it, so the elapsed time carries on "
            "rather than jumping. Ignored while the tempo-relative interval below is set, and "
            "restored the moment a plain number clears that back to 0. Whether the clocker is "
            "running, how long it has been running, and which clock it is bound to are run-time "
            "state and are not saved.",
            "1+ ms");
  PARAM_DOC("intervalbeats", "0",
            "The reporting interval in beats, for the tempo-relative unit issue #725 added, and "
            "the second creation argument. 0 means the interval is the millisecond one above, "
            "which is what a Max clocker always is; any positive value makes it a beat count on "
            "the domain clock a 'clock <name>' message names, and a note value or tick count in "
            "the right inlet overwrites it afterwards. A plain number in the right inlet clears it "
            "back to 0. A tempo-relative interval with no clock bound does not run at all, rather "
            "than falling back to a millisecond grid nobody asked for. Re-editing it while the "
            "clocker runs on a clock rebases the grid at the current beat, keeping the phase. The "
            "clock binding itself is run-time state and is not saved.",
            "0+ beats");
}

// ─── the time base ──────────────────────────────────────────────────────────

std::int64_t gClocker::NowNs() {
  // steady_clock, and not by preference: it is `timerThread::Clock`, so the
  // tick that carries this reading and the reading itself are two views of one
  // timeline. It is also `INTERNAL::time`'s source (#667) and `MIDI::nowNs`'s.
  // A *wall* clock would be wrong twice over — not monotonic, so an NTP
  // correction mid run would make the elapsed time jump or go backwards.
  //
  // RT-safe: a QueryPerformanceCounter on Windows and a vDSO clock_gettime on
  // Linux and Android. INTERNAL::time::update reads it from the audio callback
  // on every block already.
  return std::chrono::duration_cast<std::chrono::nanoseconds>(
             std::chrono::steady_clock::now().time_since_epoch())
      .count();
}

timerThread::millisec gClocker::IntervalMs() const {
  const Int ms = interval.load();
  return ms > 0 ? (timerThread::millisec)ms : 1;
}

double gClocker::IntervalBeats() const {
  const Flt beats = intervalbeats.load();
  // Written as a failed `>` so a NaN — which a live SetParams re-parse could
  // store — reads as "no beat interval" rather than as a grid nothing can land
  // on. A clocker on a NaN interval would divide by it forever.
  return beats > 0.f ? (double)beats : 0.0;
}

std::int64_t gClocker::Elapsed() const {
  // Max: a stopped clocker "always resets to 0 anyway", so the baseline is not
  // even read here — it means nothing while the run is off.
  if (!running.load(std::memory_order_relaxed)) return 0;
  const std::int64_t ns = NowNs() - baseNs.load(std::memory_order_relaxed);
  // Negative cannot happen on a monotonic clock, but the baseline is written by
  // another thread and a reader that trusted the subtraction would report a
  // huge number if it ever did.
  if (ns <= 0) return 0;
  const std::int64_t ms = ns / 1000000;
  return ms > MAX_REPORTED_MS ? MAX_REPORTED_MS : ms;
}

bool gClocker::ElapsedBeats(double& beats) const {
  if (!running.load(std::memory_order_relaxed)) return false;
  const double base = baseBeat.load(std::memory_order_relaxed);
  // No baseline is no measurement — the run began with no clock to start it
  // from. A report would adopt one here; a query must not, being a query.
  if (std::isnan(base)) return false;
  double beat = 0.0;
  if (!ReadBeat(YSE::T_GUI, beat)) return false;
  beats = beat - base;
  return true;
}

// ─── the clock: bound, never created ────────────────────────────────────────

const char* gClocker::ClockName() const {
  const clockBridge::Handle bound = binding.load(std::memory_order_relaxed);
  if (bound == 0) return "";
  const clockBridge* clocks = Clocks();
  if (clocks == nullptr) return "";
  return clocks->NameOf(bound);
}

bool gClocker::ReadBeatFast(double& beat) const {
  // No binding, no beats — which covers an unbound object, a full bridge, and a
  // standalone one with no patcher to bind through.
  const clockBridge* clocks = Clocks();
  if (clocks == nullptr) return false;
  const clockBridge::Handle bound = binding.load(std::memory_order_relaxed);
  if (bound == 0) return false;
  // Two acquire loads, the only read the audio callback and the report path may
  // take. It also keeps #707's rule — a clock the host destroyed under a
  // resolved binding reads its frozen beat rather than disappearing mid-run.
  return clocks->Beat(bound, beat);
}

bool gClocker::ReadBeat(YSE::THREAD thread, double& beat) const {
  if (ReadBeatFast(beat)) return true;

  const clockBridge* clocks = Clocks();
  if (clocks == nullptr) return false;
  const clockBridge::Handle bound = binding.load(std::memory_order_relaxed);
  if (bound == 0) return false;

  // The binding has not resolved yet — resolution is a background hop, since
  // the name lookup takes the clock manager's mutex. Off the callback this
  // thread may take that mutex itself, which is what lets a run started in the
  // same breath as `clock <name>` have a baseline at all. On the callback there
  // is simply nothing to report; the first report that finds a clock adopts a
  // baseline instead. `.timer`'s fallback (#506), and its reasoning.
  if (OnAudioThread(thread)) return false;
  const char* name = clocks->NameOf(bound);
  if (name == nullptr || name[0] == '\0') return false;
  // The manager asks by std::string and this object holds the name only as the
  // bridge's storage, so one is built here. This branch is never the audio
  // callback and is only ever taken in the window before the pool resolves the
  // binding, so that allocation is this thread's to make — `.tempo`'s rule for
  // the same call.
  const std::string lookup(name);
  if (!CLOCK::Manager().clockExists(lookup)) return false;
  beat = CLOCK::Manager().beatPosition(lookup);
  return true;
}

void gClocker::SetClock(const char* name, std::size_t length, YSE::THREAD thread) {
  // Max's bare `clock`: back to "Max's regular millisecond clock". The beats
  // outlet falls silent with the binding, and the baseline goes with it — a
  // beat position on a clock this object no longer reads is not a baseline.
  if (name == nullptr || length == 0) {
    binding.store(0, std::memory_order_relaxed);
    baseBeat.store(NO_BEAT, std::memory_order_relaxed);
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
  // leaves the object on whatever clock it was on rather than silently
  // reporting against another one.
  if (bound == 0) return;
  binding.store(bound, std::memory_order_relaxed);

  // What the outlets measure from moves onto the new clock: the beats a run has
  // covered are that clock's beats, and a difference taken across two clocks
  // would be a number about neither. The *milliseconds* are untouched, this
  // being a change of clock and not a reset.
  if (running.load(std::memory_order_relaxed)) {
    double beat = 0.0;
    baseBeat.store(ReadBeat(thread, beat) ? beat : NO_BEAT, std::memory_order_relaxed);
  }
  // And a beat run's grid moves with it. A millisecond run is left alone: the
  // unit of a run is fixed at the toggle, so a `clock <name>` reaches it at the
  // next start.
  RebaseGrid();
}

// ─── the timer, and which route this handler may take ───────────────────────

void gClocker::TickTrampoline(void* ctx) {
  static_cast<gClocker*>(ctx)->Tick();
}

bool gClocker::OnAudioThread(YSE::THREAD thread) const {
  // The `THREAD` tag is dispatch semantics, not thread identity: in-patcher
  // delivery dispatches T_DSP, and the drains at the top of Calculate dispatch
  // T_GUI *from the audio callback*. Only the patcher knows, and only since
  // #690 — so ask it, exactly as `.metro`, `.tempo` and `.transport` do, and
  // pass the tag itself on unaltered. A standalone object has no patcher and is
  // never rendered, so it answers false and takes the direct path.
  if (parent == nullptr) return false;
  return static_cast<patcherImplementation*>(parent)->CallingThread(thread) == YSE::T_DSP;
}

void gClocker::StartMillis(YSE::THREAD thread) {
  if (timerSlot == 0) return;
  // WantStart bumps the bridge's `startSeq`, so a start against a timer that is
  // already armed replaces it rather than retiming it — the re-phase Max's
  // "starts the clocker object" means on a running object.
  if (InOwnTick(this) || OnAudioThread(thread))
    TimerBridge().RequestStart(timerSlot, IntervalMs());
  else
    TimerBridge().ApplyStart(timerSlot, IntervalMs());
}

void gClocker::StopMillis(YSE::THREAD thread) {
  if (timerSlot == 0) return;
  // The inline stop keeps `ClearTimer`'s handshake — once it returns, no
  // further callback for this timer can begin. The wait-free one cannot promise
  // that, so a clocker stopped from the audio callback or from inside its own
  // tick may still have a callback run; it finds `running` false and sends
  // nothing, which is the whole promise that matters here.
  if (InOwnTick(this) || OnAudioThread(thread))
    TimerBridge().RequestStop(timerSlot);
  else
    TimerBridge().ApplyStop(timerSlot);
}

void gClocker::ApplyInterval(YSE::THREAD thread) {
  if (timerSlot == 0) return;
  // A retime never writes `wantOn`, so it cannot resurrect a stopped clocker;
  // only the *blocking* route has to be avoided from inside the tick.
  if (InOwnTick(this) || OnAudioThread(thread))
    TimerBridge().RequestPeriod(timerSlot, IntervalMs());
  else
    TimerBridge().ApplyPeriod(timerSlot, IntervalMs());
}

// ─── the beat grid ──────────────────────────────────────────────────────────

void gClocker::CancelWakeup() {
  if (pending == 0) return;
  messageScheduler* scheduler = Scheduler();
  if (scheduler != nullptr) scheduler->Cancel(pending);
  pending = 0;
}

void gClocker::ArmNext(clockBridge::Handle bound) {
  messageScheduler* scheduler = Scheduler();
  if (scheduler == nullptr) return;

  CancelWakeup();

  double ahead = gridBeats;
  double beat = 0.0;
  if (beatBased && ReadBeatFast(beat)) {
    // The *absolute* next grid point, not one interval from here. A delivery
    // lands at the first audio block boundary at or after its deadline, so
    // asking for a fixed interval every time would hand back that overshoot on
    // every report and run the grid slow. (The delivery still takes its index
    // off the clock rather than from this arm. Both halves are needed.)
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

void gClocker::StartBeats(clockBridge::Handle bound) {
  storeGuard guard(busy);
  // Losing the guard means another thread is inside the grid right now. This
  // run does not start rather than starting on a half-written one; that other
  // thread is a toggle or a retime, and it arms whatever it decides on.
  if (!guard.Held()) {
    beatOn.store(false, std::memory_order_relaxed);
    running.store(false, std::memory_order_relaxed);
    return;
  }

  gridBeats = IntervalBeats();
  emitted = 0;
  beatBased = false;
  double beat = 0.0;
  // Grid point 0 stands on the beat this message landed on, so the run begins
  // where the message did. An unresolved binding has no beat to take; the first
  // wakeup that finds a clock takes it instead.
  if (ReadBeatFast(beat)) {
    beatBase = beat;
    beatBased = true;
  }
  ArmNext(bound);
  // Nothing is emitted: Max documents an immediate output for `metro` and
  // documents none here, and 0 is what a stopped clocker already reads.
}

void gClocker::RebaseGrid() {
  if (!beatOn.load(std::memory_order_relaxed)) return;
  const clockBridge::Handle bound = binding.load(std::memory_order_relaxed);
  if (bound == 0) return;

  storeGuard guard(busy);
  // Another thread is inside the grid right now; it arms whatever it decides on
  // off the same live state this call already wrote, so doing nothing here is
  // the answer rather than waiting — which a message handler may never do.
  if (!guard.Held()) return;

  gridBeats = IntervalBeats();
  emitted = 0;
  beatBased = false;
  double beat = 0.0;
  if (ReadBeatFast(beat)) {
    beatBase = beat;
    beatBased = true;
  }
  ArmNext(bound);
}

void gClocker::RetimeBeats() {
  if (!beatOn.load(std::memory_order_relaxed)) return;
  const clockBridge::Handle bound = binding.load(std::memory_order_relaxed);
  if (bound == 0) return;

  const double unit = IntervalBeats();
  // The interval stopped being tempo-relative. The unit of a run is fixed at
  // the toggle, so the grid stays where it is and the millisecond interval
  // takes effect at the next start.
  if (!(unit > 0.0)) return;

  storeGuard guard(busy);
  if (!guard.Held()) return;
  if (unit == gridBeats) return;

  double beat = 0.0;
  if (ReadBeatFast(beat)) {
    // Rebase rather than rescale: #625's rule for the millisecond path is that
    // retiming keeps the phase and does not re-trigger, and measuring the new
    // grid from *here* is what that means on a beat clock. What the outlets
    // report is untouched — the baselines are not the grid.
    beatBase = beat;
    beatBased = true;
    emitted = 0;
  }
  gridBeats = unit;
  ArmNext(bound);
}

// ─── Max's methods ──────────────────────────────────────────────────────────

void gClocker::TakeBaselines(YSE::THREAD thread) {
  // The beat first, so the pair a report subtracts belongs to one instant, and
  // both before the run is on, so a tick can never find `running` true against
  // a baseline belonging to the previous run.
  double beat = 0.0;
  baseBeat.store(ReadBeat(thread, beat) ? beat : NO_BEAT, std::memory_order_relaxed);
  baseNs.store(NowNs(), std::memory_order_relaxed);
}

void gClocker::StopRun(YSE::THREAD thread) {
  // Cleared first, and this is what makes a stop immediate however late the
  // timer is actually disarmed: `Tick()` reads it before it sends.
  running.store(false, std::memory_order_relaxed);
  StopMillis(thread);

  beatOn.store(false, std::memory_order_relaxed);
  storeGuard guard(busy);
  // A lost guard leaves the wakeup armed; it finds `beatOn` false and stops
  // there, which is why that flag lives outside the guard.
  if (!guard.Held()) return;
  CancelWakeup();
}

void gClocker::Start(YSE::THREAD thread) {
  // Stop first on either edge: a re-start must not leak the previous timer or
  // the previous wakeup, and Max's non-zero int is a re-start rather than a
  // no-op.
  StopRun(thread);

  // Which engine this run uses is decided here, once, and does not change under
  // it — `.metro`'s rule (#705).
  if (IntervalBeats() > 0.0) {
    const clockBridge::Handle bound = binding.load(std::memory_order_relaxed);
    // A tempo-relative interval with no clock bound has nothing to be measured
    // against, so the clocker does not run: falling back to the millisecond
    // interval would report on a grid nobody asked for. A later `clock <name>`
    // makes the next start work.
    if (bound == 0) return;
    TakeBaselines(thread);
    running.store(true, std::memory_order_relaxed);
    beatOn.store(true, std::memory_order_relaxed);
    StartBeats(bound);
    return;
  }

  TakeBaselines(thread);
  running.store(true, std::memory_order_relaxed);
  StartMillis(thread);
}

void gClocker::Reset(YSE::THREAD thread) {
  // Max: "this message is meaningless when the clocker is not running, since it
  // always resets to 0 anyway when stopped." Writing the baselines anyway would
  // be harmless but dishonest — nothing reads them while stopped.
  if (!running.load(std::memory_order_relaxed)) return;
  // The grid and the timer are deliberately untouched: Max's "without stopping
  // or restarting the clock; clocker continues to report the new elapsed time
  // at the same regular interval." Both units rewind together.
  TakeBaselines(thread);
}

INT_IN(Toggle) {
  (void)inlet;
  if (value == 0) {
    StopRun(thread);
    return;
  }
  // Max: "any non-zero number starts the clocker object", with no exception for
  // one that is already running — so this is `.metro`'s re-start, fresh
  // baselines and a re-phased grid. The bang method below is where Max *does*
  // give the running case a rule of its own, and it is a different one.
  Start(thread);
}

FLOAT_IN(ToggleFloat) {
  // Max: "float — same as int". Compared against zero rather than cast, which
  // is `.metro`'s rule (#711): 0.5 is a number other than 0 and therefore
  // starts, where `(int)0.5` is the stop value. NaN takes the start branch for
  // the same reason — it is not 0.
  Toggle(value == 0.f ? 0 : 1, inlet, thread);
}

BANG_IN(BangIn) {
  // Max, left inlet: "if the clocker object is not running, a bang message will
  // start the count. If the clocker object is running, a bang message will
  // reset the count." Registered on inlet 0 only, which is where Max puts it.
  if (running.load(std::memory_order_relaxed)) {
    Reset(thread);
    return;
  }
  Start(thread);
}

INT_IN(SetIntInterval) {
  (void)inlet;
  interval = value;
  // Max: "the number is the time interval, in milliseconds", which is a
  // statement about the *unit*, so a plain number puts the object back on
  // milliseconds whatever tempo-relative interval it was carrying (#725, and
  // `.metro`'s rule for the same message). A beat run in progress keeps its
  // grid; the unit of a run is fixed at the toggle.
  intervalbeats = 0.f;
  // Max's interval attribute reaches a running clocker, and does so without
  // re-phasing it (#625's rule for `.metro`'s millisecond path). It matters
  // more here than there: the reported number is measured from the start of the
  // run, so a re-phase would show up as a jump in what the user reads.
  ApplyInterval(thread);
}

FLOAT_IN(SetFloatInterval) {
  SetIntInterval((int)value, inlet, thread);
}

LIST_IN(Command) {
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
    // Max's `stop`, left inlet: "stops the clocker object" — `int 0` under
    // another name, so it goes through the one path rather than growing a
    // second way to stop.
    if (length == 4 && value.compare(begin, length, "stop", 4) == 0) {
      Toggle(0, inlet, thread);
      return;
    }

    // Max's `reset`, left inlet: "resets the elapsed time to 0 without stopping
    // or restarting the clock."
    if (length == 5 && value.compare(begin, length, "reset", 5) == 0) {
      Reset(thread);
      return;
    }

    // Max's `clock <name>` (issue #725): setclock's name is "passed as the
    // argument to a 'clock' message to numerous objects that use timing in
    // Max". The whole remainder is the name, so a clock named with spaces works
    // — `.metro`'s, `.timepoint`'s and `.tempo`'s reading of the same message.
    if (length == 5 && value.compare(begin, length, "clock", 5) == 0) {
      std::size_t nameBegin = end;
      while (nameBegin < value.size() && IsSelectorSeparator(value[nameBegin]))
        nameBegin++;
      std::size_t nameEnd = value.size();
      while (nameEnd > nameBegin && IsSelectorSeparator(value[nameEnd - 1]))
        nameEnd--;
      SetClock(value.c_str() + nameBegin, nameEnd - nameBegin, thread);
      return;
    }

    // A number on its own means what a bare number means. Everything else —
    // Max's `quantize`, a bars.beats.units figure, an unknown word — does
    // nothing, deliberately, rather than being read as some command it is not.
    float number = 0.f;
    if (!ReadNumericToken(value.c_str() + begin, length, number)) return;
    ToggleFloat(number, inlet, thread);
    return;
  }

  // Max's list / anything method on the right inlet: "a list may be used to
  // specify time in one of the Max time formats." The tempo-relative half is
  // read first, because its tick spelling *starts* with a number — `1440 ticks`
  // read as a leading number would silently become 1440 ms, an interval nobody
  // asked for and wrong by whatever the tempo is. Read through the shared
  // `timeValue.h`, so this object and `.metro` cannot drift apart on one syntax.
  if (inlet != 1) return;

  double beats = 0.0;
  if (ReadBeatTime(value.c_str() + begin, value.size() - begin, beats)) {
    intervalbeats = (Flt)beats;
    RetimeBeats();
    return;
  }

  // A leading number means the same thing a bare number does; everything else —
  // bars.beats.units, an unknown word — does nothing, deliberately, rather than
  // being read as some number it is not.
  float number = 0.f;
  if (!ReadNumericToken(value.c_str() + begin, length, number)) return;
  SetFloatInterval(number, inlet, thread);
}

// ─── the report ─────────────────────────────────────────────────────────────

void gClocker::Report(YSE::THREAD thread) {
  // Both figures are read before either is sent. An outlet wired back round to
  // the left inlet resets or stops the run mid-send, and the pair the patch
  // receives has to be the one measurement it asked for rather than half of
  // each.
  double beats = 0.0;
  bool haveBeats = false;
  double beat = 0.0;
  if (ReadBeatFast(beat)) {
    double base = baseBeat.load(std::memory_order_relaxed);
    if (std::isnan(base)) {
      // The run began before its clock resolved, or before anything created it.
      // This report is where the beat measurement starts — the same rule
      // `.metro` applies to its grid, for the same reason: the alternative is
      // a run that never reports beats at all because it was a moment early.
      baseBeat.store(beat, std::memory_order_relaxed);
      base = beat;
    }
    beats = beat - base;
    haveBeats = true;
  }
  const int ms = (int)Elapsed();

  // Right to left, Max's outlet order.
  if (haveBeats) outputs[1].SendFloat((float)beats, thread);
  outputs[0].SendInt(ms, thread);
}

void gClocker::Tick() {
  // A stop that has already landed wins, whatever the timer is still doing:
  // this is what lets both wait-free stop routes — the audio-thread one and the
  // one taken from inside this very callback — be immediate where it counts.
  if (!running.load(std::memory_order_relaxed)) return;

  // Marked for the whole frame: the send below can come back round to this
  // object's own inlets, where the blocking half of the bridge would take a
  // mutex a pool job may be holding while it waits for this callback to return.
  // The mark travels with the thread and not with the send, so a cycle closed
  // through a `.t` is covered exactly as a single cord is.
  tickFrameGuard frame(this);

  // Picks up an interval stored by the parameter path, which has no way to call
  // in here itself (#625's second route). Requesting rather than applying is
  // load-bearing: the reconciler may be holding this slot right now, blocked
  // inside ClearTimer waiting for this very callback, so a Tick() that waited
  // for the slot would deadlock against a stop. timerBridge drops a request
  // that does not move the interval, so the common tick costs one relaxed load
  // and a compare.
  if (timerSlot != 0) TimerBridge().RequestPeriod(timerSlot, IntervalMs());

  Report(T_GUI);
}

void gClocker::DeliverDeferred(const deferredMessage& msg, YSE::THREAD thread) {
  (void)msg;
  bool report = false;
  {
    storeGuard guard(busy);
    // A lost guard drops this wakeup without re-arming, which is safe here for
    // `.metro`'s reason: the thread holding the grid is a start, a rebase or a
    // retime, and every one of them arms a wakeup of its own before it lets go.
    if (!guard.Held()) return;

    // Whatever armed this wakeup has fired; the handle it left behind is stale.
    pending = 0;
    if (!beatOn.load(std::memory_order_relaxed)) return;
    if (!running.load(std::memory_order_relaxed)) return;
    const clockBridge::Handle bound = binding.load(std::memory_order_relaxed);
    if (bound == 0) return;

    double beat = 0.0;
    if (!ReadBeatFast(beat)) {
      // The clock does not exist yet, so no time has passed on it. Re-arm and
      // let the bridge's own Poll resolve the name; this is what makes a clock
      // named before anything creates it start the run when it appears.
      ArmNext(bound);
      return;
    }

    // The run started before its clock resolved; this is the wakeup that found
    // it, and grid point 0 stands here.
    if (!beatBased) {
      beatBase = beat;
      beatBased = true;
      emitted = 0;
    }

    // The SetParams route into the interval (#625's second route), which stores
    // from the audio thread and notifies nobody. Rebasing rather than rescaling
    // keeps the phase, as it does in RetimeBeats.
    const double unit = IntervalBeats();
    if (unit > 0.0 && unit != gridBeats) {
      gridBeats = unit;
      beatBase = beat;
      emitted = 0;
    }
    if (!(gridBeats > 0.0)) {
      // Nothing to divide by. Cannot happen from a start, which refuses a
      // non-positive interval, but a grid is a grid only while it has a step.
      beatOn.store(false, std::memory_order_relaxed);
      return;
    }

    // The time base: how many intervals the *clock* has moved, never how many
    // wakeups have arrived. See the header — this is the whole reason the grid
    // does not drift, and the reason it does not cap at one report per block
    // when the interval is shorter than a block.
    double elapsed = (beat - beatBase) / gridBeats;
    // Also the NaN case, no comparison accepting one. A beat position that went
    // backwards reports nothing rather than winding the grid back.
    if (!(elapsed > 0.0)) elapsed = 0.0;
    // Far past any grid index a running clock can reach, and small enough that
    // the int64 below cannot overflow.
    if (elapsed > 1.0e12) elapsed = 1.0e12;

    const std::int64_t target = (std::int64_t)std::floor(elapsed);
    // One report however many grid points this wakeup covered: the number is
    // the time *now*, so copies of it would be copies of one reading rather
    // than the reports that were missed — see the header. The index still
    // advances to where the clock actually is, so nothing accumulates into the
    // next wakeup.
    if (target > emitted) report = true;
    emitted = target;

    // Re-armed before anything is emitted, so the next wakeup is on the grid
    // whatever this one turns out to send. A send that stops the clocker — a
    // toggle coming back through a cord — takes it out again on the way past.
    ArmNext(bound);
  }

  if (report) Report(thread);
}

gClocker::~gClocker() {
  if (timerSlot != 0) {
    // Giving the slot back stops the timer and *waits out* a callback already
    // in flight, `timerBridge::Release` keeping `timerThread::ClearTimer`'s
    // handshake for exactly this caller. That handshake is the point:
    // everything Tick() touches is still alive here — `outputs` is a base-class
    // member, destroyed only after this body — and a destructor never runs on
    // the audio thread, so it is allowed to block.
    TimerBridge().Release(timerSlot);
    timerSlot = 0;
  }
  // The beat wakeup needs no such handshake: a pending scheduler message is
  // re-resolved against the block's pinned GraphState before it is delivered,
  // and an object being destroyed is absent from it, so the message is dropped
  // rather than delivered to a dying object. `.metro` and `.delay` rely on the
  // same rule.
}
