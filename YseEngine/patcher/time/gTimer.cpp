
#include "gTimer.h"
#include "../../clock/clockManager.h"
#include "../pObjectList.hpp"
#include "../patcherImplementation.h"
#include <chrono>
#include <cmath>
#include <cstdint>
#include <limits>

using namespace YSE::PATCHER;

#define className gTimer

namespace {

  constexpr char kStartInletDoc[] =
      "A bang marks the first of the two events — Max's 'in left inlet: starts or restarts the "
      "timer'. It takes the instant it arrived at and the bound clock's beat position at that same "
      "instant, and everything the right inlet later reports is measured from there. Banging it "
      "again simply moves the mark, so a timer is re-used by re-starting it rather than by being "
      "cleared. Nothing is emitted here: an interval needs two events and this is one of them.";

  constexpr char kReportInletDoc[] =
      "A bang marks the second event and sends the interval — Max's 'in right inlet: sends out the "
      "time elapsed since the timer was started'. It *measures* rather than stops: the mark stays "
      "where the left inlet put it, so banging this inlet again reports a larger interval from the "
      "same start, which is how split times are taken. A bang before the timer has ever been "
      "started emits nothing at all, there being no interval between one event and no event.";

  constexpr char kMsOutletDoc[] =
      "The interval between the two events, in milliseconds. It is measured rather than counted: "
      "both events read the engine's monotonic clock (std::chrono::steady_clock, the same source "
      "the patcher's timer thread schedules on and the same one the engine times its own ticks "
      "with) and the reading is their difference, so a bang that the OS delivered late is timed at "
      "the instant it really arrived and the figure never disagrees with the events that produced "
      "it. Sent after the beats outlet, which is Max's right-to-left outlet order.";

  constexpr char kBeatsOutletDoc[] =
      "The same interval expressed in beats of the domain clock named by the creation argument — "
      "issue #506's 'second outlet reporting the interval in beats against a bound domain clock', "
      "and what Max's own right outlet does against his transport. The clock's beat position is "
      "read at each of the two events and subtracted, so a tempo change, a ramp or a pause "
      "between them is accounted for exactly; this is emphatically not the millisecond figure "
      "divided by a BPM number, and the two outlets are not two spellings of one value. Sent "
      "first, Max's outlets firing right to left. Nothing at all is emitted when there is no beat "
      "to report: no creation argument, a clock nothing has created, or a clock that only appeared "
      "after the timer was started and so has no baseline in this interval.";

} // namespace

