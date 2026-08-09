
#include "gTempo.h"
#include "../../clock/clockManager.h"
#include "../pListArgs.h"
#include "../pObjectList.hpp"
#include "../pSelector.h"
#include "../patcherImplementation.h"
#include <cmath>
#include <cstddef>
#include <cstdint>

using namespace YSE::PATCHER;

#define className gTempo

namespace {

  constexpr char kHotInletDoc[] =
      "Starts, restarts and stops the metronome, and carries its three messages. Max: 'bang starts "
      "or restarts the metronome', and a float is 'non-zero acts as bang; 0 acts as stop' — "
      "compared against zero rather than cast, so 0.5 starts it where a cast to int would stop it. "
      "An int does the same, and the message 'stop' is 0 under Max's own name. Starting a "
      "metronome that is already running re-phases it: the grid is taken afresh from the beat the "
      "message landed on and the count restarts at 0, which is how one button puts several of them "
      "in step. A start also writes the object's tempo onto its clock; a stop never does, because "
      "a metronome switching off must not stop every clip on the domain with it. 'tempo <bpm>' "
      "sets the tempo, and an optional second number makes it a glide in seconds ('tempo 140 2' "
      "reaches 140 over two seconds) — YSE's domain clocks ramp, which is the one thing here Max's "
      "tempo cannot do. 'clock <name>' names the YSE domain clock the beats are counted on, the "
      "same message '.metro' and '.timepoint' take, and a bare 'clock' unbinds and stops the "
      "metronome counting: there is no millisecond fallback here, a tempo and a note value having "
      "no meaning without a beat. Everything else does nothing, deliberately, rather than being "
      "read as some command it is not.";

  constexpr char kTempoInletDoc[] =
      "Sets the tempo in BPM — Max's 'sets tempo in quarter notes per minute' — which is the same "
      "value 'tempo <bpm>' sets in the left inlet, and the inlet a slider goes into. It is written "
      "onto the bound domain clock at once, so it retempos everything counting on that clock and "
      "not only this object: the tempo is the clock's, which is what binding to a domain clock "
      "rather than reimplementing tempo means. Not clamped, because a domain clock's tempo is not "
      "— Max's 5-300 range is gone; 0 pauses the clock and a negative tempo runs it backwards. "
      "With no clock bound nothing is written at all, a tempo at a clock that does not exist not "
      "being a tempo.";

  constexpr char kMultiplierInletDoc[] =
      "Sets Max's beat multiplier, which slows the output proportionally: the interval between "
      "outputs is 4 x multiplier / division beats, so a multiplier of 2 halves the rate without "
      "touching the length of the count's cycle. Floored at 1. Changing it under a running "
      "metronome rebases the grid at the current beat, keeping the phase rather than "
      "re-triggering, and the count carries on from where it stood.";

  constexpr char kDivisionInletDoc[] =
      "Sets Max's rhythmic value — the division of a whole note the metronome ticks at, and the "
      "length of the count's cycle. 4 is a quarter note (one beat), 16 the default sixteenth, 8 an "
      "eighth. Clamped to Max's documented 1-96: unlike the tempo, a division is a shape of the "
      "grid rather than a tempo, so the range stays. Changing it under a running metronome rebases "
      "the grid at the current beat, keeping the phase, and the count carries on, wrapped into the "
      "new cycle if that shrank under it.";

  constexpr char kOutletDoc[] =
      "The tick number, cycling continuously from 0 to division - 1 — Max's 'cycles continuously "
      "from 0 to (rhythmic value - 1)'. 0 is sent the moment the metronome is started, as Max's "
      "metronomes emit on start, and one number follows every 4 x multiplier / division beats of "
      "the bound clock after that. The count is a position in the cycle rather than a tally of "
      "outputs: a domain that jumps many units in one block advances it over every unit it "
      "skipped and emits only the last few, so the number that arrives is always the right step "
      "even when the ones before it were dropped. Nothing is emitted while no clock is bound, "
      "while the clock does not exist yet, or while it is paused.";

