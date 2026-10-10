
#include "gTransport.h"
#include "../../clock/clockManager.h"
#include "../pListArgs.h"
#include "../pObjectList.hpp"
#include "../pSelector.h"
#include "../patcherImplementation.h"
#include <cstddef>

using namespace YSE::PATCHER;

#define className gTransport

namespace {

  constexpr char kHotInletDoc[] =
      "Starts and stops the clock, and reports where it stands. A non-zero int or float starts it, "
      "0 stops it, and the messages 'start' and 'stop' are the same two under Max's own names. "
      "'tempo <bpm>' sets the tempo the transport runs at, and an optional second number makes it "
      "a glide in seconds ('tempo 140 2' reaches 140 over two seconds) — YSE's domain clocks ramp, "
      "which is the one thing here Max's transport cannot do. A tempo sent while the transport is "
      "stopped is remembered for the next start rather than obeyed, so setting a tempo never "
      "starts anything. A bang reports the clock's current beat position out the left outlet and "
      "its current tempo out the right one, right outlet first; a bang with no clock to read emits "
      "nothing at all rather than a zero that would look like a real position. There is no 'seek' "
      "and no bars.beats.units: a YSE domain clock is a bare beat accumulator, so 'stop' is a "
      "pause that holds the beat where it stands and 'start' carries on from there.";

  constexpr char kColdInletDoc[] =
      "Sets the tempo in BPM, which is the same value 'tempo <bpm>' sets in the left inlet — the "
      "inlet a slider goes into. It takes effect at once while the transport is running and is "
      "remembered for the next start while it is stopped. The glide set by 'tempo <bpm> <ramp>' "
      "applies here too, so a transport given a ramp keeps gliding to every tempo it is handed. "
      "Tempo is not clamped, because a domain clock's is not: 0 pauses the clock and a negative "
      "tempo runs it backwards.";

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
  REG_BANG_IN(Report);
  REG_LIST_IN(Command);

  ADD_IN_1;
  REG_INT_IN(SetIntTempo);
  REG_FLOAT_IN(SetFloatTempo);

  ADD_OUT_FLOAT; // beat position
  ADD_OUT_FLOAT; // tempo

  ADD_PARAM(clockname);
  ADD_PARAM(tempo);
  ADD_PARAM(ramp);

  tempo = 120.f;
  ramp = 0.f;

  ADD_DESCRIPTION(
      "Controls one of YSE's named domain clocks from inside the patcher — Max's transport, and "
      "the object that connects a patch to the engine's polytemporal clock system (issue #513). "
      "The creation argument names the clock: '.transport main 120' controls the clock called "
      "'main' and runs it at 120 BPM when started. That is the same clock "
      "yse_system_create_clock / setTempo / beatPosition address from the host, the same one a "
      "clip transport plays on, and the same one a '.metro clock main' counts its beats on — so a "
      "patch and its host are two hands on one lever, and every object bound to that name follows "
      "both. There is no tempo implementation here: start and stop are tempo writes onto the "
      "engine's clock (a domain clock has no run flag — tempo 0 is what 'paused' means), and the "
      "reported position is the clock's own beat count. Ownership is deliberate and narrow: the "
      "object creates the named clock if nobody has yet, on the control thread when it is added to "
      "a patcher, and creates it *stopped* so that dropping a transport into a patch never starts "
      "anything; a clock the host already made is left exactly as it is, first registration "
      "winning. It never destroys a clock — not on delete, not on a rename — because a remote "
      "control that unplugged the studio clock would take every other object bound to that name "
      "with it; teardown belongs to the host's destroyClock or System::close. A transport with no "
      "creation argument controls nothing at all and says so by doing nothing — a bang with no "
      "clock to read emits nothing rather than a zero that would look like a real position — and "
      "one that is not in a patcher creates and binds nothing. "
      "Calculate() does nothing, and no message handler allocates, locks or blocks "
      "when it turns out to be running on the audio callback: the clock is written through the "
      "patcher's clock bridge there, which is three atomic stores, and by name off it.");
  ADD_CATEGORY(pCategory::TIME);
  INLET_DOC(0, "control", kHotInletDoc, "0 or 1, bang, 'start', 'stop', 'tempo <bpm> [<ramp>]'");
  INLET_DOC(1, "tempo", kColdInletDoc, "BPM");
  OUTLET_DOC(0, "beat",
             "The clock's current beat position, on bang. Beats are the running "
             "integral of tempo, so this only ever moves forward while the tempo is "
             "positive and holds while the clock is stopped. Nothing is emitted when "
             "there is no clock to read.",
             "");
  OUTLET_DOC(1, "tempo",
             "The clock's current tempo in BPM, on bang, sent before the beat "
             "position so the pair arrives right to left as Max orders outlets. This "
             "is the clock's tempo and not the transport's wanted one, so it shows a "
             "ramp in progress and it shows a tempo the host changed behind the "
             "transport's back.",
             "BPM");
  PARAM_DOC("clockname", "",
            "The domain clock this transport controls. Clocks are addressed by name across the "
            "whole engine, so this is the same string the host passes to createClock and the same "
            "one a '.metro clock <name>' binds. Empty (the default) means the object controls "
            "nothing: it creates no clock, binds nothing, and every command is a no-op. The name "
            "is read once when the object is added to a patcher; re-pointing a transport at "
            "another clock is a re-create, which is what keeps clock creation on the control "
            "thread.",
            "");
  PARAM_DOC("tempo", "120",
            "The tempo in BPM the transport runs its clock at. Written to the clock on every "
            "start and on every change while running; remembered rather than applied while "
            "stopped. Not clamped, a domain clock's tempo not being clamped either — 0 pauses and "
            "a negative value runs the clock backwards.",
            "BPM");
  PARAM_DOC("ramp", "0",
            "Seconds every tempo write glides over, 0 being instant. This is domainClock's "
            "rampable tempo, which is what lets a patch accelerando rather than jump; it applies "
            "to a start and to any tempo change while running. A stop is always instant whatever "
            "this says — a stop that glided to a halt would be a fade-out nobody asked for.",
            "0+ seconds");
}

