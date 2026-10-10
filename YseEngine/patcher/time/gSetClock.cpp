
#include "gSetClock.h"
#include "../../clock/clockManager.h"
#include "../pListArgs.h"
#include "../pObjectList.hpp"
#include "../pSelector.h"
#include "../patcherImplementation.h"
#include <cstddef>

using namespace YSE::PATCHER;

#define className gSetClock

namespace {

  constexpr char kInletDoc[] =
      "A number sets the tempo in BPM; 0 stops the clock. 'tempo <bpm> [<ramp>]' does the same "
      "and can set a glide time. A bang sends the beat position.";

  // `.transport`'s reader, kept strict and kept identical so that the `tempo`
  // message means the same thing on both objects: whitespace-separated numbers
  // out of `text` from `from`, at most `max`, stopping at the first token that
  // is not one whole finite number. Bounded and allocation-free, because this
  // runs on whichever thread sent the message.
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
  REG_BANG_IN(Report);
  REG_INT_IN(SetIntTempo);
  REG_FLOAT_IN(SetFloatTempo);
  REG_LIST_IN(Command);

  ADD_OUT_FLOAT; // beat position

  ADD_PARAM(clockname);
  ADD_PARAM(tempo);
  ADD_PARAM(ramp);

  tempo = 120.f;
  ramp = 0.f;

  ADD_DESCRIPTION(
      "Creates a named domain clock that runs from the start, and sets its tempo: every number "
      "it receives is the new tempo in BPM. A bang sends the clock's beat position.");
  ADD_CATEGORY(pCategory::TIME);
  INLET_DOC(0, "speed", kInletDoc, "BPM, bang, 'tempo <bpm> [<ramp>]'");
  OUTLET_DOC(0, "beat", "The clock's beat position, on bang.", "");
  PARAM_DOC("clockname", "",
            "The domain clock to create, when the object is added to a patcher. Empty does "
            "nothing.",
            "");
  PARAM_DOC("tempo", "120",
            "The tempo in BPM, used when the clock is created and replaced by every number "
            "received. Not clamped: 0 stops the clock, a negative tempo runs it backwards.",
            "BPM");
  PARAM_DOC("ramp", "0",
            "Seconds each tempo change glides over. 0 is instant. The starting tempo is always "
            "instant.",
            "0+ seconds");
}

// ─── the ownership contract: create once, running, on the control thread ────

void gSetClock::SetParent(pObject* parent) {
  pObject::SetParent(parent);

  // A standalone object has no patcher, so no bridge and — by the contract in
  // the header — no clock either. Nothing is created for one.
  if (parent == nullptr) return;
  if (clockname.empty()) return;

  // The one call in this object that takes CLOCK::Manager's mutex, made on the
  // one hook that is guaranteed to be the control thread. Created *running* at
  // `tempo`, which is the whole difference between this object and a
  // `.transport`: a setclock is an alternative time source, so it is turning
  // the moment the patch loads rather than waiting for a start it has no
  // message for. `createClock` is first-registration-wins, so a name the host
  // or another object already claimed is left exactly as it is — not re-tempoed
  // to this object's argument — and `created` records which of the two
  // happened.
  created.store(CLOCK::Manager().createClock(clockname, tempo.load(std::memory_order_relaxed)),
                std::memory_order_relaxed);

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

bool gSetClock::OnAudioThread(YSE::THREAD thread) const {
  // The `THREAD` tag is dispatch semantics, not thread identity: in-patcher
  // delivery dispatches T_DSP, and the drains at the top of Calculate dispatch
  // T_GUI *from the audio callback*. Only the patcher knows, and only since
  // #690 — so ask it, exactly as `.transport`, `.when` and `.metro` do. A
  // standalone object has no patcher and is never rendered, so it answers false.
  if (parent == nullptr) return false;
  return static_cast<patcherImplementation*>(parent)->CallingThread(thread) == YSE::T_DSP;
}

void gSetClock::Push(float bpm, float seconds, YSE::THREAD thread) {
  if (clockname.empty()) return;

  if (!OnAudioThread(thread)) {
    // By name, inline. Takes the manager's mutex, which this thread is allowed
    // to do, and needs no resolved binding — so a tempo sent in the same breath
    // as CreateObject lands immediately rather than waiting for the background
    // pool to have found the clock.
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

void gSetClock::Retempo(float bpm, YSE::THREAD thread) {
  tempo.store(bpm, std::memory_order_relaxed);
  Push(bpm, ramp.load(std::memory_order_relaxed), thread);
}

bool gSetClock::ReadBeat(YSE::THREAD thread, double& beat) const {
  if (clockname.empty()) return false;

  if (!OnAudioThread(thread)) {
    // `beatPosition` answers 0 for a name it does not have, and 0 is a
    // perfectly ordinary position, so the existence question has to be asked
    // separately.
    if (!CLOCK::Manager().clockExists(clockname)) return false;
    beat = CLOCK::Manager().beatPosition(clockname);
    return true;
  }

  const clockBridge* clocks = Clocks();
  if (clocks == nullptr) return false;
  // Two acquire loads, and false while the binding is unresolved. A clock the
  // host has destroyed under a resolved binding reports the position it froze
  // at rather than going quiet — #707's rule, inherited from the bridge.
  return clocks->Beat(binding.load(std::memory_order_relaxed), beat);
}

// ─── the methods ────────────────────────────────────────────────────────────

BANG_IN(Report) {
  (void)inlet;
  double beat = 0.0;
  // Nothing at all when there is no clock: a zero here would be
  // indistinguishable from a clock genuinely sitting at beat 0, and whatever
  // this is wired into would act on a position that does not exist.
  if (!ReadBeat(thread, beat)) return;
  outputs[0].SendFloat((float)beat, thread);
}

FLOAT_IN(SetFloatTempo) {
  (void)inlet;
  Retempo(value, thread);
}

INT_IN(SetIntTempo) {
  SetFloatTempo((float)value, inlet, thread);
}

LIST_IN(Command) {
  (void)inlet;

  // Max's word for the same setting, and `.transport`'s. MatchWord requires a
  // separator after the word, so `tempos 4` is not a tempo; a substr would
  // allocate on whichever thread the message arrived on, so the match is made
  // in place.
  std::size_t at = 0;
  if (!MatchWord(value, "tempo", 5, at)) return;

  float args[2] = {0.f, 0.f};
  const int count = ReadFloatArgs(value, at, args, 2);
  // A bare `tempo`, or one followed by something that is not a number, is not a
  // tempo. Doing nothing beats picking a number.
  if (count == 0) return;
  if (count > 1) ramp.store(args[1] < 0.f ? 0.f : args[1], std::memory_order_relaxed);
  Retempo(args[0], thread);

  // Anything else — Max's `mode`, `set`, `reset` and `resolution`, a bare
  // number word, an unknown message — does nothing, deliberately, rather than
  // being read as some command it is not. See the header on why each of those
  // is absent rather than unimplemented.
}
