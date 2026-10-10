#include "gPreset.h"
#include "../../implementations/logImplementation.h"
#include "../math/gExprEval.h"
#include "../pHandle.hpp"
#include "../pListArgs.h"
#include "../pSelector.h"
#include "../patcherImplementation.h"
#include <cmath>

using namespace YSE::PATCHER;

#define className gPreset

namespace {

  // An int as text, through the patcher's one int formatter rather than
  // std::to_string. Control-thread only here — the GUI value and the
  // construction-time clamp log.
  std::string IntText(int value) {
    char digits[FORMAT_INT_WIDTH];
    const std::size_t written = WriteInt(value, digits);
    return std::string(digits, written);
  }

  // The bounds of the token starting at or after `from`, or false when there is
  // none. Walked in place — the family's token walk, kept allocation-free so
  // the audio-callback parse (a message handed off to the timer thread,
  // issue #952) costs nothing.
  bool NextToken(const char* text, std::size_t length, std::size_t from, std::size_t& begin,
                 std::size_t& end) {
    begin = from;
    while (begin < length && IsSelectorSeparator(text[begin]))
      begin++;
    end = begin;
    while (end < length && !IsSelectorSeparator(text[end]))
      end++;
    return end > begin;
  }

  // The next token that reads as a number, advancing `from` past it. Tokens
  // that are not numbers are skipped rather than ending the walk — the
  // family's policy.
  bool NextNumber(const char* text, std::size_t length, std::size_t& from, float& out) {
    std::size_t begin = 0;
    std::size_t end = 0;
    while (NextToken(text, length, from, begin, end)) {
      from = end;
      if (ReadNumericToken(text + begin, end - begin, out)) return true;
    }
    return false;
  }

  // Whether the `length` characters at `text` are exactly `word`.
  bool TokenIs(const char* text, std::size_t length, const char* word, std::size_t wordLength) {
    if (length != wordLength) return false;
    for (std::size_t i = 0; i < length; i++) {
      if (text[i] != word[i]) return false;
    }
    return true;
  }

  constexpr char kInDoc[] =
      "A number, or 'recall <n>', recalls that slot; a bang recalls the active slot again; "
      "'store <n>' captures the patch into slot n; 'clear <n>', 'clear' and 'clearall' empty "
      "slots.";

  constexpr char kRecalledOutDoc[] =
      "The slot just recalled, sent once every value is back in place. Nothing for an empty "
      "slot.";

  constexpr char kStoredOutDoc[] = "The slot just stored.";

} // namespace

CONSTRUCT() {
  // One inlet, hot: recalls, the bang, and the command messages.
  ADD_IN_0;
  REG_BANG_IN(BangIn);
  REG_INT_IN(IntIn);
  REG_FLOAT_IN(FloatIn);
  REG_LIST_IN(ListIn);

  ADD_OUT_INT; // 0: slot just recalled
  ADD_OUT_INT; // 1: slot just stored

  // The bank is sized by the creation argument, so a saved `.preset 8` comes
  // back eight slots wide. Registering both callbacks makes
  // ParamsNeedRebuild() true, so a live re-parse replaces the object through
  // the graph swap rather than resizing the bank under a concurrent recall —
  // and the replacement starts with empty slots, `.function`'s arrangement.
  REG_PARM_CLEAR;
  REG_PARM_PARSE;

  ADD_PARAM(creationArgs);

  ShapeStore();

  // The timer slot a callback-side message is handed off through (issue
  // #952), for the rest of this object's life. Claimed here, on the control
  // thread, so no handler ever has to; given back in the destructor. A
  // refusal — the process holding timerBridge::CAPACITY owners at once —
  // leaves control-thread messages untouched and counts every callback-side
  // one as dropped.
  timerSlot = TimerBridge().Claim(&gPreset::DeferTrampoline, this);

  ADD_DESCRIPTION("Stores and recalls snapshots of the patch's controls. 'store <n>' captures "
                  "every control whose value can be restored into slot n, and a number recalls "
                  "that slot. The slots are saved with the patch.");
  ADD_CATEGORY(pCategory::GUI);
  INLET_DOC(0, "control", kInDoc, "slot 0 to slots-1");
  OUTLET_DOC(0, "recalled", kRecalledOutDoc, "0 to slots-1");
  OUTLET_DOC(1, "stored", kStoredOutDoc, "0 to slots-1");
  PARAM_DOC("slots", "32",
            "How many slots the bank holds. Changing it replaces the object, and the new one "
            "starts with empty slots.",
            "1-1024");
}