  // Read up to `max` whitespace-separated numbers out of `text` starting at
  // `from`, stopping at the first token that is not one whole finite number.
  // Bounded and allocation-free: this runs on whichever thread sent the message.
  int ReadFloatArgs(const std::string& text, std::size_t from, float* out, int max) {
    int count = 0;
    std::size_t i = from;
    while (count < max) {
      while (i < text.size() && IsSelectorSeparator(text[i]))
        i++;
      std::size_t end = i;
      while (end < text.size() && !IsSelectorSeparator(text[end]))
        end++;
      if (end <= i) break;
      if (!ReadNumericToken(text.c_str() + i, end - i, out[count])) break;
      count++;
      i = end;
    }
    return count;
  }

} // namespace

CONSTRUCT() {
  ADD_IN_0;
  REG_INT_IN(ToggleInt);
  REG_FLOAT_IN(ToggleFloat);
  REG_BANG_IN(BangIn);
  REG_LIST_IN(Command);

  ADD_IN_1;
  REG_INT_IN(SetIntTempo);
  REG_FLOAT_IN(SetFloatTempo);

  ADD_IN_2;
  REG_INT_IN(SetIntMultiplier);
  REG_FLOAT_IN(SetFloatMultiplier);

  ADD_IN_3;
  REG_INT_IN(SetIntDivision);
  REG_FLOAT_IN(SetFloatDivision);

  ADD_OUT_INT; // the tick number

  ADD_PARAM(clockname);
  ADD_PARAM(tempo);
  ADD_PARAM(multiplier);
  ADD_PARAM(division);

  tempo = 120.f;
  multiplier = 1;
  division = 16;

  ADD_DESCRIPTION(
      "Counting metronome on a named YSE domain clock — Max's tempo, and the patcher's rhythmic "
      "backbone (issue #512). '.tempo main 120 1 16' counts sixteenth notes at 120 BPM on the "
      "clock called 'main', sending 0, 1, 2 ... 15, 0, 1 ... out its outlet. Where '.metro' bangs "
      "at an interval, this emits a number, and the number is where in the cycle the tick is — "
      "which is what a '.sel', a '.route' or a '.coll' downstream needs to make a pattern rather "
      "than a pulse. The interval is Max's 4 x multiplier / division beats, division being a "
      "fraction of a whole note, and the count cycles from 0 to division - 1. There is no "
      "millisecond engine in here at all: a tempo in BPM and a note value are meaningless without "
      "a beat, and the engine's named domain clocks are the only thing in the process that has "
      "one. So the timing is read off the clock's beat position — inheriting its tempo changes, "
      "its ramps and its pauses for free, and keeping two '.tempo' objects on one clock in exact "
      "relation to each other and to every clip on that domain — and the tempo is written onto "
      "that clock rather than implemented here, which makes it rampable and, unlike Max's 5-300, "
      "never clamped. Starting the metronome asserts its tempo on the clock and begins the count "
      "at 0; stopping retires the grid and never writes the clock, because a metronome switching "
      "off must not stop every clip on the domain with it. It never creates a clock and never "
      "destroys one: '.transport' is the sole creator (issue #513), so a name nothing has claimed "
      "simply never ticks, and the run begins the moment the clock starts existing. The tick count "
      "is read off the clock's beat position rather than counted from the wakeups that deliver it "
      "— a beat deadline is armed relative to the beat it was armed at but delivered at the first "
      "audio block boundary past it, so counting deliveries would drop that overshoot every tick "
      "and run the metronome slow. bars.beats.units, quantize and Max's transport attribute stay "
      "out, all three needing a meter a bare beat accumulator does not have. Calculate() does "
      "nothing, and no message or delivery path allocates, locks or blocks when it turns out to be "
      "running on the audio callback: binding and arming are wait-free, reading a beat is two "
      "acquire loads, and the tempo is written through the patcher's clock bridge there — three "
      "atomic stores — and by name off it.");
  ADD_CATEGORY(pCategory::TIME);
  INLET_DOC(0, "on/off", kHotInletDoc,
            "0 or 1, bang, 'stop', 'tempo <bpm> [<ramp>]', 'clock <name>'");
  INLET_DOC(1, "tempo", kTempoInletDoc, "BPM");
  INLET_DOC(2, "multiplier", kMultiplierInletDoc, "1+");
  INLET_DOC(3, "division", kDivisionInletDoc, "1-96");
  OUTLET_DOC(0, "count", kOutletDoc, "0 to division - 1");
  PARAM_DOC("clockname", "",
            "The domain clock the metronome counts on. Clocks are addressed by name across the "
            "whole engine, so this is the same string the host passes to createClock and the same "
            "one a '.transport <name>' drives. Empty (the default) means the object counts "
            "nothing and emits nothing. The name is bound when the object is added to a patcher — "
            "bound, not created: a clock nobody has made yet leaves the metronome waiting, and it "
            "starts when the clock appears. A 'clock <name>' message re-points the object without "
            "rewriting this, so the patch file keeps the name its author typed.",
            "");
  PARAM_DOC("tempo", "120",
            "The tempo in BPM the metronome asserts on its clock. Written when the metronome is "
            "started and whenever it is set through the left inlet's 'tempo' message or the tempo "
            "inlet; never written at creation, and never on a stop. Not clamped, a domain clock's "
            "tempo not being clamped either — Max's 5-300 range is gone, 0 pauses the clock and a "
            "negative value runs it backwards. On a clock something else also drives, the last "
            "write wins: a '.tempo' meant only to subdivide a pulse a '.transport' sets should be "
            "left alone rather than started with a tempo of its own.",
            "BPM");
  PARAM_DOC("multiplier", "1",
            "Max's beat multiplier. The interval between outputs is 4 x multiplier / division "
            "beats, so a higher multiplier slows the output proportionally without changing the "
            "length of the count's cycle. Floored at 1.",
            "1+");
  PARAM_DOC("division", "16",
            "Max's rhythmic value: the division of a whole note the metronome ticks at, and the "
            "length of the count's cycle. 4 is a quarter note — one beat on a domain clock — 8 an "
            "eighth and 16 the default sixteenth. Clamped to Max's documented 1-96, a division "
            "being a shape of the grid rather than a tempo.",
            "1-96");
}

