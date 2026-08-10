
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
      "Sets how fast the clock runs, and reports where it has got to. An int or a float is the "
      "tempo in BPM — this is the inlet a slider or a '.line' goes into, and the difference that "
      "makes this object rather than a '.transport', where the same wire would toggle the clock on "
      "and off with every value it sent. Tempo is not clamped, a domain clock's not being clamped "
      "either: 0 stops the clock (tempo 0 is what 'stopped' means to a domain clock, so there is "
      "no separate start and stop here and nothing remembers a tempo to come back to) and a "
      "negative tempo runs it backwards. 'tempo <bpm>' is the same setting under Max's word, and "
      "an optional second number makes it a glide in seconds — 'tempo 140 2' reaches 140 over two "
      "seconds, which is the one thing the bare number cannot say and the one thing Max's setclock "
      "cannot do at all. A bang sends the clock's current beat position out the outlet; a bang "
      "with no clock to read emits nothing at all rather than a zero that would look like a real "
      "position. Max's modes ('pass', 'add', 'mul', 'interp') and its int-sets-the-time behaviour "
      "are absent: all four derive one millisecond time from another, and a YSE domain clock is a "
      "bare beat accumulator with no origin and no settable position. There is no 'clock <name>' "
      "either — this object *is* the clock, and its name is its creation argument.";

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
      "Creates one of YSE's named domain clocks and sets its speed — Max's setclock, the object "
      "that gives part of a patch a tempo of its own (issue #515). '.setclock fast 240' brings a "
      "clock called 'fast' into being, already running at 240 BPM, and every object that names it "
      "— a '.metro clock fast', a '.timepoint', a '.tempo', a '.qlist' — counts on that clock "
      "instead of the default one, which is what lets two halves of a patch move at independent "
      "tempi. It is the same clock the host addresses through yse_system_create_clock / setTempo "
      "/ beatPosition, so a patch and its host share it by name. The patcher's other two clock "
      "objects differ from this one by exactly one thing each: a '.when' only reads and creates "
      "nothing, and a '.transport' creates its clock *stopped* and spends its int and float on "
      "start and stop, so it is a play button rather than a time source. Here the number you send "
      "is the speed, in BPM, which is the wire you actually want a slider on; 0 stops the clock, "
      "because tempo 0 is what 'stopped' means to a domain clock, and 'tempo <bpm> <ramp>' glides "
      "there over the given seconds. Creation is first-registration-wins and happens on the "
      "control thread when the object joins a patcher: a name the host or another object already "
      "claimed is left exactly as it is rather than re-tempoed. Destruction is deliberately not "
      "here — the clock outlives the object, as '.transport''s does, because a patcher clock "
      "binding owns a share of its clock and is never released, so destroying the clock would "
      "freeze every object bound to it rather than hand them back the default one; teardown "
      "belongs to the host's destroyClock or System::close. Max's modes ('pass', 'add', 'mul', "
      "'interp'), its settable time and its reporting interval are all absent, because a domain "
      "clock is a bare beat accumulator with no origin and no position to set and the engine "
      "advances it once per audio block rather than polling it. Calculate() does nothing, and no "
      "message handler allocates, locks or blocks when it turns out to be running on the audio "
      "callback: the clock is written through the patcher's clock bridge there, which is three "
      "atomic stores, and by name off it.");
  ADD_CATEGORY(pCategory::TIME);
  INLET_DOC(0, "speed", kInletDoc, "BPM, bang, 'tempo <bpm> [<ramp>]'");
  OUTLET_DOC(0, "beat",
             "The clock's current beat position, on bang. Beats are the running integral "
             "of tempo, so this only ever moves forward while the tempo is positive, holds "
             "while it is 0 and runs backwards while it is negative. Nothing is emitted "
             "when there is no clock to read — not a zero, which would be indistinguishable "
             "from a real clock sitting at beat 0. The tempo is not reported beside it, as "
             "'.transport' and '.when' report the pair, because on this object the tempo is "
             "the thing you just set; '.when <name>' reads one back.",
             "");
  PARAM_DOC("clockname", "",
            "The domain clock this object creates and is. Clocks are addressed by name across the "
            "whole engine, so this is the same string the host passes to createClock and the same "
            "one a '.metro clock <name>' runs on. Empty (the default) means the object is nothing: "
            "it creates no clock, binds nothing, ignores every command and emits nothing. The "
            "clock is made when the object is added to a patcher, first registration winning — a "
            "name someone already claimed is left exactly as it is, tempo included — and it is "
            "never destroyed, not on delete and not on a rename, because the bindings other "
            "objects hold on it are never released. There is no 'clock <name>' message: this "
            "object is the clock, so re-pointing it is a re-create, which is also what keeps clock "
            "creation on the control thread.",
            "");
  PARAM_DOC("tempo", "120",
            "The tempo in BPM the clock runs at. Unlike '.transport', which creates its clock "
            "stopped and waits to be started, this is the clock's *initial* tempo as well as its "
            "current one: a '.setclock' is turning as soon as the patch loads, which is what makes "
            "it a time source rather than a transport. Written to the clock again on every number "
            "the object receives. Not clamped, a domain clock's tempo not being clamped either — "
            "0 stops it and a negative value runs it backwards.",
            "BPM");
  PARAM_DOC("ramp", "0",
            "Seconds every tempo change glides over, 0 being instant. This is domainClock's "
            "rampable tempo, which is what lets a patch accelerando rather than jump, and it is "
            "the one thing this object has that Max's setclock does not. It applies to every "
            "number sent to the inlet; the clock's initial tempo is always instant, there being "
            "nothing to glide from.",
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
