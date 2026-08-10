#include "gPack.h"
#include "../../implementations/logImplementation.h"
#include "../math/gExprEval.h"
#include "../pAtomList.h"
#include "../pListArgs.h"
#include "../pObjectList.hpp"
#include "../pSelector.h"
#include <cstddef>
#include <string>

using namespace YSE::PATCHER;

#define className gPackBase

namespace {

  // An int as text, through the patcher's one int formatter rather than
  // std::to_string. Control-thread only (the construction-time logs).
  std::string IntText(int value) {
    char digits[YSE::PATCHER::FORMAT_INT_WIDTH];
    const std::size_t written = YSE::PATCHER::WriteInt(value, digits);
    return std::string(digits, written);
  }

} // namespace

gPackBase::gPackBase(const char* name, packTrigger policy)
  : pObject(false), typeName(name), trigger(policy) {
  // Every port is built by ShapePorts(), because the argument list *is* the
  // port list. The clear callback is what makes `SetParams("")` return the
  // object to Max's no-argument shape rather than leaving the previous slot
  // count in place.
  REG_PARM_CLEAR;
  REG_PARM_PARSE;

  ADD_PARAM(creationArgs);

  // Max: "If no arguments provided, the object creates two inlets with initial
  // values of 0 (int)." Also the shape ClearParams() restores.
  ShapePorts();

  // The one allocation the object makes outside its two lists, and it happens
  // here rather than on an arrival: the list is rendered into storage that is
  // already long enough, on whichever thread the value came in on — routinely
  // the audio callback.
  AtomList::ReserveRender(render);

  ADD_CATEGORY(pCategory::GENERIC);
}

void gPackBase::Document(const char* summary, const char* leftInlet, const char* otherInlets,
                         const char* outlet, const char* paramDoc) {
  ADD_DESCRIPTION(summary);

  // Kept, not only applied: a re-parse rebuilds the ports and has to document
  // the new ones without knowing which family member it belongs to.
  leftInletDoc = leftInlet;
  otherInletDoc = otherInlets;
  outletDoc = outlet;
  ApplyDocs();

  PARAM_DOC("elements", "0 0", paramDoc,
            "one argument per inlet, 1-256; int / float / symbol by spelling");
}

void gPackBase::ApplyDocs() {
  // Called from the base constructor's ShapePorts too, when the derived
  // constructor has not run yet and there is nothing to apply.
  if (leftInletDoc == nullptr) return;

  for (int i = 0; i < (int)inputs.size(); i++)
    inputs[(std::size_t)i].SetDoc(InletLabel(i), i == 0 ? leftInletDoc : otherInletDoc, "any");

  if (!outputs.empty()) outputs[0].SetDoc("list", outletDoc, "any");
}

// ─── the creation arguments ─────────────────────────────────────────────────

void gPackBase::ShapePorts() {
  // Rebuilt rather than resized: the slot *count* comes from the arguments, so
  // the ports, the type table and the packed list all have to agree. Safe
  // because every caller runs before the object is wired or published — the
  // constructor, and the two parameter callbacks, which
  // patcherImplementation::CreateObjectUnlocked runs before AssignGraphIds.
  types.clear();
  slots.Clear();
  work.Clear();

  // Parameters::Set splits on single spaces, so a run of them yields empty
  // tokens; an empty token declares no slot.
  int requested = 0;
  for (const std::string& token : creationArgs) {
    if (!token.empty()) requested++;
  }

  int count = requested;
  if (count > MAX_PORTS) count = MAX_PORTS;

  int taken = 0;
  for (const std::string& token : creationArgs) {
    if (token.empty()) continue;
    if (taken >= count) break;

    // The argument is the slot's starting value *and*, by its spelling, the
    // slot's type — Max's "the arguments determine the list format and types of
    // the list elements".
    if (!slots.Add(token)) break;

    float number = 0.f;
    if (!ReadNumericToken(token, number)) {
      types.push_back(packType::SYMBOL);
    } else if (TokenLooksLikeFloat(token)) {
      types.push_back(packType::FLOAT);
    } else {
      types.push_back(packType::INT);
    }
    taken++;
  }

  if (taken != requested) {
    // The control thread, before the object is wired or published, so this is
    // the one place a clamp can be *said* rather than merely observable through
    // PortCount().
    INTERNAL::LogImpl().emit(
        E_WARNING, std::string("patcher: ") + typeName + " asked for " + IntText(requested) +
                       " elements; only " + IntText(taken) + " fitted (at most " +
                       IntText(MAX_PORTS) + " elements spanning " +
                       IntText((int)AtomList::TEXT_CAPACITY) + " characters between them)");
  }

  if (taken == 0) {
    // Max's default object, and the fallback for an argument list where not
    // even the first element fitted: "two inlets with initial values of 0
    // (int)". Rebuilt from scratch so a partial first pass cannot leak in.
    slots.Clear();
    for (int i = 0; i < DEFAULT_PORTS; i++) {
      slots.AddInt(0);
      types.push_back(packType::INT);
    }
  }

  inputs.clear();
  outputs.clear();

  for (int i = 0; i < (int)types.size(); i++) {
    // Inlet 0 is the object's active one, as it is everywhere else in the
    // patcher; the rest are ordinary control inlets.
    if (i == 0) {
      ADD_IN_0;
      // Max documents bang on the left inlet, and registering it nowhere else
      // keeps GetAcceptedTypes() reporting the real contract — the .zl /
      // .combine discipline.
      REG_BANG_IN(PackBang);
    } else {
      inputs.emplace_back(this, false, i);
    }
    REG_INT_IN(PackInt);
    REG_FLOAT_IN(PackFloat);
    REG_LIST_IN(PackList);
  }

  // ANY rather than LIST: a one-element pack sends the int, float or symbol it
  // spells rather than a list of one, and this patcher does no coercion at an
  // inlet.
  ADD_OUT_ANY;

  ApplyDocs();
}