// ─── the clock: bound, never created ────────────────────────────────────────

void gTempo::SetParent(pObject* parent) {
  pObject::SetParent(parent);

  // A standalone object has no patcher, so no bridge and no scheduler: nothing
  // to bind and nothing to arm.
  if (parent == nullptr) return;
  if (clockname.empty()) return;

  clockBridge* clocks = Clocks();
  if (clocks == nullptr) return;

  // Bind only. `.transport` creates clocks and this object does not — see the
  // header — so there is no CLOCK::Manager() call here. Binding is idempotent by
  // name, so this shares a slot with the `.transport` driving the clock rather
  // than costing one of its own.
  binding.store(clocks->Bind(clockname.c_str(), clockname.size()), std::memory_order_relaxed);
}

const char* gTempo::ClockName() const {
  const clockBridge::Handle bound = binding.load(std::memory_order_relaxed);
  if (bound == 0) return "";
  const clockBridge* clocks = Clocks();
  if (clocks == nullptr) return "";
  return clocks->NameOf(bound);
}

void gTempo::SetClock(const char* name, std::size_t length) {
  // A bare `clock` gives the clock back, which for this object means it stops
  // counting: there is no millisecond fallback, a tempo and a note value having
  // no meaning without a beat. The run is left *running* so that a later
  // `clock <name>` resumes it where a fresh start would restart the cycle.
  if (name == nullptr || length == 0) {
    binding.store(0, std::memory_order_relaxed);
    storeGuard guard(busy);
    if (guard.Held()) CancelWakeup();
    return;
  }

  clockBridge* clocks = Clocks();
  if (clocks == nullptr) return;

  // Wait-free: a bounded walk over the patcher's binding table and a memcpy of
  // the name into a slot that already exists. The name is *not* looked up here —
  // that takes the clock manager's mutex and happens on the background pool.
  const clockBridge::Handle bound = clocks->Bind(name, length);
  // A refusal (the table is full, or the name is longer than a slot holds)
  // leaves the object on whatever clock it was on rather than silently counting
  // nothing.
  if (bound == 0) return;
  binding.store(bound, std::memory_order_relaxed);
  Rebase();
}

