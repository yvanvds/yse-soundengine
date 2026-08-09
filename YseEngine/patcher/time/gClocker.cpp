
#include "gClocker.h"
#include "../pListArgs.h"
#include "../pObjectList.hpp"
#include "../pSelector.h"
#include "../patcherImplementation.h"
#include <chrono>
#include <cstddef>
#include <cstdint>

using namespace YSE::PATCHER;

#define className gClocker

namespace {

  constexpr char kHotInletDoc[] =
      "Starts and stops the clocker, and carries its two messages. Max: 'any non-zero number "
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
      "anyway when stopped'. Everything else does nothing, deliberately, rather than being read "
      "as some command it is not.";

  constexpr char kColdInletDoc[] =
      "Sets the reporting interval in milliseconds — Max's 'interval' attribute, and the object's "
      "creation argument. An int, a float or a list with a leading number all set it. Setting it "
      "under a running clocker retimes the tick without re-phasing it, so the elapsed time the "
      "next report carries follows on from the last rather than jumping: the number is measured "
      "from the start of the run and nothing here moves that. Tempo-relative intervals — a note "
      "value, a tick count, bars.beats.units — stay out, and so does 'clock <name>': they need a "
      "domain clock, and a clocker on one raises a question this object does not answer (whether "
      "'elapsed' is then milliseconds or beats). A message that is not one whole number therefore "
      "does nothing at all, and that includes the ones that *start* with a number: '1440 ticks' "
      "is not read as 1440 milliseconds, which would look like it worked and be wrong by whatever "
      "the tempo is.";

  constexpr char kOutletDoc[] =
      "The elapsed time in milliseconds, sent once per interval while the clocker runs. It is "
      "*measured* rather than counted: each report reads the engine's monotonic clock and "
      "subtracts the instant the run started, so a tick the OS delivered late reports the time it "
      "actually arrived at and the run never drifts away from real time. Multiplying a tick count "
      "by the interval — the obvious implementation — would report the nominal time forever and "
      "diverge a little further on every tick. Nothing is sent while the clocker is stopped, and "
      "the first report comes one interval after the start rather than immediately: Max documents "
      "an immediate output for 'metro' and documents none here.";

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
  // baseline are written here and now, and `Tick()` reads `running` before it
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

  ADD_PARAM(interval);

  // Max: "if there is no argument, the initial time interval is set to 5
  // milliseconds."
  interval = 5;

  // The slot this object's timer lives in for the rest of its life (issue
  // #718). Taken here, on the control thread, so no message handler ever has
  // to; given back in the destructor. A refusal — a process holding
  // timerBridge::CAPACITY timer owners at once — costs this one its clock,
  // silently, since a log line is not this constructor's to emit.
  timerSlot = TimerBridge().Claim(&gClocker::TickTrampoline, this);

