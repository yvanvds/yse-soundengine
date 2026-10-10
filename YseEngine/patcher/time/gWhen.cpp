
#include "gWhen.h"
#include "../../clock/clockManager.h"
#include "../pObjectList.hpp"
#include "../patcherImplementation.h"

using namespace YSE::PATCHER;

#define className gWhen

namespace {

  constexpr char kInletDoc[] =
      "Reports where the named clock stands. Any input at all does it — a bang, an int, a "
      "float or any list — which is Max's own reading of 'when' and what makes the object "
      "safe on the end of something that is already firing: a '.metro' into a '.when' "
      "samples the clock every tick, where the same wiring into a '.transport' would toggle "
      "it. The value sent in is ignored; its arrival is the whole message. The beat position "
      "goes out the left outlet and the tempo out the right, right outlet first. With no "
      "clock to read, nothing at all is emitted — not a zero, which would be "
      "indistinguishable from a real clock sitting at beat 0. There is no 'clock <name>': "
      "Max's 'when' has no such method (a 'clock main' sent to one reports the time like any "
      "other message), and the clock a '.when' reads is its creation argument.";

} // namespace

CONSTRUCT() {
  ADD_IN_0;
  REG_BANG_IN(ReportBang);
  REG_INT_IN(ReportInt);
  REG_FLOAT_IN(ReportFloat);
  REG_LIST_IN(ReportList);

  ADD_OUT_FLOAT; // beat position
  ADD_OUT_FLOAT; // tempo

  ADD_PARAM(clockname);

  ADD_DESCRIPTION(
      "Reports the current position of one of YSE's named domain clocks, on demand — Max's "
      "'when', and the read-only half of '.transport' (issue #514). '.when main' answers for "
      "the clock called 'main': the beat position out the left outlet and the tempo in BPM "
      "out the right, sent right to left, on any input at all. That is the same clock "
      "yse_system_create_clock / beatPosition address from the host, the same one a clip "
      "transport plays on, and the same one a '.metro clock main' counts its beats on. A "
      "'.transport' can already report this, so the reason to reach for a '.when' instead is "
      "that a '.transport' *creates* the clock it names when it joins a patcher: asking what "
      "beat 'main' is on by dropping a transport into the patch would conjure a stopped "
      "'main' that every other object bound to that name would then be waiting on. A '.when' "
      "creates nothing and destroys nothing — it binds a name and reads it — which is "
      "'.timepoint''s and '.tempo''s contract, and a name nothing has claimed simply reports "
      "nothing until a '.transport' or the host brings the clock into being. The second "
      "difference is the inlet: a '.transport''s int and float start and stop the clock, "
      "while every input to a '.when' is a query, so this is the object that goes on the end "
      "of whatever already fires at the moments you want to sample musical time at. Max's "
      "bars.beats.units and ticks are not reported because a domain clock has neither — it "
      "is a bare beat accumulator with no origin and no meter, the same fact that costs "
      "'.transport' its 'seek'. Calculate() does nothing, and the report does not allocate, "
      "lock or block when it turns out to be running on the audio callback: the clock is "
      "read through the patcher's clock bridge there, which is two acquire loads per value, "
      "and by name off it.");
  ADD_CATEGORY(pCategory::TIME);
  INLET_DOC(0, "query", kInletDoc, "bang, int, float or any list");
  OUTLET_DOC(0, "beat",
             "The clock's current beat position, on any input. Beats are the running "
             "integral of tempo, so this only ever moves forward while the tempo is "
             "positive and holds while the clock is stopped. Nothing is emitted when "
             "there is no clock to read.",
             "");
  OUTLET_DOC(1, "tempo",
             "The clock's current tempo in BPM, on any input, sent before the beat "
             "position so the pair arrives right to left as Max orders outlets. A "
             "clock paused by a '.transport' stop reads 0 here, tempo 0 being how the "
             "engine spells 'stopped', and a ramp in progress reads the tempo it has "
             "reached rather than the one it is heading for.",
             "BPM");
  PARAM_DOC("clockname", "",
            "The domain clock this object reports on. Clocks are addressed by name across the "
            "whole engine, so this is the same string the host passes to createClock and the same "
            "one a '.transport <name>' drives. Empty (the default) means the object reads nothing "
            "and emits nothing. The name is bound when the object is added to a patcher — bound, "
            "not created: a clock nobody has made yet leaves the object silent, and it starts "
            "answering when the clock appears. There is no 'clock <name>' message, so re-pointing "
            "a '.when' at another clock is a re-create — '.transport''s rule, and Max's, where "
            "'when' takes its transport as a creation argument and treats every message as a "
            "query.",
            "");
}