CONSTRUCT() {
  ADD_IN_0;
  REG_BANG_IN(Start);

  ADD_IN_1;
  REG_BANG_IN(Report);

  ADD_OUT_FLOAT; // the interval in milliseconds
  ADD_OUT_FLOAT; // the interval in beats

  ADD_PARAM(clockname);

  ADD_DESCRIPTION(
      "Reports the elapsed time between two events — Max's timer, and the first patcher object "
      "that consumes time rather than producing it (issue #506). A bang in the left inlet marks "
      "the start, a bang in the right sends the interval since that mark: the tempo of a tapped-in "
      "beat, the length of a gesture, how long a note was held. The right inlet measures rather "
      "than stops — the mark stays put, so banging it repeatedly gives split times from the one "
      "start — and a report before any start emits nothing, there being no interval between one "
      "event and no event. The milliseconds are measured, never counted: both events read the "
      "engine's monotonic clock (std::chrono::steady_clock, the same source the patcher's timer "
      "thread schedules on), so a '.metro' tick that the OS delivered late is timed at the instant "
      "it really arrived. The second outlet reports the same interval in beats of a YSE domain "
      "clock named by the creation argument ('.timer main'), which is what Max's own right outlet "
      "does against his transport: the beat position is read at each event and subtracted, so a "
      "tempo change or a ramp between them is accounted for exactly rather than being papered over "
      "by dividing milliseconds by a BPM figure. The beats outlet stays silent when there is no "
      "clock to read. This object never creates a clock and never destroys one — '.transport' is "
      "the sole creator (issue #513) — so a name nobody has claimed simply has no beats until "
      "something claims it. Max's 'clock <name>' message stays out because it names a setclock, an "
      "alternative millisecond clock YSE does not have, and reusing the word for the beat domain "
      "would give a Max message a different meaning in a patch that looks like Max's; the format "
      "attribute stays out with it, the two units this object has being spelled by its two "
      "outlets. Calculate() does nothing, and neither inlet allocates, locks or blocks when it "
      "turns out to be running on the audio callback: the clock is read through the patcher's "
      "bridge there, which is two acquire loads, and by name only off it.");
  ADD_CATEGORY(pCategory::TIME);
  INLET_DOC(0, "start", kStartInletDoc, "bang");
  INLET_DOC(1, "report", kReportInletDoc, "bang");
  OUTLET_DOC(0, "ms", kMsOutletDoc, "0+ ms");
  OUTLET_DOC(1, "beats", kBeatsOutletDoc, "beats");
  PARAM_DOC("clockname", "",
            "The domain clock the beats outlet measures against — YSE's answer to Max's transport "
            "attribute. Clocks are addressed by name across the whole engine, so this is the same "
            "string the host passes to createClock and the same one a '.transport <name>' drives. "
            "Empty (the default) leaves the beats outlet silent and the object a pure millisecond "
            "timer. The name is bound when the object is added to a patcher — bound, not created: "
            "a clock nobody has made yet leaves the beats outlet quiet, and it starts reporting "
            "once the clock exists and a fresh start has a beat position to measure from. There is "
            "no message to re-point it, deliberately: Max's 'clock' word names a setclock and not "
            "a transport, and '.transport' sets the same precedent of reading its clock name once.",
            "");
}

// ─── the time base ──────────────────────────────────────────────────────────

std::int64_t gTimer::NowNs() {
  // steady_clock, and not by preference: it is `timerThread::Clock`, so a
  // `.metro` tick that starts this timer and the reading of that start are two
  // views of one timeline. It is also `INTERNAL::time`'s source (#667) and
  // `MIDI::nowNs`'s. A *wall* clock would be wrong twice over — not monotonic,
  // so an NTP correction between the two events would stretch, shrink or
  // reverse the interval.
  //
  // RT-safe: a QueryPerformanceCounter on Windows and a vDSO clock_gettime on
  // Linux and Android. INTERNAL::time::update reads it from the audio callback
  // on every block already.
  return std::chrono::duration_cast<std::chrono::nanoseconds>(
             std::chrono::steady_clock::now().time_since_epoch())
      .count();
}

double gTimer::ElapsedMs() const {
  if (!started.load(std::memory_order_acquire)) return 0.0;
  const std::int64_t ns = NowNs() - startNs.load(std::memory_order_relaxed);
  // Negative cannot happen on a monotonic clock, but the baseline is written by
  // another thread and a reader that trusted the subtraction would report a
  // huge interval if it ever did.
  if (ns <= 0) return 0.0;
  return (double)ns / 1000000.0;
}

// ─── the clock: bound, never created ────────────────────────────────────────

void gTimer::SetParent(pObject* parent) {
  pObject::SetParent(parent);

  // A standalone object has no patcher, so no bridge: nothing to bind, and a
  // millisecond timer is all it is.
  if (parent == nullptr) return;
  if (clockname.empty()) return;

  clockBridge* clocks = Clocks();
  if (clocks == nullptr) return;

  // Bind only. `.transport` creates clocks and this object does not (#513), so
  // there is no CLOCK::Manager() call here at all. Binding is idempotent by
  // name, so this shares a slot with the `.transport` driving the clock rather
  // than costing one of its own.
  binding.store(clocks->Bind(clockname.c_str(), clockname.size()), std::memory_order_relaxed);
}