// ─── the grid ───────────────────────────────────────────────────────────────

int gTempo::Multiplier() const {
  const Int value = multiplier.load();
  return value > 1 ? (int)value : 1;
}

int gTempo::Division() const {
  const Int value = division.load();
  if (value < 1) return 1;
  return value > MAX_DIVISION ? MAX_DIVISION : (int)value;
}

double gTempo::UnitBeats() const {
  // Max's unit: a division of a whole note, and a whole note is four beats on a
  // domain clock. Both factors are clamped, so this is always positive and
  // never NaN however a live SetParams re-parse mangled the fields.
  return 4.0 * (double)Multiplier() / (double)Division();
}

void gTempo::CancelWakeup() {
  if (pending == 0) return;
  messageScheduler* scheduler = Scheduler();
  if (scheduler != nullptr) scheduler->Cancel(pending);
  pending = 0;
}

void gTempo::ArmNext(clockBridge::Handle bound) {
  messageScheduler* scheduler = Scheduler();
  if (scheduler == nullptr) return;

  CancelWakeup();

  double ahead = gridBeats;
  const clockBridge* clocks = Clocks();
  double beat = 0.0;
  if (beatBased && clocks != nullptr && clocks->Beat(bound, beat)) {
    // The *absolute* next grid point, not one unit from here. A delivery lands
    // at the first audio block boundary at or after its deadline, so asking for
    // a fixed unit every time would hand back that overshoot on every tick and
    // run the metronome slow. (The delivery still takes its tick count off the
    // clock rather than from this arm — see the header. Both halves are needed.)
    ahead = (beatBase + (double)(emitted + 1) * gridBeats) - beat;
    // Already past it — a wakeup that covered more than one unit. 0 beats is due
    // at the next drain, which is the next block, so the catch-up continues one
    // block at a time instead of stalling.
    if (!(ahead > 0.0)) ahead = 0.0;
  }
  // An unresolved binding leaves `ahead` at one plain unit, which the scheduler
  // baselines at the beat the binding resolves on: the run starts when the clock
  // starts existing.
  pending = scheduler->ScheduleBangOnClock(this, 0, bound, ahead);
}

void gTempo::Rebase() {
  if (!running.load(std::memory_order_relaxed)) return;
  const clockBridge::Handle bound = binding.load(std::memory_order_relaxed);
  if (bound == 0) return;

  storeGuard guard(busy);
  // Another thread is inside the grid right now; it arms whatever it decides on
  // off the same live state this call already wrote, so doing nothing here is
  // the answer rather than waiting — which a message handler may never do.
  if (!guard.Held()) return;

  gridBeats = UnitBeats();
  emitted = 0;
  beatBased = false;
  const clockBridge* clocks = Clocks();
  double beat = 0.0;
  if (clocks != nullptr && clocks->Beat(bound, beat)) {
    beatBase = beat;
    beatBased = true;
  }
  ArmNext(bound);
}

void gTempo::Retime() {
  if (!running.load(std::memory_order_relaxed)) return;
  const clockBridge::Handle bound = binding.load(std::memory_order_relaxed);
  if (bound == 0) return;

  const double unit = UnitBeats();

  storeGuard guard(busy);
  if (!guard.Held()) return;

  // A division change moves the cycle even when it leaves the rate alone
  // (multiplier 2 / division 32 ticks exactly as fast as 1 / 16), so the count
  // is wrapped whichever of the two moved.
  const std::int64_t wrap = Division();
  const std::int64_t at = count.load(std::memory_order_relaxed);
  if (at >= wrap) count.store(at % wrap, std::memory_order_relaxed);

  if (unit == gridBeats) return;

  const clockBridge* clocks = Clocks();
  double beat = 0.0;
  if (clocks != nullptr && clocks->Beat(bound, beat)) {
    // Rebase rather than rescale: #625's rule for `.metro`'s millisecond path is
    // that retiming keeps the phase and does not re-trigger, and measuring the
    // new grid from *here* is what that means on a beat clock. The count is left
    // where it stands — a change of rate is not a change of position in the bar.
    beatBase = beat;
    beatBased = true;
    emitted = 0;
  }
  gridBeats = unit;
  ArmNext(bound);
}

