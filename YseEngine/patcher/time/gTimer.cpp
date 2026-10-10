
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

  constexpr char kStartInletDoc[] = "A bang marks the start. Another bang moves the start to now.";

  constexpr char kReportInletDoc[] =
      "A bang sends the time since the start. The start stays where it is.";

  constexpr char kMsOutletDoc[] = "The time since the start, in milliseconds.";

  constexpr char kBeatsOutletDoc[] =
      "The beats the named clock moved in the same time, sent just before the milliseconds. "
      "Silent without a clock.";

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
      "Measures the time between a bang in the left inlet and a bang in the right inlet, in "
      "milliseconds and, with a clock name as argument, in beats.");
  ADD_CATEGORY(pCategory::TIME);
  INLET_DOC(0, "start", kStartInletDoc, "bang");
  INLET_DOC(1, "report", kReportInletDoc, "bang");
  OUTLET_DOC(0, "ms", kMsOutletDoc, "0+ ms");
  OUTLET_DOC(1, "beats", kBeatsOutletDoc, "beats");
  PARAM_DOC("clockname", "",
            "The domain clock to measure beats on. Empty measures milliseconds only.", "");
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
