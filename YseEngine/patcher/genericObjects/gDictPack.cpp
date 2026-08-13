#include "gDictPack.h"
#include "../../implementations/logImplementation.h"
#include "../math/gExprEval.h"
#include "../pListArgs.h"
#include "../pObjectList.hpp"
#include "../patcherImplementation.h"

#include <cstring>
#include <string>

using namespace YSE::PATCHER;
#define className gDictPack

namespace {

  constexpr std::size_t kReferenceLength = sizeof(kDictReferenceWord) - 1;
  constexpr std::size_t kSetWordLength = 3;

  // An int as text, through the patcher's one int formatter rather than
  // std::to_string. Control-thread only (the construction-time logs).
  std::string IntText(int value) {
    char digits[YSE::PATCHER::FORMAT_INT_WIDTH];
    const std::size_t written = YSE::PATCHER::WriteInt(value, digits);
    return std::string(digits, written);
  }

  // Doc strings, hoisted out of CONSTRUCT() because they are long enough that
  // the constructor stops being readable with them inline — gDict's
  // arrangement. Kept at namespace scope so ShapePorts can re-document the
  // ports it rebuilds on a re-parse.
  constexpr char kHotInletDoc[] =
      "The hot inlet, and the first key path's value. A value arriving here is stored in its "
      "slot, the whole dictionary is packed — the bound dictionary replaced with one entry per "
      "key path, current values — and the reference \"dictionary <name>\" is sent. A whole list "
      "is one value, not a spread: a dictionary value is list text, so \"0.2 0.5 0.9\" here is "
      "the array this key holds. A bang packs and sends without storing, and \"set <value>\" "
      "performs the same store while suppressing only the release, which is how a patch loads "
      "this slot and chooses when to send. \"dictionary <name>\" naming the bound dictionary "
      "also packs and sends — the message a .dict's reference outlet emits — while a reference "
      "naming anything else is refused and counted: an identity is not a value, and resolving "
      "an unrecognised name would mean the registry's mutex on whatever thread this message "
      "arrived on. A value past 256 characters is refused whole and counted, the slot keeping "
      "what it had.";
  constexpr char kColdInletDoc[] =
      "Cold: a value arriving here is stored in this inlet's key path slot and nothing is sent "
      "— load the right-hand values first, then let the leftmost inlet carry the finished "
      "dictionary out. A whole list is one value, not a spread, and \"set <value>\" is accepted "
      "here too, though it means the same as the plain message since this inlet never releases. "
      "\"dictionary <name>\" naming the bound dictionary is acknowledged and stores nothing; a "
      "reference naming anything else is refused and counted. A value past 256 characters is "
      "refused whole and counted, the slot keeping what it had.";
  constexpr char kTriggerInletDoc[] =
      "With no key-path arguments there is nothing to store: a bang, or \"dictionary <name>\" "
      "naming the bound dictionary, packs the empty dictionary — replacing whatever the bound "
      "store held — and sends its reference. Any other message is refused and counted, since "
      "this inlet may be the audio thread.";
  constexpr char kOutletDoc[] =
      "\"dictionary <name>\" after a pack — the reference the dict.* family binds, carrying the "
      "local name so the receiving object prefixes it with its own patcher's name. Sent "
      "whenever the leftmost inlet is written, banged, or handed the bound dictionary's "
      "reference. Silent for an unnamed dictionary, which has no name to pass on — the pack "
      "still lands in the private store.";

} // namespace