bool gTimer::OnAudioThread(YSE::THREAD thread) const {
  // The `THREAD` tag is dispatch semantics, not thread identity: in-patcher
  // delivery dispatches T_DSP, and the drains at the top of Calculate dispatch
  // T_GUI *from the audio callback*. Only the patcher knows, and only since
  // #690 — so ask it, exactly as `.metro`, `.clocker` and `.transport` do, and
  // pass the tag itself on unaltered. A standalone object has no patcher and is
  // never rendered, so it answers false.
  if (parent == nullptr) return false;
  return static_cast<patcherImplementation*>(parent)->CallingThread(thread) == YSE::T_DSP;
}

bool gTimer::ReadBeat(YSE::THREAD thread, double& beat) const {
  // No binding, no beats — which covers a nameless object, a full bridge, and a
  // standalone one with no patcher to bind through. The by-name route below is
  // deliberately *not* a way around that: it exists only to cover the gap
  // between binding a name and the pool resolving it, and an object that never
  // bound a name has no such gap to cover.
  const clockBridge* clocks = Clocks();
  if (clocks == nullptr) return false;
  const clockBridge::Handle bound = binding.load(std::memory_order_relaxed);
  if (bound == 0) return false;

  // The wait-free route first, whatever the thread: two acquire loads, and the
  // only one the audio callback may take. It also keeps #707's rule — a clock
  // the host destroyed under a resolved binding reads its frozen beat rather
  // than disappearing mid-measurement, so an interval that started on a live
  // clock still closes on the same one.
  if (clocks->Beat(bound, beat)) return true;

  // The binding has not resolved yet — resolution is a background hop, since
  // the name lookup takes the clock manager's mutex. Off the callback this
  // thread may take that mutex itself, which is what lets a measurement made in
  // the same breath as the object's creation have beats at all. On the callback
  // there is simply nothing to report.
  if (OnAudioThread(thread)) return false;
  if (!CLOCK::Manager().clockExists(clockname)) return false;
  beat = CLOCK::Manager().beatPosition(clockname);
  return true;
}

// ─── Max's two events ───────────────────────────────────────────────────────

BANG_IN(Start) {
  (void)inlet;
  // Max, left inlet: "starts or restarts the timer." Both baselines are taken
  // from the one instant, and the beat one first, so the release store below
  // publishes a matched pair.
  double beat = 0.0;
  const bool haveBeat = ReadBeat(thread, beat);
  startBeat.store(haveBeat ? beat : std::numeric_limits<double>::quiet_NaN(),
                  std::memory_order_relaxed);
  startNs.store(NowNs(), std::memory_order_relaxed);
  started.store(true, std::memory_order_release);
}

BANG_IN(Report) {
  (void)inlet;
  // Max, right inlet: "sends out the time elapsed since the timer was started."
  // Nothing at all before the first start — see the header on why silence beats
  // a 0 that no pair of events produced.
  if (!started.load(std::memory_order_acquire)) return;

  const std::int64_t ns = NowNs() - startNs.load(std::memory_order_relaxed);
  const double ms = ns <= 0 ? 0.0 : (double)ns / 1000000.0;

  double beats = 0.0;
  bool haveBeats = false;
  const double base = startBeat.load(std::memory_order_relaxed);
  // NaN is the "there was no clock when this interval began" sentinel, so the
  // beats half is skipped rather than reported as a difference from nothing.
  if (!std::isnan(base)) {
    double beat = 0.0;
    if (ReadBeat(thread, beat)) {
      beats = beat - base;
      haveBeats = true;
    }
  }

  // Both figures are read before either is sent. An outlet wired back round to
  // the left inlet re-starts the timer mid-send, and the pair the patch receives
  // has to be the one measurement it asked for rather than half of each.
  //
  // Right to left, Max's outlet order.
  if (haveBeats) outputs[1].SendFloat((float)beats, thread);
  outputs[0].SendFloat((float)ms, thread);
}