// ─── the bank ─────────────────────────────────────────────────────────────────

void gPreset::ShapeStore() {
  // Rebuilt rather than resized: safe because every caller runs before the
  // object is wired or published — the constructor and the two parameter
  // callbacks; a *live* SetParams never reaches here on a published object,
  // since registering the callbacks makes ParamsNeedRebuild() true and #234
  // replaces the object instead.
  int requested = DEFAULT_SLOTS;
  int wanted = DEFAULT_SLOTS;
  int read = 0;

  for (const std::string& token : creationArgs) {
    if (read >= 1) break;

    // Parameters::Set splits on single spaces, so a run of them yields empty
    // tokens; an empty argument is not a number.
    if (token.empty()) continue;

    float number = 0.f;
    // Strict on purpose, as the rest of the family is: a creation argument
    // either is a whole finite number or it is not a capacity at all.
    if (!ReadNumericToken(token, number)) continue;

    wanted = ExprToInt(number);
    requested = wanted;
    if (requested > MAX_SLOTS) requested = MAX_SLOTS;
    if (requested < MIN_SLOTS) requested = MIN_SLOTS;
    read++;
  }

  // The control thread, before the object is wired or published — the one
  // place a clamp can be *said* rather than merely observed through
  // Capacity().
  if (read > 0 && wanted != requested) {
    INTERNAL::LogImpl().emit(E_WARNING, std::string("patcher: .preset slot capacity ") +
                                            IntText(wanted) + " is outside " + IntText(MIN_SLOTS) +
                                            "-" + IntText(MAX_SLOTS) + "; clamped to " +
                                            IntText(requested));
  }

  capacity = requested;
  slots.assign((std::size_t)capacity, std::vector<Entry>());
  scratch.clear();
  active.store(-1, std::memory_order_relaxed);
}

PARM_CLEAR() {
  // Runs on the control thread before the parameter string is re-read, and is
  // the whole of SetParams(""): Parameters::Set returns without calling the
  // parse callback for an empty argument, so this has to leave the
  // no-argument object behind rather than one still holding the previous
  // capacity.
  creationArgs.clear();
  ShapeStore();
}

PARM_PARSE() {
  ShapeStore();
}

// ─── which thread this is, physically ─────────────────────────────────────────

bool gPreset::OnAudioThread(YSE::THREAD thread) const {
  // The `THREAD` tag is dispatch semantics, not thread identity: in-patcher
  // delivery dispatches T_DSP, and the drains at the top of Calculate dispatch
  // T_GUI *from the audio callback*. Only the patcher knows, and only since
  // #690 — so ask it, exactly as `.metro`, `.s` and the time family do. A
  // standalone object has no patcher to ask and trusts the tag: unlike
  // `.when`, which answers false because a standalone object is never
  // rendered, this object's work must never run on the callback, so the
  // conservative reading is the honest one — and it is what lets a test rig
  // exercise the hand-off path at all.
  if (parent == nullptr) return thread == YSE::T_DSP;
  return static_cast<patcherImplementation*>(parent)->CallingThread(thread) == YSE::T_DSP;
}

// ─── the audio-callback hand-off (issue #952) ─────────────────────────────────

namespace {
  // The preset whose deferred drain this thread is inside, or null. Teardown
  // reached from inside that drain (a recall whose fan-out deletes the preset)
  // must not take the bridge's blocking stop: the reconciler may hold the slot
  // while waiting for this very callback to return — gMetro's #721 trap.
  thread_local const void* tDrainingPreset = nullptr;

  struct drainFrame {
    const void* previous;
    explicit drainFrame(const void* self) : previous(tDrainingPreset) {
      tDrainingPreset = self;
    }
    ~drainFrame() {
      tDrainingPreset = previous;
    }
    drainFrame(const drainFrame&) = delete;
    drainFrame& operator=(const drainFrame&) = delete;
    drainFrame(drainFrame&&) = delete;
    drainFrame& operator=(drainFrame&&) = delete;
  };
} // namespace

void gPreset::Submit(Op op, int slot, YSE::THREAD thread) {
  if (OnAudioThread(thread)) {
    Defer(op, slot);
    return;
  }
  Execute(op, slot, thread);
}