CONSTRUCT() {
  ADD_PARAM(dictName);
  ADD_PARAM(keyArgs);
  REG_PARM_CLEAR;
  REG_PARM_PARSE;

  // A private, empty store to start with, so `store` is never null and no
  // message handler needs a null check. Rebind() trades it for a shared one
  // as soon as there is both a name and a patcher to prefix it with.
  Rebind();
  RefreshReference();

  // Every port is built by ShapePorts(), because the argument list *is* the
  // port list — gPack's arrangement.
  ShapePorts();

  ADD_DESCRIPTION(
      "Builds a dictionary from a list of named inlets — Max's dict.pack on the name-addressed "
      "value model .dict settled: the dictionary is bound from the first creation argument, and "
      "every argument after it is a key path declaring one inlet — \".dict.pack out freq gain "
      "pan\" — because a dictionary is addressed by name and never passed down a cord. The "
      "dictionary counterpart of .pack, and its hot/cold rule exactly: only the leftmost inlet "
      "releases — writing any other inlet stores its key's value and stays quiet — so a patch "
      "loads the right-hand values first and lets the leftmost carry the finished dictionary "
      "out. A trigger — a value on inlet 0, a bang, or the bound dictionary's \"dictionary "
      "<name>\" reference — replaces the bound dictionary whole with one entry per key path in "
      "argument order, a slot never written packing its path with an empty value, and sends the "
      "reference out the outlet, consistent with .dict. \"set <value>\" stores without "
      "releasing. A whole list is one value rather than a spread, since a dictionary value is "
      "list text; paths nest with \"::\", so the packed dictionary is as structured as its keys "
      "spell. A value past 256 characters is refused whole and counted rather than truncated, "
      "and a reference naming an unbound dictionary is refused rather than resolved, a registry "
      "lookup being a mutex on whatever thread the message arrived on.");
  ADD_CATEGORY(pCategory::GENERIC);
  PARAM_DOC("name", "",
            "The packed dictionary's shared name — the first creation argument, addressed as "
            "\"<patcherName>.<name>\", the dictionary a .dict of the same name in this patcher "
            "holds. Resolved once, on the control thread, which is why no message re-points it "
            "at run time. The result goes into a bound dictionary rather than a new anonymous "
            "one because there is no way to hand a fresh dictionary's identity down a cord. "
            "Empty packs into a private store and sends nothing.",
            "any identifier");
  PARAM_DOC("keys", "",
            "One key path per inlet, in order — the shape of the packed dictionary. Paths nest "
            "with \"::\", Max's own separator, so \"voice::1::freq\" is one entry three levels "
            "deep. At most 256 paths of at most 128 characters each; a path that does not fit "
            "declares no inlet and is logged, parameter parsing being control-thread only. A "
            "duplicate path is two inlets writing one entry, and the later inlet wins. With no "
            "paths the object is a bare trigger that packs the empty dictionary.",
            "key paths");
}

// A re-parse must not leave half of the previous configuration standing:
// Parameters::Set() calls this before parsing and returns early on an empty
// argument string, so this is what makes SetParams("") a real reset —
// dropping the name and the keys, going back to a private store and the bare
// trigger shape. gDict's rule, plus gPack's for the ports.
PARM_CLEAR() {
  dictName.clear();
  keyArgs.clear();
  Rebind();
  RefreshReference();
  ShapePorts();
}

PARM_PARSE() {
  Rebind();
  RefreshReference();
  ShapePorts();
}

// `parent` is a patcherImplementation by construction (the patcher hands
// itself to every object via SetParent); the cast mirrors gDict's.
void gDictPack::SetParent(pObject* newParent) {
  pObject::SetParent(newParent);
  Rebind();
}

void gDictPack::RefreshBinding() {
  Rebind();
}

void gDictPack::Rebind() {
  // No name, or no patcher to prefix it with, means no address — and no
  // address means a private, empty dictionary. See gDict.h for why an
  // unnamed object does not pool on "<patcherName>.".
  std::string address;
  if (!dictName.empty() && parent != nullptr) {
    auto* p = static_cast<patcherImplementation*>(parent);
    address = p->Name() + "." + dictName;
  }

  // Unchanged binding: keep the store. A live SetParams that leaves the name
  // alone must not re-anchor it, and neither must the second Rebind() a
  // Set() makes (clear, then parse).
  if (store != nullptr && address == boundAddress) return;

  bool created = false;
  store = address.empty() ? std::make_shared<dictStore>()
                          : AcquireNamedStore<dictStore>(address, created);
  boundAddress = address;
}

void gDictPack::RefreshReference() {
  reference.clear();
  if (dictName.empty()) return;
  reference.reserve(kReferenceLength + 1 + dictName.size());
  reference += kDictReferenceWord;
  reference += ' ';
  reference += dictName;
}

// ─── the creation arguments ───────────────────────────────────────────────────