PARM_CLEAR() {
  // Runs on the control thread before the parameter string is re-read, and is
  // the whole of `SetParams("")`: Parameters::Set returns without calling the
  // parse callback for an empty argument, so this has to leave Max's
  // no-argument object behind rather than one still holding the previous slots.
  creationArgs.clear();
  ShapePorts();
}

PARM_PARSE() {
  ShapePorts();
}

// ─── the slots ──────────────────────────────────────────────────────────────

gPackBase::packType gPackBase::SlotType(int index) const {
  if (index < 0 || index >= (int)types.size()) return packType::SYMBOL;
  return types[(std::size_t)index];
}

std::string gPackBase::Packed() const {
  // Diagnostics only: this builds a string, which is exactly what the object's
  // own release path is written to avoid.
  std::string out;
  AtomList::ReserveRender(out);
  slots.Render(out);
  return out;
}

// ─── the guard ──────────────────────────────────────────────────────────────

bool gPackBase::Enter() {
  if (busy.exchange(true, std::memory_order_acq_rel)) {
    // Another thread is mid-message, or this object is wired back into its own
    // inlet and a release has come round again. Counted rather than spun on:
    // this is a path the audio callback takes.
    CountDrop();
    return false;
  }
  return true;
}

void gPackBase::Leave() {
  busy.store(false, std::memory_order_release);
}

void gPackBase::CountDrop(std::size_t count) {
  dropped.fetch_add((std::uint64_t)count, std::memory_order_relaxed);
}

// ─── storing ────────────────────────────────────────────────────────────────

bool gPackBase::AddCoerced(AtomList& into, int index, const char* token, std::size_t length,
                           bool& refused) {
  float number = 0.f;
  const bool numeric = ReadNumericToken(token, length, number);

  switch (types[(std::size_t)index]) {
  case packType::INT:
  case packType::FLOAT:
    if (!numeric) {
      // Max: "type conversion occurs based on initialization" — and a symbol
      // has nothing to convert. The slot keeps what it had rather than taking
      // a 0 that would read downstream as a value the patch chose.
      refused = true;
      return into.Add(slots.AtomText(index), slots.AtomLength(index));
    }
    // Truncated through the range-checked conversion rather than cast, casting
    // a float outside the int range being undefined.
    return types[(std::size_t)index] == packType::INT ? into.AddInt(ExprToInt(number))
                                                      : into.AddFloat(number);

  case packType::SYMBOL:
  default:
    // Whatever arrived, verbatim. Max errors here; the patcher's transport is
    // text to begin with, so the characters are both the useful reading and the
    // honest one.
    return into.Add(token, length);
  }
}

