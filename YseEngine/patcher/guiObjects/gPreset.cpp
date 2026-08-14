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
  // the refusal path (a message dropped on the audio callback) costs nothing.
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
      "The whole object, and the only inlet. An int or a float (truncated) recalls that slot: "
      "every value captured in it is pushed back into its object as an ordinary list message on "
      "inlet 0 - pHandle::SetListData, the same call a host makes - never by writing another "
      "object's state directly, and outlet 0 announces the slot once the patch is in it. A bang "
      "recalls the active slot again, the resync a patch wants after editing by hand; nothing "
      "when no slot is active. 'store <n>' captures the patch into slot n - the headless "
      "spelling of Max's shift-click - and announces it out outlet 1; 'recall <n>' is the int "
      "spelled out; 'clear <n>' empties one slot, bare 'clear' the active one, 'clearall' every "
      "slot - clearing the active slot clears the active marker with it. Participation is issue "
      "#551's settable promise: exactly the objects answering true to GuiValueIsSettable() are "
      "captured, checked before the value is read, so a consume-on-read object is never polled; "
      "an object whose bulk read is empty at store time is left out and recall leaves it "
      "untouched. Recalling an empty or out-of-range slot does nothing and announces nothing. A "
      "slot entry is pushed only while its storage ID still names a live object of the captured "
      "type - IDs are reused after a delete - and a stale entry is skipped silently. Everything "
      "here is control-thread work: store walks the patch and recall runs whole subgraphs, so a "
      "message that physically arrives on the audio callback (a deferred .delay drain, a cord "
      "inside the render traversal) is dropped whole before any work. A .metro or MIDI-driven "
      "recall works - those dispatch from control-side threads. Not here: interpolated recall "
      "(Max's preset is instant; interpolation is pattrstorage's, on the excluded pattr system) "
      "and every mouse and drawing concern.";

  constexpr char kRecalledOutDoc[] =
      "The slot just recalled, as an int, sent after every captured value has been pushed back "
      "into its object - so whatever this outlet triggers sees the patch already in the preset. "
      "Max's 'preset number recalled' outlet. Silent for an empty or out-of-range slot, for a "
      "bang with no active slot, and for a store: the family's rule that an object with no "
      "answer stays quiet.";

  constexpr char kStoredOutDoc[] =
      "The slot just stored, as an int - Max's 'preset number stored' outlet, and the hook a "
      "patch hangs bookkeeping on (a .coll of slot names, a dirty flag for the host). Sent "
      "after the capture has landed in the slot and the slot has become active. Silent for a "
      "slot out of range.";

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

  ADD_DESCRIPTION(
      "Snapshot and recall of the patch's control values - Max's preset, 'store and recall the "
      "values of the control objects in a patcher'. A bank of numbered slots: 'store <n>' "
      "captures the current value of every participating control into slot n, an int (or "
      "'recall <n>') pushes each captured value back into its object, a bang recalls the active "
      "slot again, and 'clear <n>' / 'clear' / 'clearall' empty one, the active, or every slot. "
      "Participation is the GUI value protocol's settable promise (issue #551): exactly the "
      "objects answering true to GuiValueIsSettable() are captured - the promise is the round "
      "trip a preset needs, so an object cannot be captured without also being restorable, by "
      "construction. .preset itself is not settable, so no preset captures a preset; the scalar "
      "controls .slider, .i, .f, .dial, .incdec and .t hold the round trip since their #846 "
      "migration and are captured, while .b stays out - its value is a consume-on-read press, an "
      "event no recall could meaningfully restore; objects nested in subpatchers participate "
      "- a preset belongs to the patch, not to one level of it. Recall writes through the "
      "ordinary control-thread message path - each object receives the exact string its "
      "GetGuiValue() produced, as a list on inlet 0, so every clamp, side effect and outlet "
      "send its own handler performs still happens - and announces the slot out outlet 0 only "
      "after the patch is in it; a store announces out outlet 1. Entries are guarded against "
      "storage-ID reuse: a slot value is pushed only while its ID still names a live object of "
      "the captured type. The slots persist through DumpState / RestoreState unconditionally, "
      "plus the active marker - Max's preset is a UI object whose contents save with the patch - "
      "with each entry naming its object by the dump's record order (the rank of its ID among "
      "the live objects), which is exactly the fresh numbering ParseJSON hands out, so a patch "
      "whose IDs went sparse through deletes still recalls correctly after a save and a load. "
      "Loading does not recall: a patch that wants to come up in slot 0 wires .loadbang into "
      "the inlet. Everything the object does is control-thread work - store walks the patch, "
      "recall runs whole subgraphs - so a message that physically arrives on the audio callback "
      "is dropped whole before any work, at the cost of one thread-local load; Calculate() "
      "itself does nothing at all. The GUI value is the active slot number, -1 when none - what "
      "a host highlights in a row of preset dots - and is read-only. Not ported: interpolated "
      "recall (Max's preset is instant; interpolation belongs to pattrstorage, on the excluded "
      "pattr system - morphing between targets is already a patch, .xyslider into .nodes), the "
      "pattrstorage client outlet, and every mouse, bubble and colour concern.");
  ADD_CATEGORY(pCategory::GUI);
  INLET_DOC(0, "control", kInDoc, "slot 0 to slots-1");
  OUTLET_DOC(0, "recalled", kRecalledOutDoc, "0 to slots-1");
  OUTLET_DOC(1, "stored", kStoredOutDoc, "0 to slots-1");
  PARAM_DOC(
      "slots", "32",
      "How many numbered slots the bank holds, clamped to 1-1024 and 32 by default. A creation "
      "parameter because the bank is shaped once, on the control thread, before the object is "
      "wired or published: a live SetParams therefore replaces the object through the graph "
      "swap - and the replacement starts with empty slots, exactly as a re-parsed .function "
      "starts with no points. Max's preset holds as many presets as its box has room to draw; "
      "headless, the room is this number. The slot contents are not parameters - they are "
      "contents, and they ride the saved patch through the object's state instead, "
      "unconditionally.",
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
  // exercise the drop path at all.
  if (parent == nullptr) return thread == YSE::T_DSP;
  return static_cast<patcherImplementation*>(parent)->CallingThread(thread) == YSE::T_DSP;
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
  scratch.clear();
  const unsigned int count = p->Objects();
  for (unsigned int i = 0; i < count; i++) {
    YSE::pHandle* handle = p->GetHandleFromList(i);
    if (handle == nullptr) continue;
    if (!handle->GuiValueIsSettable()) continue;
    std::string value = handle->GetGuiValue();
    // An empty string is not a message an inlet can take back, so the object
    // is left out of the slot and a recall leaves it untouched.
    if (value.empty()) continue;
    Entry entry;
    entry.id = handle->GetID();
    entry.type = handle->Type();
    entry.value = std::move(value);
    scratch.push_back(std::move(entry));
  }

  {
    storeGuard guard(busy);
    if (!guard.Held()) return;
    // Swap rather than copy: the old contents land in the staging buffer and
    // are released by the next operation — on the control thread, which the
    // drop rule above guarantees this is.
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

  for (const Entry& entry : scratch) {
    YSE::pHandle* handle = p->GetHandleFromID(entry.id);
    if (handle == nullptr) continue;
    // Storage IDs are reused after a delete (issue #733): without this check
    // a recall could write one control's state into whatever object inherited
    // its number. A stale entry is skipped; the rest of the slot still lands.
    if (entry.type != handle->Type()) continue;
    if (!handle->GuiValueIsSettable()) continue;
    // The ordinary control-thread message path — the same call a host makes,
    // so every clamp, side effect and outlet send the object's own inlet
    // handler performs still happens. Never a poke into its fields.
    handle->SetListData(0, entry.value);
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
    Store(ExprToInt(slot), thread);
    return true;
  }

  if (TokenIs(word, length, "recall", 6)) {
    std::size_t cursor = argOffset;
    float slot = 0.f;
    if (!NextNumber(text, total, cursor, slot)) return true;
    if (!(slot >= 0.f)) return true;
    Recall(ExprToInt(slot), thread);
    return true;
  }

  if (TokenIs(word, length, "clearall", 8)) {
    ClearAll();
    return true;
  }

  if (TokenIs(word, length, "clear", 5)) {
    std::size_t cursor = argOffset;
    float slot = 0.f;
    if (NextNumber(text, total, cursor, slot)) {
      if (!(slot >= 0.f)) return true;
      ClearSlot(ExprToInt(slot));
    } else {
      // Bare `clear` empties the active slot — the parallel of the bang.
      ClearSlot(active.load(std::memory_order_relaxed));
    }
    return true;
  }

  return false;
}

// ─── inlets ───────────────────────────────────────────────────────────────────

BANG_IN(BangIn) {
  if (inlet != 0) return;
  // Everything this object does is control-thread work — see the class
  // comment. Decided first, at the cost of one thread-local load, so the
  // refusal path does nothing at all.
  if (OnAudioThread(thread)) return;
  // The resync: recall the active slot again. Recall's range check answers
  // the no-active case (-1) with silence.
  Recall(active.load(std::memory_order_relaxed), thread);
}

INT_IN(IntIn) {
  if (inlet != 0) return;
  if (OnAudioThread(thread)) return;
  // Max: "recalls the preset whose number is received".
  Recall(value, thread);
}

FLOAT_IN(FloatIn) {
  if (inlet != 0) return;
  if (OnAudioThread(thread)) return;
  // Truncated the way every numeric control truncates. A NaN names no slot.
  if (std::isnan(value)) return;
  Recall(ExprToInt(value), thread);
}

LIST_IN(ListIn) {
  // Registered on inlet 0 only, but routed anyway: a later inlet must never
  // start driving the bank because someone added a handler above.
  if (inlet != 0) return;
  if (OnAudioThread(thread)) return;

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
  Recall(ExprToInt(slot), thread);
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

void gPreset::DumpState(nlohmann::json::value_type& json) {
  auto* p = static_cast<patcherImplementation*>(parent);

  // Control thread — patcherImplementation::DumpJSON holds mtx — but the
  // guard is still taken, because a timer-thread recall may land mid-save.
  // The patcher accessors used below take no lock of their own, so calling
  // them from under mtx is fine.
  storeGuard guard(busy);
  if (!guard.Held()) return;

  // Unconditional, `.coll`'s always-rule — but an untouched object writes
  // nothing, so its serialised form is byte for byte what it would be
  // without the hook.
  const int act = active.load(std::memory_order_relaxed);
  bool anySlot = false;
  for (int i = 0; i < capacity && !anySlot; i++) {
    if (!slots[(std::size_t)i].empty()) anySlot = true;
  }
  if (act < 0 && !anySlot) return;

  if (act >= 0) json["active"] = act;

  // A standalone object is never serialised through a patcher and has no
  // live set to rank against; nothing more to write.
  if (!anySlot || p == nullptr) return;

  const unsigned int count = p->Objects();
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
      YSE::pHandle* handle = p->GetHandleFromID(entry.id);
      if (handle == nullptr) continue;
      if (entry.type != handle->Type()) continue;

      // The rank of the ID among the live objects — the dump's own record
      // order ("object 0", "object 1", ...), which is exactly the fresh
      // numbering ParseJSON hands out (issue #730). Spelled as the rank
      // rather than the raw ID so a patch whose IDs went sparse through
      // deletes still recalls correctly after a save and a load.
      unsigned int rank = 0;
      for (unsigned int o = 0; o < count; o++) {
        YSE::pHandle* other = p->GetHandleFromList(o);
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