void gDictPack::ShapePorts() {
  // Rebuilt rather than resized: the key count *is* the inlet count, so the
  // ports, the key list and the value slots all have to agree. Safe because
  // every caller runs before the object is wired or published — the
  // constructor, and the two parameter callbacks, which
  // patcherImplementation::CreateObjectUnlocked runs before AssignGraphIds.
  keys.clear();
  slots.clear();

  // Parameters::Set splits on single spaces, so a run of them yields empty
  // tokens; an empty token declares no key.
  int requested = 0;
  for (const std::string& token : keyArgs) {
    if (!token.empty()) requested++;
  }

  for (const std::string& token : keyArgs) {
    if (token.empty()) continue;
    if (keys.size() >= MAX_KEYS) break;
    // Refused, never truncated: a clipped path would silently pack a key
    // nobody spelled. The skip is logged below — this is the control
    // thread, the one place a refusal can be *said*.
    if (token.size() > KEY_CAPACITY) continue;
    keys.push_back(token);
  }
  slots.resize(keys.size());

  if ((int)keys.size() != requested) {
    INTERNAL::LogImpl().emit(E_WARNING, std::string("patcher: ") + Type() + " asked for " +
                                            IntText(requested) + " key paths; only " +
                                            IntText((int)keys.size()) + " fitted (at most " +
                                            IntText((int)MAX_KEYS) + " paths of at most " +
                                            IntText((int)KEY_CAPACITY) + " characters each)");
  }

  inputs.clear();
  outputs.clear();

  // One inlet per key path — or, with no paths at all, a bare trigger
  // inlet. Inlet 0 is the object's active one, as it is everywhere else in
  // the patcher; the rest are ordinary control inlets.
  const int inletCount = keys.empty() ? 1 : (int)keys.size();
  for (int i = 0; i < inletCount; i++) {
    if (i == 0) {
      ADD_IN_0;
      // Bang goes on the releasing inlet only, which is where Max documents
      // it — gPack's rule, and the .zl / .combine discipline of registering
      // handlers only where they mean something.
      REG_BANG_IN(BangIn);
      REG_LIST_IN(ListIn);
      if (!keys.empty()) {
        REG_INT_IN(IntIn);
        REG_FLOAT_IN(FloatIn);
      }
    } else {
      inputs.emplace_back(this, false, i);
      REG_INT_IN(IntIn);
      REG_FLOAT_IN(FloatIn);
      REG_LIST_IN(ListIn);
    }
  }

  ADD_OUT_LIST;

  ApplyDocs();
}

void gDictPack::ApplyDocs() {
  if (keys.empty()) {
    inputs[0].SetDoc("pack", kTriggerInletDoc, "");
  } else {
    for (std::size_t i = 0; i < keys.size(); i++) {
      // The label is the key path itself — what this inlet writes is the
      // one thing worth saying about it.
      inputs[i].SetDoc(keys[i], i == 0 ? kHotInletDoc : kColdInletDoc, "any");
    }
  }
  outputs[0].SetDoc("reference", kOutletDoc, "");
}

// ─── the guard ────────────────────────────────────────────────────────────────

bool gDictPack::Enter() {
  if (busy.exchange(true, std::memory_order_acq_rel)) {
    // Another thread is mid-message, or this object's outlet is wired back
    // into an inlet and the send has come round again. Counted rather than
    // spun on: this is a path the audio callback takes.
    Refuse();
    return false;
  }
  return true;
}

void gDictPack::Leave() {
  busy.store(false, std::memory_order_release);
}

// ─── the pack ─────────────────────────────────────────────────────────────────

void gDictPack::Take(const char* text, std::size_t length, int inlet, bool emit,
                     YSE::THREAD thread) {
  // A value where no key inlet exists — the bare-trigger shape handed a
  // list. Nothing to store it in, so it is refused and counted rather than
  // silently swallowed.
  if (inlet < 0 || inlet >= (int)slots.size()) {
    Refuse();
    return;
  }
  if (!Enter()) return;

  if (length > VALUE_CAPACITY) {
    // Refused whole rather than truncated: a prefix nobody sent is not the
    // value the patch chose. The slot keeps what it had.
    Refuse();
  } else {
    packSlot& slot = slots[(std::size_t)inlet];
    std::memcpy(slot.text, text, length);
    slot.text[length] = 0;
    slot.length = length;
  }

  // The store is settled before the release, so a patch that loops the
  // outlet back into an inlet finds the dictionary it is being handed — and
  // finds the guard taken, which is what stops it recursing on the audio
  // thread.
  if (emit) Pack(thread);

  Leave();
}