// ─── the ownership contract: create once, on the control thread ─────────────

void gTransport::SetParent(pObject* parent) {
  pObject::SetParent(parent);

  // A standalone object has no patcher, so no bridge and — by the contract in
  // the header — no clock either. Nothing is created for one.
  if (parent == nullptr) return;
  if (clockname.empty()) return;

  // The one call in this object that takes CLOCK::Manager's mutex, made on the
  // one hook that is guaranteed to be the control thread. `createClock` is
  // first-registration-wins, so this is a no-op for a clock the host already
  // made; a name nobody has claimed gets a clock at tempo 0, which is how the
  // engine spells "stopped". Creating a transport therefore never starts
  // anything — `start` does.
  CLOCK::Manager().createClock(clockname, 0.f);

  // Bind for the audio-callback read/write route. Idempotent by name, so this
  // shares a slot with every other object in the patcher naming the same clock
  // rather than costing one of its own. A refusal (a full bridge) leaves the
  // handle at 0: the by-name route still works, only the audio-callback one is
  // lost, and there is nothing useful to say about it from here.
  clockBridge* clocks = Clocks();
  if (clocks == nullptr) return;
  binding.store(clocks->Bind(clockname.c_str(), clockname.size()), std::memory_order_relaxed);
}

// ─── which route this handler may take ──────────────────────────────────────

bool gTransport::OnAudioThread(YSE::THREAD thread) const {
  // The `THREAD` tag is dispatch semantics, not thread identity: in-patcher
  // delivery dispatches T_DSP, and the drains at the top of Calculate dispatch
  // T_GUI *from the audio callback*. Only the patcher knows, and only since
  // #690 — so ask it, exactly as `.metro`, `.s` and `.bag` do. A standalone
  // object has no patcher and is never rendered, so it answers false.
  if (parent == nullptr) return false;
  return static_cast<patcherImplementation*>(parent)->CallingThread(thread) == YSE::T_DSP;
}