void gPreset::Execute(Op op, int slot, YSE::THREAD thread) {
  switch (op) {
  case Op::RECALL:
    Recall(slot, thread);
    break;
  case Op::RECALL_ACTIVE:
    // The resync. Recall's range check answers the no-active case (-1) with
    // silence.
    Recall(active.load(std::memory_order_relaxed), thread);
    break;
  case Op::STORE:
    Store(slot, thread);
    break;
  case Op::CLEAR:
    ClearSlot(slot);
    break;
  case Op::CLEAR_ACTIVE:
    ClearSlot(active.load(std::memory_order_relaxed));
    break;
  case Op::CLEAR_ALL:
    ClearAll();
    break;
  }
}

void gPreset::Defer(Op op, int slot) {
  // Audio callback: a queue push, and for the first message of a burst an
  // exchange plus the bridge's wait-free request. Nothing here allocates,
  // locks or waits, and nothing is logged — the count is the report.
  if (timerSlot == 0 || closed.load(std::memory_order_relaxed)) {
    deferredDropped.fetch_add(1, std::memory_order_relaxed);
    return;
  }
  Request request;
  request.op = op;
  request.slot = slot;
  if (!deferred.try_push(request)) {
    deferredDropped.fetch_add(1, std::memory_order_relaxed);
    return;
  }
  // Only the producer that flips `armed` from clear to set asks for the
  // wake-up, so a start request is never issued while the callback may still
  // be issuing its stop. acq_rel: the release publishes the push above to the
  // callback's own exchange; the acquire orders this start after the stop the
  // callback requested before it cleared the flag.
  if (!armed.exchange(true, std::memory_order_acq_rel)) {
    TimerBridge().RequestStart(timerSlot, 1);
  }
}

void gPreset::DeferTrampoline(void* ctx) {
  static_cast<gPreset*>(ctx)->DrainDeferred();
}

void gPreset::RunDeferred() {
  Request request;
  while (deferred.try_pop(request)) {
    // A closed preset's queue is emptied without running anything: the patch
    // it would reach into is being taken apart.
    if (closed.load(std::memory_order_acquire)) continue;
    // T_GUI: the timer thread, exactly the tag a `.metro`-driven recall has
    // always carried.
    Execute(request.op, request.slot, YSE::T_GUI);
  }
}

void gPreset::DrainDeferred() {
  // Timer thread. A tick that finds `armed` clear is a leftover of a stop the
  // bridge has not reconciled yet: nothing is ours to drain or to stop, and
  // requesting a stop here could race a producer's start.
  if (!armed.load(std::memory_order_acquire)) return;

  const drainFrame frame(this);
  RunDeferred();

  // Stop first, then give `armed` up, then look once more. The stop is
  // requested while `armed` is still set, so no producer can be requesting a
  // start at the same moment. The exchange then reads the last producer's
  // write to `armed` — acquire, so every push made by a producer that saw
  // the flag still set (and therefore did not wake anyone) is visible to the
  // drain below. A push after the exchange finds the flag clear and wakes
  // the timer itself, ordered after this stop.
  TimerBridge().RequestStop(timerSlot);
  armed.exchange(false, std::memory_order_acq_rel);
  RunDeferred();
}

void gPreset::Teardown(YSE::THREAD thread) {
  (void)thread;
  closed.store(true, std::memory_order_release);
  if (timerSlot == 0) return;
  // From inside this preset's own drain the drain is the thing to wait for;
  // `closed` already makes the rest of it a no-op.
  if (tDrainingPreset == this) return;
  // The blocking stop: once it returns, no drain is still running into the
  // patch, which is what lets the patcher delete handles after this pass. A
  // producer racing it can wake the timer once more; that drain sees `closed`
  // and runs nothing.
  TimerBridge().ApplyStop(timerSlot);
}

gPreset::~gPreset() {
  if (timerSlot != 0) {
    // Stops the timer and waits out a drain in flight — timerBridge::Release
    // keeps ClearTimer's handshake for exactly this caller. A destructor
    // never runs on the audio callback, so it may block.
    TimerBridge().Release(timerSlot);
    timerSlot = 0;
  }
}

// ─── store and recall ─────────────────────────────────────────────────────────