void gDictPack::Pack(YSE::THREAD thread) {
  // Called with the object's guard held. The bound store is replaced whole
  // under its own guard alone — the object's busy flag is not a dictStore
  // guard, so no two dict guards ever nest — and the guard is released
  // before the send, which runs the whole downstream graph and may well
  // store into this same dictionary.
  {
    const dictStoreGuard guard(store->busy);
    if (!guard.Held()) {
      Refuse();
      return;
    }
    for (std::size_t i = 0; i < store->count; i++) {
      store->entries[i].key.clear();
      store->entries[i].value.clear();
    }
    store->count = 0;
    for (std::size_t i = 0; i < keys.size(); i++) {
      // Cannot fail by construction — keys are validated at parse time and
      // a slot is bounded by VALUE_CAPACITY — but a refusal is counted
      // rather than assumed away.
      if (!DictStoreAt(*store, keys[i].data(), keys[i].size(), slots[i].text, slots[i].length)) {
        Refuse();
      }
    }
  }

  // An unnamed dictionary has no name to pass on — gDict's rule for an
  // unnamed reference. The pack itself still landed in the private store.
  if (reference.empty()) return;
  outputs[0].SendList(reference, thread);
}

// ─── the inlets ───────────────────────────────────────────────────────────────

BANG_IN(BangIn) {
  // Max: bang packs and outputs the dictionary. Stores nothing; registered
  // on the releasing inlet only.
  if (inlet != 0) return;
  if (!Enter()) return;
  Pack(thread);
  Leave();
}

INT_IN(IntIn) {
  // Formatted through the patcher's one spelling of a number into a stack
  // buffer, then stored through the same path a list takes — one storage
  // rule.
  char text[kExprValueTextMax];
  const int written = ExprFormatValue(ExprValue::Int(value), text, kExprValueTextMax);
  if (written <= 0) return;
  Take(text, (std::size_t)written, inlet, inlet == 0, thread);
}

FLOAT_IN(FloatIn) {
  char text[kExprValueTextMax];
  const int written = ExprFormatValue(ExprValue::Float(value), text, kExprValueTextMax);
  if (written <= 0) return;
  Take(text, (std::size_t)written, inlet, inlet == 0, thread);
}

LIST_IN(ListIn) {
  std::size_t at = 0;
  if (MatchWord(value, "set", kSetWordLength, at)) {
    // Exactly the store the same message without the word would have
    // performed, suppressing only the release — one storage rule, so the
    // two paths cannot drift apart. gPack's reading of Max's "set". A bare
    // "set" with nothing after it is not this message (MatchWord requires a
    // separator) and falls through as a value.
    Take(value.c_str() + at, value.size() - at, inlet, false, thread);
    return;
  }

  if (DictReferenceNames(value, dictName)) {
    // The bound dictionary's own reference — the message its .dict emits on
    // a bang. A trigger on the hot inlet, and an acknowledgment — nothing
    // stored, nothing sent — on a cold one, so a reference wired across is
    // not counted as an error.
    if (inlet == 0) {
      if (!Enter()) return;
      Pack(thread);
      Leave();
    }
    return;
  }

  if (MatchWord(value, kDictReferenceWord, kReferenceLength, at)) {
    // A reference naming a dictionary this object is not bound to. Max
    // embeds it as a subdictionary; that needs a registry resolve — a mutex
    // — on whatever thread this message arrived on, so it is refused and
    // counted rather than resolved, and not stored either: an identity is
    // not a value, and burying a mis-wire in the data would hide it.
    Refuse();
    return;
  }

  Take(value.c_str(), value.size(), inlet, inlet == 0, thread);
}

// ─── diagnostics ──────────────────────────────────────────────────────────────

std::string gDictPack::KeyAt(std::size_t index) const {
  if (index >= keys.size()) return std::string();
  return keys[index];
}

std::string gDictPack::ValueAt(std::size_t index) const {
  if (index >= slots.size()) return std::string();
  return std::string(slots[index].text, slots[index].length);
}

std::string gDictPack::Lookup(const std::string& path) const {
  const dictStoreGuard guard(store->busy);
  if (!guard.Held()) return std::string();
  const std::size_t at = DictFind(*store, path.c_str(), path.size());
  if (at >= store->count) return std::string();
  return store->entries[at].value;
}