// ─── binding: read, never create ────────────────────────────────────────────

void gWhen::SetParent(pObject* parent) {
  pObject::SetParent(parent);

  // A standalone object has no patcher and therefore no bridge. Its reports
  // still go out by name, so one built beside a clock the host made does answer;
  // what it will never do is bind, which is the audio-callback route only.
  if (parent == nullptr) return;
  if (clockname.empty()) return;

  clockBridge* clocks = Clocks();
  if (clocks == nullptr) return;

  // Bind only. `.transport` creates clocks and this object does not — see the
  // header — so there is no CLOCK::Manager() call here, and dropping a `.when`
  // into a patch cannot bring a stopped clock into being. Binding is idempotent
  // by name, so this shares a slot with the `.transport` driving the clock
  // rather than costing one of its own; a refusal (a full bridge) leaves the
  // handle at 0, which loses the audio-callback route and keeps the by-name one.
  binding.store(clocks->Bind(clockname.c_str(), clockname.size()), std::memory_order_relaxed);
}

// ─── which route this handler may take ──────────────────────────────────────

bool gWhen::OnAudioThread(YSE::THREAD thread) const {
  // The `THREAD` tag is dispatch semantics, not thread identity: in-patcher
  // delivery dispatches T_DSP, and the drains at the top of Calculate dispatch
  // T_GUI *from the audio callback*. Only the patcher knows, and only since
  // #690 — so ask it, exactly as `.transport`, `.metro` and `.s` do. A
  // standalone object has no patcher and is never rendered, so it answers false.
  if (parent == nullptr) return false;
  return static_cast<patcherImplementation*>(parent)->CallingThread(thread) == YSE::T_DSP;
}

bool gWhen::ReadClock(YSE::THREAD thread, double& beat, float& bpm) const {
  if (clockname.empty()) return false;

  if (!OnAudioThread(thread)) {
    // `beatPosition` and `currentTempo` both answer 0 for a name they do not
    // have, and 0 is a perfectly ordinary position and tempo, so the existence
    // question has to be asked separately. This route needs no resolved
    // binding, so a bang in the same breath as CreateObject already answers.
    if (!CLOCK::Manager().clockExists(clockname)) return false;
    beat = CLOCK::Manager().beatPosition(clockname);
    bpm = CLOCK::Manager().currentTempo(clockname);
    return true;
  }

  const clockBridge* clocks = Clocks();
  if (clocks == nullptr) return false;
  const clockBridge::Handle bound = binding.load(std::memory_order_relaxed);
  // Two acquire loads per value, and false while the binding is unresolved. A
  // clock the host has destroyed under a resolved binding reports the values it
  // froze at rather than going quiet — #707's rule, inherited from the bridge.
  return clocks->Beat(bound, beat) && clocks->Tempo(bound, bpm);
}

// ─── Max's one method, under four names ─────────────────────────────────────

void gWhen::Report(YSE::THREAD thread) {
  double beat = 0.0;
  float bpm = 0.f;
  // Nothing at all when there is no clock: a zero here would be
  // indistinguishable from a clock genuinely sitting at beat 0, and whatever
  // this is wired into would act on a position that does not exist.
  if (!ReadClock(thread, beat, bpm)) return;
  // Right to left, Max's outlet order.
  outputs[1].SendFloat(bpm, thread);
  outputs[0].SendFloat((float)beat, thread);
}

BANG_IN(ReportBang) {
  (void)inlet;
  Report(thread);
}

INT_IN(ReportInt) {
  // Max: "int — equivalent to anything". The value is not a setting and not a
  // selector; its arrival is the whole message.
  (void)inlet;
  (void)value;
  Report(thread);
}

FLOAT_IN(ReportFloat) {
  (void)inlet;
  (void)value;
  Report(thread);
}

LIST_IN(ReportList) {
  // Max: "any list or message causes the current time to be sent out". Nothing
  // is read out of the message, which is also why this handler cannot allocate
  // on a thread that turns out to be the audio callback.
  (void)inlet;
  (void)value;
  Report(thread);
}