void gTransport::Push(YSE::THREAD thread) {
  if (clockname.empty()) return;

  const bool on = running.load(std::memory_order_relaxed);
  // Stopping is instant whatever the ramp says: Max's stop is immediate, and a
  // stop that glided to a halt would be a ritardando nobody asked for.
  const float bpm = on ? tempo.load(std::memory_order_relaxed) : 0.f;
  const float seconds = on ? ramp.load(std::memory_order_relaxed) : 0.f;

  if (!OnAudioThread(thread)) {
    // By name, inline. Takes the manager's mutex, which this thread is allowed
    // to do, and needs no resolved binding — so a `start` sent in the same
    // breath as CreateObject lands immediately rather than waiting for the
    // background pool to have found the clock.
    CLOCK::Manager().setTempo(clockname, bpm, seconds);
    return;
  }

  // On the callback: the binding's wait-free write, three atomic stores the
  // clock consumes on its next block. An unresolved binding drops the command
  // rather than blocking; the clock was created before the bind, so resolution
  // is a background hop away and every later command lands.
  clockBridge* clocks = Clocks();
  if (clocks == nullptr) return;
  clocks->RequestTempo(binding.load(std::memory_order_relaxed), bpm, seconds);
}

bool gTransport::ReadClock(YSE::THREAD thread, double& beat, float& bpm) const {
  if (clockname.empty()) return false;

  if (!OnAudioThread(thread)) {
    // `beatPosition` and `currentTempo` both answer 0 for a name they do not
    // have, and 0 is a perfectly ordinary position and tempo, so the existence
    // question has to be asked separately.
    if (!CLOCK::Manager().clockExists(clockname)) return false;
    beat = CLOCK::Manager().beatPosition(clockname);
    bpm = CLOCK::Manager().currentTempo(clockname);
    return true;
  }

  const clockBridge* clocks = Clocks();
  if (clocks == nullptr) return false;
  const clockBridge::Handle bound = binding.load(std::memory_order_relaxed);
  // The two routes agree on every clock that exists. They part on one that the
  // host has *destroyed*: the manager no longer has it, so the route above goes
  // quiet, while a resolved binding keeps the clock alive and frozen and
  // reports the values it stopped at. That is #707's rule, inherited rather
  // than papered over — a destroyed clock is a stopped clock to everything
  // bound to it.
  return clocks->Beat(bound, beat) && clocks->Tempo(bound, bpm);
}

// ─── Max's methods ──────────────────────────────────────────────────────────

void gTransport::Run(bool on, YSE::THREAD thread) {
  running.store(on, std::memory_order_relaxed);
  Push(thread);
}

INT_IN(ToggleInt) {
  (void)inlet;
  Run(value != 0, thread);
}

FLOAT_IN(ToggleFloat) {
  (void)inlet;
  // Compared against zero rather than cast, `.metro`'s rule: "any number other
  // than 0 starts" makes 0.5 a start where `(int)0.5` would be a stop.
  Run(value != 0.f, thread);
}

BANG_IN(Report) {
  (void)inlet;
  double beat = 0.0;
  float bpm = 0.f;
  // Nothing at all when there is no clock: a zero here would be indistinguishable
  // from a clock genuinely sitting at beat 0, and whatever this is wired into
  // would act on a position that does not exist.
  if (!ReadClock(thread, beat, bpm)) return;
  // Right to left, Max's outlet order.
  outputs[1].SendFloat(bpm, thread);
  outputs[0].SendFloat((float)beat, thread);
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

  if (length == 5 && value.compare(begin, length, "start", 5) == 0) {
    Run(true, thread);
    return;
  }
  if (length == 4 && value.compare(begin, length, "stop", 4) == 0) {
    Run(false, thread);
    return;
  }
  if (length == 5 && value.compare(begin, length, "tempo", 5) == 0) {
    float args[2] = {0.f, 0.f};
    const int count = ReadFloatArgs(value, end, args, 2);
    // A bare `tempo` is not a tempo. Doing nothing beats picking a number.
    if (count == 0) return;
    tempo.store(args[0], std::memory_order_relaxed);
    if (count > 1) ramp.store(args[1] < 0.f ? 0.f : args[1], std::memory_order_relaxed);
    // Remembered rather than obeyed while stopped: setting a tempo must not
    // start a transport nobody started.
    if (running.load(std::memory_order_relaxed)) Push(thread);
    return;
  }

  // Anything else — Max's `seek`, a bars.beats.units figure, an unknown word —
  // does nothing, deliberately, rather than being read as some command it is
  // not. See the header on why `seek` is absent rather than unimplemented.
}

FLOAT_IN(SetFloatTempo) {
  (void)inlet;
  tempo.store(value, std::memory_order_relaxed);
  if (running.load(std::memory_order_relaxed)) Push(thread);
}

INT_IN(SetIntTempo) {
  SetFloatTempo((float)value, inlet, thread);
}