// ─── the tempo write, and which route this handler may take ─────────────────

bool gTempo::OnAudioThread(YSE::THREAD thread) const {
  // The `THREAD` tag is dispatch semantics, not thread identity: in-patcher
  // delivery dispatches T_DSP, and the drains at the top of Calculate dispatch
  // T_GUI *from the audio callback*. Only the patcher knows, and only since
  // #690 — so ask it, exactly as `.transport`, `.metro` and `.bag` do. A
  // standalone object has no patcher and is never rendered, so it answers false.
  if (parent == nullptr) return false;
  return static_cast<patcherImplementation*>(parent)->CallingThread(thread) == YSE::T_DSP;
}

void gTempo::PushTempo(float bpm, float rampSeconds, YSE::THREAD thread) {
  const clockBridge::Handle bound = binding.load(std::memory_order_relaxed);
  // Nothing bound is nothing to retempo. Unlike `.transport`, this object has no
  // by-name fallback for an unbound name: it never creates a clock, so the name
  // it would write to is exactly the name the bridge holds.
  if (bound == 0) return;
  clockBridge* clocks = Clocks();
  if (clocks == nullptr) return;

  if (OnAudioThread(thread)) {
    // The binding's wait-free write: three atomic stores the clock consumes on
    // its next block. An unresolved binding drops the write rather than
    // blocking, and the bridge's own Poll has it resolved within
    // clockBridge::RESOLVE_INTERVAL_BLOCKS blocks.
    clocks->RequestTempo(bound, bpm, rampSeconds);
    return;
  }

  // Off the callback the manager is called by name, inline. It takes the
  // manager's mutex, which this thread is allowed to do, and it needs no
  // resolved binding — so a start sent in the same breath as CreateObject
  // retempos the clock immediately rather than waiting for the background pool
  // to have found it.
  const char* name = clocks->NameOf(bound);
  if (name == nullptr || name[0] == '\0') return;
  // Almost always the creation argument, which is already the std::string the
  // manager wants and therefore costs nothing to pass. Only a `clock <name>`
  // re-point needs one built, and this branch is never the audio callback, so
  // that allocation is this thread's to make.
  if (clockname == name) {
    CLOCK::Manager().setTempo(clockname, bpm, rampSeconds);
    return;
  }
  CLOCK::Manager().setTempo(std::string(name), bpm, rampSeconds);
}

// ─── Max's methods ──────────────────────────────────────────────────────────

void gTempo::Stop() {
  // No tempo write. A `.transport`'s stop is a tempo 0 because stopping the
  // transport is what it is for; a metronome switching off must not stop every
  // clip on the domain with it.
  running.store(false, std::memory_order_relaxed);
  storeGuard guard(busy);
  // A lost guard leaves the wakeup armed; it finds `running` false and stops
  // there, which is why that flag lives outside the guard.
  if (!guard.Held()) return;
  CancelWakeup();
}

