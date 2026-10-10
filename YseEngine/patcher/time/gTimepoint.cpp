
#include "gTimepoint.h"
#include "../pListArgs.h"
#include "../pObjectList.hpp"
#include "../pSelector.h"
#include <cmath>
#include <cstddef>

using namespace YSE::PATCHER;

#define className gTimepoint

namespace {

  constexpr char kInletDoc[] =
      "A number sets the beat position and arms the object. 'active 0|1' disarms or re-arms it; "
      "'clock <name>' changes the clock.";

  constexpr char kOutletDoc[] = "A bang, once, when the clock passes the beat position.";

  // Read one whitespace-separated number out of `text` starting at `from`.
  // Bounded and allocation-free: this runs on whichever thread sent the message.
  bool ReadFloatArg(const std::string& text, std::size_t from, float& out) {
    std::size_t i = from;
    while (i < text.size() && IsSelectorSeparator(text[i]))
      i++;
    std::size_t end = i;
    while (end < text.size() && !IsSelectorSeparator(text[end]))
      end++;
    if (end <= i) return false;
    return ReadNumericToken(text.c_str() + i, end - i, out);
  }

} // namespace

CONSTRUCT() {
  ADD_IN_0;
  REG_FLOAT_IN(SetFloatTime);
  REG_INT_IN(SetIntTime);
  REG_LIST_IN(Command);

  ADD_OUT_BANG;

  ADD_PARAM(clockname);
  ADD_PARAM(time);
  ADD_PARAM(active);

  time = 0.f;
  active = 1;

  ADD_DESCRIPTION(
      "Sends a bang once when a named domain clock passes a beat position, as in '.timepoint main "
      "128'.");
  ADD_CATEGORY(pCategory::TIME);
  INLET_DOC(0, "time", kInletDoc, "beats, 'clock <name>', 'active 0|1'");
  OUTLET_DOC(0, "out", kOutletDoc, "");
  PARAM_DOC("clockname", "",
            "The domain clock to watch. Empty watches nothing. A 'clock <name>' message does not "
            "change it.",
            "");
  PARAM_DOC("time", "0", "The beat position to bang at.", "beats");
  PARAM_DOC("active", "1", "1 watches the clock, 0 does not.", "0 or 1");
}

// ─── the clock: bound, never created ────────────────────────────────────────

void gTimepoint::SetParent(pObject* parent) {
  pObject::SetParent(parent);

  // A standalone object has no patcher, so no bridge and no scheduler: nothing
  // to bind and nothing to arm.
  if (parent == nullptr) return;
  if (clockname.empty()) return;

  clockBridge* clocks = Clocks();
  if (clocks == nullptr) return;

  // Bind only. `.transport` creates clocks and this object does not — see the
  // header — so there is no CLOCK::Manager() call here at all, which is also
  // why nothing about this hook has to be the control thread. Binding is
  // idempotent by name, so this shares a slot with the `.transport` driving the
  // clock rather than costing one of its own.
  binding.store(clocks->Bind(clockname.c_str(), clockname.size()), std::memory_order_relaxed);
  Rearm();
}

const char* gTimepoint::ClockName() const {
  const clockBridge::Handle bound = binding.load(std::memory_order_relaxed);
  if (bound == 0) return "";
  const clockBridge* clocks = Clocks();
  if (clocks == nullptr) return "";
  return clocks->NameOf(bound);
}

void gTimepoint::SetClock(const char* name, std::size_t length) {
  // A bare `clock` gives the clock back, which for a reader means it stops
  // watching: there is no millisecond fallback here, an absolute beat position
  // having no meaning on a clock that does not count beats.
  if (name == nullptr || length == 0) {
    binding.store(0, std::memory_order_relaxed);
    Rearm();
    return;
  }

  clockBridge* clocks = Clocks();
  if (clocks == nullptr) return;

  // Wait-free: a bounded walk over the patcher's binding table and a memcpy of
  // the name into a slot that already exists. The name is *not* looked up here —
  // that takes the clock manager's mutex and happens on the background pool.
  const clockBridge::Handle bound = clocks->Bind(name, length);
  // A refusal (the table is full, or the name is longer than a slot holds)
  // leaves the object on whatever clock it was on rather than silently watching
  // nothing.
  if (bound == 0) return;
  binding.store(bound, std::memory_order_relaxed);
  Rearm();
}

