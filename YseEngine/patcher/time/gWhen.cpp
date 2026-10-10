
#include "gWhen.h"
#include "../../clock/clockManager.h"
#include "../pObjectList.hpp"
#include "../patcherImplementation.h"

using namespace YSE::PATCHER;

#define className gWhen

namespace {

  constexpr char kInletDoc[] =
      "Any message sends the clock's tempo and beat position. What the message says is ignored.";

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
      "On any input, sends the beat position and tempo of a named domain clock. It only reads "
      "the clock and never creates one.");
  ADD_CATEGORY(pCategory::TIME);
  INLET_DOC(0, "query", kInletDoc, "bang, int, float or any list");
  OUTLET_DOC(0, "beat", "The clock's beat position.", "");
  OUTLET_DOC(1, "tempo",
             "The clock's tempo in BPM, sent just before the beat position. 0 while the clock is "
             "stopped; during a glide, the tempo reached so far.",
             "BPM");
  PARAM_DOC("clockname", "", "The domain clock to read. Empty reads nothing.", "");
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