void gTempo::Start(YSE::THREAD thread) {
  // Stop first on either edge: a restart while running must not leak the
  // previous wakeup, and Max's bang is a re-start rather than a no-op.
  Stop();

  const clockBridge::Handle bound = binding.load(std::memory_order_relaxed);
  // Nothing to count on. A later `clock <name>`, or the clock appearing under a
  // name already bound, makes the next start work.
  if (bound == 0) return;

  // Max's metronome runs at its own tempo, and this is the one place it says so
  // — never at creation, so dropping a `.tempo` into a patch retempos nothing.
  PushTempo(tempo.load(std::memory_order_relaxed), 0.f, thread);

  running.store(true, std::memory_order_relaxed);
  {
    storeGuard guard(busy);
    // Losing the guard means another thread is inside the grid right now. This
    // run does not start rather than starting on a half-written one.
    if (!guard.Held()) {
      running.store(false, std::memory_order_relaxed);
      return;
    }

    gridBeats = UnitBeats();
    emitted = 0;
    beatBased = false;
    count.store(0, std::memory_order_relaxed);
    const clockBridge* clocks = Clocks();
    double beat = 0.0;
    // Tick 0 stands on the beat this message landed on, so the run begins where
    // the message did. An unresolved binding has no beat to take; the first
    // wakeup that finds a clock takes it instead.
    if (clocks != nullptr && clocks->Beat(bound, beat)) {
      beatBase = beat;
      beatBased = true;
    }
    ArmNext(bound);
  }

  // Max's metronomes emit the moment they are started, and for this one that is
  // the 0 the cycle begins on. Sent outside the guard: a patch that wires this
  // outlet back into this object must not find the grid held.
  outputs[0].SendInt(0, thread);
}

INT_IN(ToggleInt) {
  (void)inlet;
  if (value == 0) {
    Stop();
    return;
  }
  Start(thread);
}

FLOAT_IN(ToggleFloat) {
  (void)inlet;
  // Max: "non-zero acts as bang; 0 acts as stop". Compared against zero rather
  // than cast, `.metro`'s rule: 0.5 is a number other than 0 and therefore
  // starts, where `(int)0.5` would stop.
  ToggleInt(value == 0.f ? 0 : 1, inlet, thread);
}

BANG_IN(BangIn) {
  // Max: "bang starts or restarts the metronome". Registered on inlet 0 only,
  // which is where Max documents it.
  ToggleInt(1, inlet, thread);
}

FLOAT_IN(SetFloatTempo) {
  (void)inlet;
  tempo.store(value, std::memory_order_relaxed);
  // Written through at once, running or not: the tempo is the clock's, and a
  // slider into this inlet has to move the domain the moment it moves.
  PushTempo(value, 0.f, thread);
}

INT_IN(SetIntTempo) {
  SetFloatTempo((float)value, inlet, thread);
}

INT_IN(SetIntMultiplier) {
  (void)inlet;
  (void)thread;
  multiplier = value;
  Retime();
}

FLOAT_IN(SetFloatMultiplier) {
  SetIntMultiplier((int)value, inlet, thread);
}

INT_IN(SetIntDivision) {
  (void)inlet;
  (void)thread;
  division = value;
  Retime();
}

FLOAT_IN(SetFloatDivision) {
  SetIntDivision((int)value, inlet, thread);
}

LIST_IN(Command) {
  // Registered on the left inlet only, where Max puts every one of these.
  if (inlet != 0) return;

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

  // Max's `stop`: "halts metronome", which is a 0 under another name, so it goes
  // through the one path rather than growing a second way to stop.
  if (length == 4 && value.compare(begin, length, "stop", 4) == 0) {
    ToggleInt(0, inlet, thread);
    return;
  }

  // Max's `clock <name>`: "sync to named setclock object (or omit name for
  // internal clock)". The whole remainder is the name, so a clock named with
  // spaces works — `.metro`'s and `.timepoint`'s reading of the same message.
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

  // Max's `tempo <bpm>`, with `.transport`'s optional ramp behind it.
  if (length == 5 && value.compare(begin, length, "tempo", 5) == 0) {
    float args[2] = {0.f, 0.f};
    const int count = ReadFloatArgs(value, end, args, 2);
    // A bare `tempo` is not a tempo. Doing nothing beats picking a number.
    if (count == 0) return;
    tempo.store(args[0], std::memory_order_relaxed);
    const float ramp = (count > 1 && args[1] > 0.f) ? args[1] : 0.f;
    PushTempo(args[0], ramp, thread);
    return;
  }

  // A leading number means what a bare number means. Everything else — Max's
  // `quantize`, a bars.beats.units figure, an unknown word — does nothing,
  // deliberately, rather than being read as some command it is not.
  float number = 0.f;
  if (!ReadNumericToken(value.c_str() + begin, length, number)) return;
  ToggleFloat(number, inlet, thread);
}