void gPreset::Store(int slot, YSE::THREAD thread) {
  if (slot < 0 || slot >= capacity) return;
  auto* p = static_cast<patcherImplementation*>(parent);
  // A standalone object has no patch to capture.
  if (p == nullptr) return;

  // `.bag`'s loser-drops re-entrancy guard: a store reached from inside a
  // recall's own fan-out (an outlet wired around to this inlet) would run
  // over the staging buffer the outer operation is still using.
  storeGuard reentry(sending);
  if (!reentry.Held()) return;

  // Capture first, into the staging buffer, with the slot guard released:
  // the GetGuiValue answers allocate and none of this touches the slots.
  // Participation is checked *before* the value is read, so a consume-on-read
  // object (`.b`) is never polled and capturing a patch cannot eat a press.
  //
  // The objects are read with mtx released — a GetGuiValue may be anything an
  // object likes — so another control thread may delete one mid-walk. The pin,
  // taken before the snapshot, keeps every object in it allocated until the
  // walk is done (issue #961); a handle would not be, so none is used.
  scratch.clear();
  const patcherImplementation::objectPin pin(*p);
  p->CollectObjects(walk);
  for (pObject* object : walk) {
    if (!object->GuiValueIsSettable()) continue;
    std::string value = object->GetGuiValue();
    // An empty string is not a message an inlet can take back, so the object
    // is left out of the slot and a recall leaves it untouched.
    if (value.empty()) continue;
    Entry entry;
    entry.id = object->GetID();
    entry.type = object->Type();
    entry.value = std::move(value);
    scratch.push_back(std::move(entry));
  }
  walk.clear();

  {
    storeGuard guard(busy);
    if (!guard.Held()) return;
    // Swap rather than copy: the old contents land in the staging buffer and
    // are released by the next operation — on a control-side thread, which
    // the audio-callback hand-off (Submit) guarantees this is.
    slots[(std::size_t)slot].swap(scratch);
  }

  active.store(slot, std::memory_order_relaxed);
  outputs[1].SendInt(slot, thread);
}

void gPreset::Recall(int slot, YSE::THREAD thread) {
  if (slot < 0 || slot >= capacity) return;
  auto* p = static_cast<patcherImplementation*>(parent);
  // A standalone object has no patch to restore into.
  if (p == nullptr) return;

  storeGuard reentry(sending);
  if (!reentry.Held()) return;

  {
    storeGuard guard(busy);
    if (!guard.Held()) return;
    const std::vector<Entry>& stored = slots[(std::size_t)slot];
    // Max: recalling an empty preset does nothing — and announces nothing,
    // so the active marker keeps naming a slot that holds something.
    if (stored.empty()) return;
    // Copied out so the pushes below run with the guard released: each one is
    // a whole subgraph, and holding the guard across it would make a patch
    // that stores from inside its own recall fan-out lose the slot table to
    // the guard it is still holding.
    scratch = stored;
  }

  active.store(slot, std::memory_order_relaxed);

  // Each object is looked up under mtx and pushed with mtx released — the push
  // runs a whole subgraph that may take mtx again — so another control thread
  // may delete it in between. The pin, taken before any lookup, keeps every
  // object found live allocated until the recall is done (issue #961). The
  // object is used rather than its handle: DeleteObject frees a handle at once.
  const patcherImplementation::objectPin pin(*p);
  for (const Entry& entry : scratch) {
    pObject* object = p->GetObjectFromID(entry.id);
    if (object == nullptr) continue;
    // Storage IDs are reused after a delete (issue #733): without this check
    // a recall could write one control's state into whatever object inherited
    // its number. A stale entry is skipped; the rest of the slot still lands.
    if (entry.type != object->Type()) continue;
    if (!object->GuiValueIsSettable()) continue;
    // The ordinary control-thread message path — what pHandle::SetListData,
    // the call a host makes, does — so every clamp, side effect and outlet
    // send the object's own inlet handler performs still happens. Never a
    // poke into its fields.
    PATCHER::inlet* in = p->ResolveInlet(object, 0);
    if (in != nullptr) in->SetList(entry.value, YSE::T_GUI);
  }

  // Announced after the pushes, so whatever this triggers sees the patch
  // already in the preset.
  outputs[0].SendInt(slot, thread);
}

void gPreset::ClearSlot(int slot) {
  if (slot < 0 || slot >= capacity) return;
  storeGuard guard(busy);
  if (!guard.Held()) return;
  slots[(std::size_t)slot].clear();
  // The number no longer names stored contents.
  if (active.load(std::memory_order_relaxed) == slot) {
    active.store(-1, std::memory_order_relaxed);
  }
}

void gPreset::ClearAll() {
  storeGuard guard(busy);
  if (!guard.Held()) return;
  for (std::vector<Entry>& slot : slots)
    slot.clear();
  active.store(-1, std::memory_order_relaxed);
}

// ─── commands ─────────────────────────────────────────────────────────────────