// ─── arming ─────────────────────────────────────────────────────────────────

double gTimepoint::Target() const {
  return (double)time.load(std::memory_order_relaxed);
}

void gTimepoint::CancelWakeup() {
  armed.store(false, std::memory_order_relaxed);
  if (pending == 0) return;
  messageScheduler* scheduler = Scheduler();
  if (scheduler != nullptr) scheduler->Cancel(pending);
  pending = 0;
}

void gTimepoint::ArmWakeup(clockBridge::Handle bound, double ahead) {
  messageScheduler* scheduler = Scheduler();
  if (scheduler == nullptr) return;
  // The tag is unused: this object has only one kind of pending message, and
  // which of the two arms it is, is `based`.
  pending = scheduler->ScheduleBangOnClock(this, 0, bound, ahead);
  // 0 means the patcher-wide pending set is full — counted by
  // messageScheduler::Dropped(). The object is left unarmed rather than banging
  // at some other time, and a new time value or an `active 1` tries again.
  armed.store(pending != 0, std::memory_order_relaxed);
}

void gTimepoint::Rearm() {
  storeGuard guard(busy);
  // Another thread is inside the wakeup state right now. It is a re-arm or a
  // delivery, and it arms whatever it decides on off the same live `time` and
  // `active` this call already wrote, so doing nothing here is the answer rather
  // than waiting — which a message handler may never do.
  if (!guard.Held()) return;
  RearmLocked();
}

void gTimepoint::RearmLocked() {
  CancelWakeup();
  based = false;

  if (active.load(std::memory_order_relaxed) == 0) return;
  const clockBridge::Handle bound = binding.load(std::memory_order_relaxed);
  if (bound == 0) return;

  const double target = Target();
  // A NaN target is not a position. Arming on one would ask the scheduler for a
  // distance it reads as 0, and the delivery would then never find `beat >=
  // target` — a wakeup per block, forever, for a target that cannot be reached.
  if (std::isnan(target)) return;

  const clockBridge* clocks = Clocks();
  if (clocks == nullptr) return;

  double beat = 0.0;
  if (clocks->Beat(bound, beat)) {
    // Already there: the crossing happened before anything was watching, and a
    // domain clock has no rewind to bring it round again. Spent — see the
    // header on why this, and not a bang, is the useful answer.
    if (beat >= target) return;
    based = true;
    ArmWakeup(bound, target - beat);
    return;
  }

  // The binding has not resolved yet, so there is no beat to measure from. 0
  // beats is stored relative to clockBridge::ResolveBeat and delivered at the
  // first block after the clock appears; that delivery is the object's first
  // sight of the clock and takes the baseline. A clock that never appears never
  // delivers it, which is the honest reading of a position on a clock that does
  // not exist.
  ArmWakeup(bound, 0.0);
}