bool gPackBase::Store(const char* text, std::size_t length, int first) {
  // The backing text of an AtomList is append-only until Clear(), so replacing
  // one atom is a rebuild: every slot written into the scratch list, then one
  // Assign. Bounded, and allocation-free — both lists reserved their text in
  // the constructor. Written into `work` rather than in place so that a store
  // which does not fit leaves the packed list exactly as it was.
  const int count = (int)types.size();
  work.Clear();

  std::size_t i = 0;
  bool refusedByType = false;

  for (int j = 0; j < count; j++) {
    bool wrote = false;

    if (j >= first) {
      // Max spreads a multi-item message from the receiving inlet rightwards,
      // one item per slot — the rule `.bondo` already follows.
      while (i < length && IsSelectorSeparator(text[i]))
        i++;
      if (i < length) {
        const std::size_t begin = i;
        while (i < length && !IsSelectorSeparator(text[i]))
          i++;
        if (!AddCoerced(work, j, text + begin, i - begin, refusedByType)) return false;
        wrote = true;
      }
    }

    // Every slot the message did not reach keeps what it holds: the values the
    // patch's other inlets are carrying are state, not surplus input.
    if (!wrote && !work.Add(slots.AtomText(j), slots.AtomLength(j))) return false;
  }

  slots.Assign(work);

  // Items past the last slot have nowhere to go. Max drops them and so does
  // `.bondo`; counted here so a patch can see that it happened.
  std::size_t surplus = 0;
  while (i < length) {
    while (i < length && IsSelectorSeparator(text[i]))
      i++;
    if (i >= length) break;
    while (i < length && !IsSelectorSeparator(text[i]))
      i++;
    surplus++;
  }
  if (surplus != 0) CountDrop(surplus);
  if (refusedByType) CountDrop();

  return true;
}

void gPackBase::Emit(YSE::THREAD thread) {
  // The family's transport convention: a packed list of one leaves as the int,
  // float or symbol it spells. A `.pack` always holds at least one slot, so
  // SendAtoms' silent case cannot be reached from here.
  SendAtoms(outputs[0], slots, render, thread);
}

void gPackBase::Take(const char* text, std::size_t length, int inlet, bool emit,
                     YSE::THREAD thread) {
  if (inlet < 0 || inlet >= (int)types.size()) return;
  if (!Enter()) return;

  // Refused whole rather than losing its tail, which is where this parts
  // company with `.zl`: the tail of a pack is the values the other inlets are
  // holding, and dropping them would silently rewrite state nobody touched.
  if (!Store(text, length, inlet)) CountDrop();

  // The store is settled before the release, so a patch that loops the outlet
  // back into an inlet finds the list it is being handed — and finds the guard
  // taken, which is what stops it recursing on the audio thread.
  if (emit) Emit(thread);

  Leave();
}

// ─── the inlets ─────────────────────────────────────────────────────────────

BANG_IN(PackBang) {
  // Max: "bang: Output currently stored list." Stores nothing. Registered on
  // the left inlet only, which is where Max documents it.
  if (inlet != 0) return;
  if (!Enter()) return;
  Emit(thread);
  Leave();
}

INT_IN(PackInt) {
  // Formatted and then stored through the same token path a list takes, so
  // there is one storage rule and one place the slot types are enforced. Into a
  // stack buffer, through the patcher's one spelling of a number.
  char text[kExprValueTextMax];
  const int written = ExprFormatValue(ExprValue::Int(value), text, kExprValueTextMax);
  if (written <= 0) return;
  Take(text, (std::size_t)written, inlet, Hot(inlet), thread);
}

FLOAT_IN(PackFloat) {
  char text[kExprValueTextMax];
  const int written = ExprFormatValue(ExprValue::Float(value), text, kExprValueTextMax);
  if (written <= 0) return;
  Take(text, (std::size_t)written, inlet, Hot(inlet), thread);
}

LIST_IN(PackList) {
  std::size_t at = 0;
  if (MatchWord(value, "set", 3, at)) {
    // Max: "Sets the values without causing list output. Although the set
    // message works with any inlet, it is only meaningful in the left inlet."
    // The same store as below, minus the release — one storage rule, so the two
    // paths cannot drift apart. A bare `set` with nothing after it is not this
    // message (MatchWord requires a separator) and falls through as a symbol.
    Take(value.c_str() + at, value.size() - at, inlet, false, thread);
    return;
  }

  Take(value.c_str(), value.size(), inlet, Hot(inlet), thread);
}

#undef className

// ─── .pack ──────────────────────────────────────────────────────────────────

