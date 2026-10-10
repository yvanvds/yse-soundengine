
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
      "A non-zero number or 'start' starts the clock; 0 or 'stop' stops it. 'tempo <bpm> "
      "[<ramp>]' sets the tempo. A bang sends the tempo and the beat position.";

  constexpr char kColdInletDoc[] =
      "The tempo in BPM. Applied at once while running, kept for the next start while stopped.";

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
      "Starts, stops and sets the tempo of a named domain clock, creating the clock (stopped) if "
      "it does not exist yet. A bang sends the clock's beat position and tempo.");
  ADD_CATEGORY(pCategory::TIME);
  INLET_DOC(0, "control", kHotInletDoc, "0 or 1, bang, 'start', 'stop', 'tempo <bpm> [<ramp>]'");
  INLET_DOC(1, "tempo", kColdInletDoc, "BPM");
  OUTLET_DOC(0, "beat", "The clock's beat position, on bang.", "");
  OUTLET_DOC(1, "tempo",
             "The clock's tempo in BPM, on bang, sent just before the beat position. It shows a "
             "glide in progress and changes made by the host.",
             "BPM");
  PARAM_DOC("clockname", "",
            "The domain clock to control, created when the object is added to a patcher. Empty "
            "controls nothing.",
            "");
  PARAM_DOC("tempo", "120",
            "The tempo in BPM the clock runs at while started. Not clamped: a negative tempo runs "
            "the clock backwards.",
            "BPM");
  PARAM_DOC("ramp", "0",
            "Seconds each start or tempo change glides over. 0 is instant. A stop is always "
            "instant.",
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