bool gPreset::HandleCommand(const char* word, std::size_t length, const std::string& message,
                            std::size_t argOffset, YSE::THREAD thread) {
  const char* text = message.c_str();
  const std::size_t total = message.size();

  if (TokenIs(word, length, "store", 5)) {
    std::size_t cursor = argOffset;
    float slot = 0.f;
    if (!NextNumber(text, total, cursor, slot)) return true;
    // NaN fails the compare, so it never reaches ExprToInt; a negative slot
    // is refused by the range check either way.
    if (!(slot >= 0.f)) return true;
    Submit(Op::STORE, ExprToInt(slot), thread);
    return true;
  }

  if (TokenIs(word, length, "recall", 6)) {
    std::size_t cursor = argOffset;
    float slot = 0.f;
    if (!NextNumber(text, total, cursor, slot)) return true;
    if (!(slot >= 0.f)) return true;
    Submit(Op::RECALL, ExprToInt(slot), thread);
    return true;
  }

  if (TokenIs(word, length, "clearall", 8)) {
    Submit(Op::CLEAR_ALL, 0, thread);
    return true;
  }

  if (TokenIs(word, length, "clear", 5)) {
    std::size_t cursor = argOffset;
    float slot = 0.f;
    if (NextNumber(text, total, cursor, slot)) {
      if (!(slot >= 0.f)) return true;
      Submit(Op::CLEAR, ExprToInt(slot), thread);
    } else {
      // Bare `clear` empties the active slot — the parallel of the bang.
      Submit(Op::CLEAR_ACTIVE, 0, thread);
    }
    return true;
  }

  return false;
}

// ─── inlets ───────────────────────────────────────────────────────────────────
//
// Every handler parses in place and ends in Submit, which runs the operation
// on a control-side thread or hands it to the timer thread from the audio
// callback (issue #952). The parse allocates nothing, so it is the same code
// on both routes.

BANG_IN(BangIn) {
  if (inlet != 0) return;
  // The resync: recall the active slot again.
  Submit(Op::RECALL_ACTIVE, 0, thread);
}

INT_IN(IntIn) {
  if (inlet != 0) return;
  // Max: "recalls the preset whose number is received".
  Submit(Op::RECALL, value, thread);
}

FLOAT_IN(FloatIn) {
  if (inlet != 0) return;
  // Truncated the way every numeric control truncates. A NaN names no slot.
  if (std::isnan(value)) return;
  Submit(Op::RECALL, ExprToInt(value), thread);
}

LIST_IN(ListIn) {
  // Registered on inlet 0 only, but routed anyway: a later inlet must never
  // start driving the bank because someone added a handler above.
  if (inlet != 0) return;

  const char* text = value.c_str();
  const std::size_t length = value.size();

  std::size_t begin = 0;
  std::size_t end = 0;
  // An empty list addresses nothing.
  if (!NextToken(text, length, 0, begin, end)) return;

  // A command first. None of the reserved words is a number, so a slot
  // number can never collide with one.
  if (HandleCommand(text + begin, end - begin, value, end, thread)) return;

  // A bare number is the int, as a list — the same recall a `.m 3` sends.
  float slot = 0.f;
  if (!ReadNumericToken(text + begin, end - begin, slot)) return;
  if (!(slot >= 0.f)) return;
  Submit(Op::RECALL, ExprToInt(slot), thread);
}

// ─── the GUI value ────────────────────────────────────────────────────────────

GUI_VALUE() {
  // The active slot, -1 when none — what a host highlights in a row of
  // preset dots. One call, one allocation, host thread; the atomic is the
  // whole synchronisation. Read-only on purpose: recalling is inlet 0's job,
  // and staying unsettable is also what keeps `.preset` out of its own
  // snapshots.
  char digits[FORMAT_INT_WIDTH];
  const std::size_t written = WriteInt(active.load(std::memory_order_relaxed), digits);
  return std::string(digits, written);
}

// ─── persistence ──────────────────────────────────────────────────────────────