gPack::gPack() : gPackBase(YSE::OBJ::G_PACK, packTrigger::LeftInlet) {
  Document(
      "Builds one list out of values arriving at separate inlets — Max's pack, 'combine items into "
      "an output list'. The patcher could hold a list (.l) but not build one, which blocked every "
      "list-consuming object and the list payloads the named bus accepts, a three-element list "
      "being how sound.<name>.position is addressed; this is the object that turns three cords "
      "carrying three numbers into one cord carrying one list, and .unpack is the way back. There "
      "is one inlet per creation argument, and each argument's spelling decides what its slot "
      "holds as well as what it starts as: a whole integer makes an int slot, a token with a "
      "decimal point or an exponent makes a float slot, and anything else makes a symbol slot. "
      "With no arguments the object is Max's default, two int inlets starting at 0. Only the "
      "leftmost inlet releases the list — writing any other one stores and stays quiet — so a "
      "patch loads the right-hand values first and lets the leftmost carry the finished list out; "
      ".pak is the same object with every inlet hot. The type is enforced on the way in, Max's "
      "'type conversion occurs based on initialization': an int slot truncates a float, a float "
      "slot promotes an int and keeps its decimal point, a symbol slot takes whatever arrives "
      "verbatim, and a number slot handed a symbol keeps the value it had and counts the refusal "
      "rather than storing a 0 that would read downstream as a value the patch chose. A "
      "multi-item message spreads from the inlet that received it rightwards, one item per slot, "
      "and items past the last slot are dropped and counted. A bang releases the list as it "
      "stands without storing, and 'set <message>' performs exactly the store the same message "
      "without the word would have performed while suppressing only the release. The packed list "
      "leaves as list text, or — when the object has a single slot — as the int, float or symbol "
      "that slot spells, since a list of one is not a list and this patcher does no coercion at an "
      "inlet. The list is the bounded pre-allocated storage the whole list family shares: at most "
      "256 slots spanning at most 1024 characters between them, in memory reserved when the object "
      "is built. A store whose result would not fit is refused whole and counted rather than "
      "losing its tail, because the tail here is the values the other inlets are holding rather "
      "than surplus input; the count is a counter rather than a log line because the refusing "
      "thread may be the audio callback, while a creation argument that does not fit is logged, "
      "parameter parsing being control-thread only. Re-typing the creation arguments rebuilds the "
      "object, since the arguments are the inlet count and the slot types. Calculate() does "
      "nothing and no message path allocates, locks or blocks: numbers are rendered into a stack "
      "buffer, the store is a rebuild inside a list reserved at construction, and the release "
      "renders into a buffer reserved at the same time. Two threads writing the same object are "
      "serialised by a single test-and-set guard whose loser is dropped and counted rather than "
      "made to spin, which is also what stops an object wired back into its own inlet from "
      "recursing on the audio thread.",

      "The hot inlet, and the first element of the list. A value arriving here is stored in "
      "element 0 and the whole list is released. A multi-item message spreads across this element "
      "and the ones to its right, one item per element, with anything past the last element "
      "dropped and counted. A bang releases the list as it stands without storing anything, and is "
      "accepted on this inlet only, which is where Max documents it. 'set <message>' performs the "
      "same store as the message without the word and releases nothing, which is how a patch loads "
      "every element and then chooses when to send. What this element accepts is fixed by the "
      "creation argument in its position: an int element truncates a float, a float element "
      "promotes an int, a symbol element takes anything verbatim, and a number element handed a "
      "symbol keeps the value it had and counts the refusal.",

      "Cold: a value arriving here is stored in the element that corresponds to this inlet and "
      "nothing is sent. That is the object's arrangement — load the right-hand elements, then let "
      "the leftmost inlet carry the finished list out. Use .pak instead when every element should "
      "release on arrival. A multi-item message spreads from this element rightwards rather than "
      "being stored whole, and 'set <message>' is accepted here too, though it means the same as "
      "the plain message since this inlet never releases. The element's type comes from the "
      "creation argument in its position and is enforced on every store, exactly as it is on the "
      "leftmost inlet.",

      "The packed list, sent whenever the leftmost inlet is written or banged. Every element goes "
      "out together, in inlet order, as space-separated list text — so an element that has never "
      "been written carries its creation argument rather than a gap. An object with a single "
      "element sends the int, float or symbol that element spells rather than a list of one, so it "
      "reaches the inlets an uncollected value would have reached.",

      "One argument per inlet, and each one is that element's starting value as well as its type. "
      "A token spelled as a whole integer declares an int element, one carrying a decimal point or "
      "an exponent declares a float element, and anything else declares a symbol element whose "
      "starting value is the token itself. With no arguments the object is Max's default: two int "
      "elements starting at 0. At most 256 elements are built, spanning at most 1024 characters "
      "between them; an argument list that does not fit is clamped and the clamp is logged.");
}