void gTimepoint::DeliverDeferred(const deferredMessage& msg, YSE::THREAD thread) {
  (void)msg;
  bool fire = false;
  {
    storeGuard guard(busy);
    // A lost guard drops this wakeup without re-arming, which is safe here for
    // `.metro`'s reason: the thread holding the state is a re-arm, and it arms a
    // wakeup of its own before it lets go.
    if (!guard.Held()) return;

    // Whatever armed this wakeup has fired; the handle it left behind is stale.
    pending = 0;
    // Cancelled by a thread that could not take the guard, or disarmed since.
    if (!armed.load(std::memory_order_relaxed)) return;
    armed.store(false, std::memory_order_relaxed);
    if (active.load(std::memory_order_relaxed) == 0) return;

    const clockBridge::Handle bound = binding.load(std::memory_order_relaxed);
    if (bound == 0) return;
    const clockBridge* clocks = Clocks();
    double beat = 0.0;
    if (clocks == nullptr || !clocks->Beat(bound, beat)) {
      // A deadline on an unresolved binding is never due, so this cannot be
      // reached by the scheduler's own test; a clock unbound between the scan
      // and the delivery could. Nothing has been reached on a clock that is not
      // there, so re-arm the baseline probe and wait for it.
      if (clocks != nullptr) ArmWakeup(bound, 0.0);
      return;
    }

    const double target = Target();
    if (std::isnan(target)) return;

    if (!based) {
      // The object's first sight of its clock: the baseline probe coming back.
      // It decides the same question RearmLocked would have decided had the
      // binding been resolved then — but it measures from where the clock stood
      // when it *appeared*, not from where it stands now. `Poll` retries an
      // unresolved binding only every clockBridge::RESOLVE_INTERVAL_BLOCKS
      // blocks, so "now" can be a long way past the moment the object started
      // watching, and comparing against it would silently swallow every target
      // inside that window. Measuring from ResolveBeat turns the resolve latency
      // into a late bang instead: `ahead` comes out negative, the scheduler
      // reads that as 0, and the delivery lands on the next block.
      based = true;
      if (clocks->ResolveBeat(bound) >= target) return;
      ArmWakeup(bound, target - beat);
      return;
    }

    if (beat < target) {
      // `now + (target - now)` is not exactly `target` in binary, so a deadline
      // can land a hair short. Re-arm the remainder rather than banging early:
      // a block's slip at worst, and never a bang at the wrong beat.
      ArmWakeup(bound, target - beat);
      return;
    }

    fire = true;
  }

  // Outside the guard: a patch that wires this outlet back into this inlet must
  // not find the wakeup state held. The tag travels unchanged, as `.metro`'s and
  // `.delay`'s deliveries pass it on.
  if (fire) outputs[0].SendBang(thread);
}

// ─── Max's methods ──────────────────────────────────────────────────────────

FLOAT_IN(SetFloatTime) {
  (void)inlet;
  (void)thread;
  // Max: "sets the time value at which the timepoint object will send a bang".
  // Always a re-arm, which is what makes a spent timepoint re-usable.
  time.store(value, std::memory_order_relaxed);
  Rearm();
}

INT_IN(SetIntTime) {
  SetFloatTime((float)value, inlet, thread);
}

LIST_IN(Command) {
  (void)inlet;

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

  // `.metro`'s `clock`, and for the same reason: setclock's name is "passed as
  // the argument to a 'clock' message to numerous objects that use timing in
  // Max". The whole remainder is the name, so a clock named with spaces works.
  if (length == 5 && value.compare(begin, length, "clock", 5) == 0) {
    std::size_t nameBegin = end;
    while (nameBegin < value.size() && IsSelectorSeparator(value[nameBegin]))
      nameBegin++;
    std::size_t nameEnd = value.size();
    while (nameEnd > nameBegin && IsSelectorSeparator(value[nameEnd - 1]))
      nameEnd--;
    SetClock(value.c_str() + nameBegin, nameEnd - nameBegin);
    return;
  }

  // Max's `active` attribute. `active 1` arms the object afresh from wherever
  // the clock now stands, which is what makes it the way to re-use a timepoint
  // that has already fired.
  if (length == 6 && value.compare(begin, length, "active", 6) == 0) {
    float on = 0.f;
    // A bare `active` is not a setting. Doing nothing beats picking a value.
    if (!ReadFloatArg(value, end, on)) return;
    active.store(on != 0.f ? 1 : 0, std::memory_order_relaxed);
    Rearm();
    return;
  }

  // A leading number means what a bare number means. Everything else — Max's
  // `quantize`, a bars.beats.units figure, an unknown word — does nothing,
  // deliberately, rather than being read as some number it is not.
  float number = 0.f;
  if (!ReadNumericToken(value.c_str() + begin, length, number)) return;
  SetFloatTime(number, inlet, thread);
}