// ─── the tick ───────────────────────────────────────────────────────────────

void gTempo::DeliverDeferred(const deferredMessage& msg, YSE::THREAD thread) {
  (void)msg;
  std::int64_t steps = 0;
  std::int64_t first = 0;
  std::int64_t wrap = 1;
  {
    storeGuard guard(busy);
    // A lost guard drops this wakeup without re-arming, which is safe here for
    // `.metro`'s reason: the thread holding the grid is a start, a rebase or a
    // retime, and every one of them arms a wakeup of its own before it lets go.
    if (!guard.Held()) return;

    // Whatever armed this wakeup has fired; the handle it left behind is stale.
    pending = 0;
    if (!running.load(std::memory_order_relaxed)) return;
    const clockBridge::Handle bound = binding.load(std::memory_order_relaxed);
    if (bound == 0) return;

    const clockBridge* clocks = Clocks();
    double beat = 0.0;
    if (clocks == nullptr || !clocks->Beat(bound, beat)) {
      // The clock does not exist yet, so no time has passed on it. Re-arm and
      // let the bridge's own Poll resolve the name; this is what makes a clock
      // named before anything creates it start the run when it appears.
      ArmNext(bound);
      return;
    }

    // The run started before its clock resolved; this is the wakeup that found
    // it, and tick 0 stands here.
    if (!beatBased) {
      beatBase = beat;
      beatBased = true;
      emitted = 0;
    }

    // The SetParams route into the multiplier and the division, which stores
    // from the audio thread and notifies nobody. Rebasing rather than rescaling
    // keeps the phase, as it does in Retime.
    const double unit = UnitBeats();
    if (unit != gridBeats) {
      gridBeats = unit;
      beatBase = beat;
      emitted = 0;
    }
    // UnitBeats() clamps both factors, so the grid always has a step; this is
    // the belt to that brace, a grid being a grid only while it has one.
    if (!(gridBeats > 0.0)) {
      running.store(false, std::memory_order_relaxed);
      return;
    }

    wrap = Division();

    // The time base: how many units the *clock* has moved, never how many
    // wakeups have arrived. See the header — this is the whole reason the run
    // does not drift, and the reason it does not cap at one tick per block when
    // the unit is shorter than a block.
    double elapsed = (beat - beatBase) / gridBeats;
    // Also the NaN case, no comparison accepting one. A beat position that went
    // backwards emits nothing rather than winding the count back.
    if (!(elapsed > 0.0)) elapsed = 0.0;
    // Far past any grid index a running clock can reach, and small enough that
    // the int64 below cannot overflow.
    if (elapsed > 1.0e12) elapsed = 1.0e12;

    const std::int64_t target = (std::int64_t)std::floor(elapsed);
    std::int64_t advance = target - emitted;
    if (advance < 0) advance = 0;
    if (advance > 0) {
      // The count advances over *every* unit the clock covered, including the
      // ones the burst below will not emit: it is a position in the cycle, and a
      // position that skipped its way there is still the right position.
      std::int64_t at = count.load(std::memory_order_relaxed);
      if (at >= wrap) at %= wrap;
      at = (at + (advance % wrap)) % wrap;
      count.store(at, std::memory_order_relaxed);

      // A domain that jumped many units in one block emits only the last few
      // rather than emptying an unbounded burst onto the audio thread. The
      // index still advances to `target`, so nothing accumulates into the next
      // wakeup.
      steps = advance > MAX_CATCHUP ? MAX_CATCHUP : advance;
      first = (((at - (steps - 1)) % wrap) + wrap) % wrap;
      emitted = target;
    }

    // Re-armed before anything is emitted, so the next wakeup is on the grid
    // whatever this one turns out to send. A send that stops the metronome — a
    // toggle coming back through a cord — takes it out again on the way past.
    ArmNext(bound);
  }

  for (std::int64_t i = 0; i < steps; i++)
    outputs[0].SendInt((int)((first + i) % wrap), thread);
}