  ADD_DESCRIPTION(
      "Elapsed-time reporter — Max's clocker, 'a metronome that reports the time elapsed since it "
      "was started' (issue #505). Where '.metro' bangs at an interval, this sends a number, and "
      "the number is how long the run has been going in milliseconds. That is what takes an "
      "object out of the patch: an envelope, a ramp or a progress readout driven by a '.metro' "
      "needs a '.counter' behind it to turn ticks into a position, and that counter is wrong the "
      "moment a tick is late or dropped. The elapsed time here is measured rather than counted — "
      "each report reads the engine's monotonic clock (std::chrono::steady_clock, the same source "
      "the patcher's timer thread schedules on and the same one the engine times its own ticks "
      "with) and subtracts the instant the run started, so an OS timer that wakes late reports "
      "the time it really arrived at and the run never drifts. A non-zero int or float in the "
      "left inlet starts it and 0 stops it, 'stop' is Max's word for the same, and a bang starts "
      "a stopped clocker or resets a running one's count — Max gives bang a rule of its own here, "
      "so unlike '.metro' it is not the same method as int: a bang moves the baseline and leaves "
      "the tick alone, where a non-zero int re-starts the run and re-phases the tick with it. The "
      "message 'reset' spells that reset out. The right inlet sets the interval in milliseconds "
      "(5 by default, as in Max) and retimes a running clocker without re-phasing it. "
      "Tempo-relative intervals, 'clock <name>', quantize, autostart and defer stay out: they "
      "need a domain clock, and a clocker on one raises a question this object does not answer. "
      "Calculate() does nothing, and no message path allocates, locks or blocks when it turns out "
      "to be running on the audio callback — the timer is armed and disarmed through timerBridge, "
      "which takes a wait-free request there and does the locking work on the background pool, "
      "the baseline is one atomic store and one clock read, and the command words are matched "
      "against the message in place.");
  ADD_CATEGORY(pCategory::TIME);
  INLET_DOC(0, "on/off", kHotInletDoc, "0 or 1, bang, 'stop', 'reset'");
  INLET_DOC(1, "interval", kColdInletDoc, "1+ ms");
  OUTLET_DOC(0, "elapsed", kOutletDoc, "0+ ms");
  PARAM_DOC("interval", "5",
            "The reporting interval in milliseconds — Max's 'interval' attribute and the object's "
            "one creation argument, whose default Max documents as 5 ms. Floored at 1, which is "
            "the finest interval the patcher's millisecond timer keeps. Re-editing it while the "
            "clocker runs retimes the tick without re-phasing it, so the elapsed time carries on "
            "rather than jumping. Whether the clocker is running, and how long it has been "
            "running, are run-time state and are not saved.",
            "1+ ms");
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

void gClocker::Start(YSE::THREAD thread) {
  // The baseline before the run is on, so a tick can never find `running` true
  // against a baseline belonging to the previous run.
  baseNs.store(NowNs(), std::memory_order_relaxed);
  running.store(true, std::memory_order_relaxed);

  if (timerSlot == 0) return;
  // WantStart bumps the bridge's `startSeq`, so a start against a timer that is
  // already armed replaces it rather than retiming it — the re-phase Max's
  // "starts the clocker object" means on a running object.
  if (InOwnTick(this) || OnAudioThread(thread))
    TimerBridge().RequestStart(timerSlot, IntervalMs());
  else
    TimerBridge().ApplyStart(timerSlot, IntervalMs());
}

void gClocker::Stop(YSE::THREAD thread) {
  // Cleared first, and this is what makes a stop immediate however late the
  // timer is actually disarmed: `Tick()` reads it before it sends.
  running.store(false, std::memory_order_relaxed);

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

void gClocker::Reset() {
  // Max: "this message is meaningless when the clocker is not running, since it
  // always resets to 0 anyway when stopped." Writing the baseline anyway would
  // be harmless but dishonest — `Elapsed()` does not read it while stopped.
  if (!running.load(std::memory_order_relaxed)) return;
  // The timer is deliberately untouched: Max's "without stopping or restarting
  // the clock; clocker continues to report the new elapsed time at the same
  // regular interval."
  baseNs.store(NowNs(), std::memory_order_relaxed);
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

// ─── Max's methods ──────────────────────────────────────────────────────────

INT_IN(Toggle) {
  (void)inlet;
  if (value == 0) {
    Stop(thread);
    return;
  }
  // Max: "any non-zero number starts the clocker object", with no exception for
  // one that is already running — so this is `.metro`'s re-start, a fresh
  // baseline and a re-phased tick. The bang method below is where Max *does*
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
    Reset();
    return;
  }
  Start(thread);
}

INT_IN(SetIntInterval) {
  (void)inlet;
  interval = value;
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

  // Whether the message is *nothing but* that one token. A leading number is
  // not enough to call a message a number, and this is the trap `.metro`
  // documents (#705): Max's tick spelling starts with a digit, so `1440 ticks`
  // read as a leading number would silently become 1440 **milliseconds** — an
  // interval nobody asked for, wrong by whatever the tempo is. This object does
  // not read tempo-relative time at all (see the header), so the honest answer
  // to every one of those spellings is to do nothing, and telling them apart
  // from a plain number is what this is for.
  std::size_t after = end;
  while (after < value.size() && IsSelectorSeparator(value[after]))
    after++;
  const bool wholeMessageIsOneToken = after == value.size();

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
      Reset();
      return;
    }

    // A number on its own means what a bare number means.
    if (!wholeMessageIsOneToken) return;
    float number = 0.f;
    if (!ReadNumericToken(value.c_str() + begin, length, number)) return;
    ToggleFloat(number, inlet, thread);
    return;
  }

  // Max's list / anything on the right inlet: "a list may be used to specify
  // time in one of the Max time formats." Only the millisecond one is read here
  // — a note value, a tick count or a bars.beats.units figure needs a domain
  // clock, which this object does not bind (see the header). Every one of those
  // does nothing, deliberately, rather than being read as some number it is
  // not: `1440 ticks` becoming 1440 milliseconds would be the worst of the
  // available answers, since it looks like it worked.
  if (inlet != 1) return;
  if (!wholeMessageIsOneToken) return;
  float number = 0.f;
  if (!ReadNumericToken(value.c_str() + begin, length, number)) return;
  SetFloatInterval(number, inlet, thread);
}

// ─── the tick ───────────────────────────────────────────────────────────────

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

  // Measured, never counted — see the header. `Elapsed()` is clamped to what an
  // int outlet can carry, so the cast is defined.
  outputs[0].SendInt((int)Elapsed(), T_GUI);
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
}