bool gPreset::DumpState(nlohmann::json::value_type& json) {
  auto* p = static_cast<patcherImplementation*>(parent);

  // Control thread — patcherImplementation::DumpJSON holds mtx — but the
  // guard is still taken, because a timer-thread recall may land mid-save.
  // mtx is already held here and is not recursive, so the patcher is read
  // through its *Unlocked accessors: the plain ones take mtx (issue #937).
  //
  // Only `busy` is waited for, through saveGuard's bounded wait, rather than
  // skipped at the first miss and the slots lost (issue #940). Every `busy`
  // section is a few copies that never takes mtx; `sending`, which a recall
  // holds across its GetObjectFromID lookups, is never touched here, so a
  // timer-thread recall blocked on this save's mtx cannot hold up the save.
  const saveGuard guard(busy);
  if (!guard.Held()) return false;

  // Unconditional, `.coll`'s always-rule — but an untouched object writes
  // nothing, so its serialised form is byte for byte what it would be
  // without the hook.
  const int act = active.load(std::memory_order_relaxed);
  bool anySlot = false;
  for (int i = 0; i < capacity && !anySlot; i++) {
    if (!slots[(std::size_t)i].empty()) anySlot = true;
  }
  if (act < 0 && !anySlot) return true;

  if (act >= 0) json["active"] = act;

  // A standalone object is never serialised through a patcher and has no
  // live set to rank against; nothing more to write.
  if (!anySlot || p == nullptr) return true;

  const unsigned int count = p->ObjectsUnlocked();
  for (int i = 0; i < capacity; i++) {
    const std::vector<Entry>& stored = slots[(std::size_t)i];
    if (stored.empty()) continue;

    nlohmann::json slotJson;
    slotJson["slot"] = i;
    for (const Entry& entry : stored) {
      // An entry whose object no longer exists — or whose ID now names an
      // object of another type — could never be pushed again; it is dropped
      // from the file rather than written as a number that would land on the
      // wrong object after the load's renumbering.
      YSE::pHandle* handle = p->GetHandleFromIDUnlocked(entry.id);
      if (handle == nullptr) continue;
      if (entry.type != handle->Type()) continue;

      // The rank of the ID among the live objects — the dump's own record
      // order ("object 0", "object 1", ...), which is exactly the fresh
      // numbering ParseJSON hands out (issue #730). Spelled as the rank
      // rather than the raw ID so a patch whose IDs went sparse through
      // deletes still recalls correctly after a save and a load.
      unsigned int rank = 0;
      for (unsigned int o = 0; o < count; o++) {
        YSE::pHandle* other = p->GetHandleFromListUnlocked(o);
        if (other != nullptr && other->GetID() < entry.id) rank++;
      }

      nlohmann::json entryJson;
      entryJson["object"] = rank;
      entryJson["type"] = entry.type;
      entryJson["value"] = entry.value;
      slotJson["objects"].push_back(entryJson);
    }

    // A slot whose every entry went stale writes nothing.
    if (slotJson.find("objects") != slotJson.end()) json["slots"].push_back(slotJson);
  }
  return true;
}

void gPreset::RestoreState(const nlohmann::json::value_type& json) {
  storeGuard guard(busy);
  if (!guard.Held()) return;

  for (std::vector<Entry>& slot : slots)
    slot.clear();

  int act = json.value("active", -1);
  // A hand-edited marker outside the bank names nothing.
  if (act < -1 || act >= capacity) act = -1;
  active.store(act, std::memory_order_relaxed);

  const auto stored = json.find("slots");
  if (stored == json.end() || !stored->is_array()) return;

  for (const auto& slotJson : *stored) {
    const int slot = slotJson.value("slot", -1);
    // Out of range — a hand-edited file, or a dump from a wider bank — is
    // dropped per slot rather than refusing the rest.
    if (slot < 0 || slot >= capacity) continue;
    const auto objects = slotJson.find("objects");
    if (objects == slotJson.end() || !objects->is_array()) continue;

    std::vector<Entry>& target = slots[(std::size_t)slot];
    target.clear();
    for (const auto& entryJson : *objects) {
      // The rank written by DumpState *is* the storage ID a load hands the
      // object — see the class comment for the invariant, and for the one
      // case (a parse into a non-empty patcher) where it does not hold.
      const int id = entryJson.value("object", -1);
      if (id < 0) continue;
      Entry entry;
      entry.id = (unsigned int)id;
      entry.type = entryJson.value("type", std::string());
      entry.value = entryJson.value("value", std::string());
      // An entry that cannot name or restore anything is dead weight.
      if (entry.type.empty() || entry.value.empty()) continue;
      target.push_back(std::move(entry));
    }
  }
}

// ─── diagnostics ──────────────────────────────────────────────────────────────

std::size_t gPreset::SlotEntryCount(int slot) const {
  if (slot < 0 || slot >= capacity) return 0;
  storeGuard guard(busy);
  if (!guard.Held()) return 0;
  return slots[(std::size_t)slot].size();
}

#undef className
