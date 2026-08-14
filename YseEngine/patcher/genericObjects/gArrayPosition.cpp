#include "gArrayPosition.h"
#include "../math/gExprEval.h"
#include "../pObjectList.hpp"
#include "../patcherImplementation.h"

#include <cstddef>
#include <cstring>
#include <string>

using namespace YSE::PATCHER;

namespace {

  // Doc strings shared by the two objects, hoisted out of the constructors
  // because they are long enough that a constructor stops being readable with
  // them inline — gArray's arrangement. What differs per object stays with
  // that object below.
  constexpr char kReferenceInletDoc[] =
      "\"array <name>\" is accepted silently when it names the array bound by the first creation "
      "argument, so a patch may wire the array's reference outlet across. It sets nothing — the "
      "binding is the creation argument, resolved on the control thread — and anything else is "
      "refused and counted.";
  constexpr char kIndexParamDoc[] =
      "The initial stored position — where the operation applies before any int has moved it. "
      "Zero-based, exactly as every position the index inlet takes.";
  constexpr char kNameParamDoc[] =
      "The array's shared name, addressed as \"<patcherName>.<name>\" — the sequence an .array "
      "of the same name in this patcher holds. Resolved once, on the control thread, which is "
      "why no message re-points it at run time.";

} // namespace

// ─── the shared base ──────────────────────────────────────────────────────────

#define className gArrayPositionBase

gArrayPositionBase::gArrayPositionBase() : gArrayEndsBase() {
  // The second creation argument, after gArrayEndsBase's name. The clear
  // hook the base registered dispatches to this class's override, so there
  // is nothing to re-register.
  ADD_PARAM(index);
}

// A re-parse must not leave half of the previous configuration standing:
// SetParams("") — and the clear half of every Set() — drops the stored
// position back to 0 along with the name. gArrayAt's rule.
PARM_CLEAR() {
  index.store(0, std::memory_order_relaxed);
  gArrayEndsBase::ClearParams();
}

void gArrayPositionBase::StoreIndex(int value) {
  // Negative is refused whole — the position does not move either, so an
  // operation after the refusal applies where it would have applied before.
  if (value < 0) {
    Refuse();
    return;
  }
  index.store(value, std::memory_order_relaxed);
}

bool gArrayPositionBase::IndexFromFloat(float value, int& out) {
  // Max's float method on an int attribute is "convert to int", .table's
  // precedent. The range test keeps a NaN or an infinity — which ExprToInt
  // folds to 0 — from quietly becoming position 0.
  if (!(value >= 0.f && value < 2147483648.f)) {
    Refuse();
    return false;
  }
  out = ExprToInt(value);
  return true;
}

bool gArrayPositionBase::LoadPosition(std::size_t& out) {
  // A negative stored position — which only a creation argument can plant,
  // the inlet refuses one before storing it — is a refusal, not a miss:
  // negative is malformed, where a miss is a well-formed position the array
  // happens not to have.
  const int at = index.load(std::memory_order_relaxed);
  if (at < 0) {
    Refuse();
    return false;
  }
  out = static_cast<std::size_t>(at);
  return true;
}

// ─── .array.insert ────────────────────────────────────────────────────────────

#undef className
#define className gArrayInsert

gArrayInsert::gArrayInsert() : gArrayPositionBase() {
  // The element inlet, hot; the index inlet and the reference inlet, cold —
  // the Max idiom: position on the right, element on the left triggers. No
  // bang method: an insert has to be told what to insert, the end-writers'
  // rule.
  ADD_IN_0;
  REG_INT_IN(IntIn);
  REG_FLOAT_IN(FloatIn);
  REG_LIST_IN(ListIn);
  ADD_IN_1;
  REG_INT_IN(IntIn);
  REG_FLOAT_IN(FloatIn);
  ADD_IN_2;
  REG_LIST_IN(ListIn);

  // The reference, always list text.
  ADD_OUT_LIST;

  ADD_DESCRIPTION(
      "Adds an element at a position, shifting the rest up — Max's array.insert on the "
      "name-addressed value model .array settled: the array is bound from the creation argument, "
      "\".array.insert <name> [<position>]\", because an array is addressed by name and never "
      "passed down a cord. This is the object a running patch wires a position into — .array's "
      "own \"insert\" needs the index inside the message text. An int, a float or a symbol on "
      "the element inlet is inserted at the stored position as the text that spells it, and a "
      "list lands whole, in the order sent — \"a b c\" at position 1 leaves a b c starting at 1 "
      "— or is refused whole, one counted refusal and nothing changed, when the position is past "
      "the end or the array cannot take all of it. Positions are zero-based and run 0..length "
      "inclusive — inserting at the length appends — never wrapped or clamped, the family's "
      "indexing rule. An int on the index inlet stores the position silently; an insert that "
      "lands emits the array's reference, \"array <name>\", so the family chains. The insert is "
      "applied entirely under one hold of the store's guard. .array.remove's exact inverse.");
  ADD_CATEGORY(pCategory::GENERIC);
  INLET_DOC(0, "element",
            "An int or a float is inserted at the stored position as the text that spells it; a "
            "symbol is inserted as itself; a list lands whole, in the order sent, or is refused "
            "whole — one counted refusal and nothing changed — when the position is past the "
            "end, the array cannot take all of it, or an element outruns 64 characters. \"array "
            "<name>\" is refused even naming the bound array: a reference is an identity, not an "
            "element, so a mis-wired reference cord shows up in the refusal count instead of in "
            "the data. Refusals are counted rather than logged, since this inlet may be the "
            "audio thread.",
            "at most 256 elements");
  INLET_DOC(1, "position",
            "An int stores the position the next insert applies at, silently — the cold half of "
            "the Max idiom: position on the right, element on the left triggers. A float "
            "truncates to an int first. Zero-based, 0..length inclusive at the moment of the "
            "insert — the length appends. A negative or non-finite value is refused and counted, "
            "and the stored position does not move.",
            "0-256");
  INLET_DOC(2, "array reference", kReferenceInletDoc, "");
  OUTLET_DOC(0, "reference",
             "The bound array's reference, \"array <name>\", sent after an insert that landed — "
             "the way an array leaves an object on the value model, so wiring it onward chains "
             "the family: into .array.length it reports the new depth, into .array.remove's "
             "trigger it acts on the array just grown. A refused insert emits nothing, and an "
             "unnamed object stays silent — the write happens, but there is no name to pass on.",
             "");
  PARAM_DOC("name", "",
            std::string(kNameParamDoc) +
                " Empty writes into a private, empty array: the inserts land, but nothing "
                "shares them and no reference leaves.",
            "any identifier");
  PARAM_DOC("index", "0", kIndexParamDoc, "0-256");
}

void gArrayInsert::ParamsChanged() {
  RefreshReference();
}

void gArrayInsert::RefreshReference() {
  reference.clear();
  if (arrayName.empty()) return;
  reference.reserve(sizeof(kArrayReferenceWord) + arrayName.size());
  reference += kArrayReferenceWord;
  reference += ' ';
  reference += arrayName;
}

INT_IN(IntIn) {
  if (inlet == 1) {
    StoreIndex(value);
    return;
  }
  pending.Clear();
  // AddInt cannot refuse a first atom, but the check keeps a formatting
  // failure counted rather than silently applying an empty insert.
  if (!pending.AddInt(value)) {
    Refuse();
    return;
  }
  Apply(thread);
}

FLOAT_IN(FloatIn) {
  if (inlet == 1) {
    int position = 0;
    if (IndexFromFloat(value, position)) StoreIndex(position);
    return;
  }
  // A non-finite float has no spelling that reads back — ExprFormatValue
  // renders it as "0." — and storing that would be silent corruption.
  // Refused, with the negated range test so a NaN takes this branch too.
  if (!(value >= -3.402823466e38f && value <= 3.402823466e38f)) {
    Refuse();
    return;
  }
  pending.Clear();
  if (!pending.AddFloat(value)) {
    Refuse();
    return;
  }
  Apply(thread);
}

LIST_IN(ListIn) {
  if (inlet == 2) {
    // The reference inlet acknowledges the array it is already bound to and
    // nothing more — the binding is the creation argument, resolved on the
    // control thread, so there is nothing to set. gDictSlice's inlet rule.
    if (!ArrayReferenceNames(value, arrayName)) Refuse();
    return;
  }

  // The bound array's reference on the element inlet is a mis-wired cord,
  // not data: a reference is an identity, not an element, so it is refused
  // and counted rather than inserting the words "array <name>" into the
  // sequence. Only the bound name can be recognised at all — the bounded
  // compare is the whole of what a message path may do with a name — so any
  // other list is simply atoms.
  if (ArrayReferenceNames(value, arrayName)) {
    Refuse();
    return;
  }

  // Every atom one element, inserted whole or refused whole — see the class
  // notes. AddTokens refusing means more atoms or characters than a list
  // carries, which no array of 256 one-atom elements can take either.
  pending.Clear();
  if (pending.AddTokens(value) != 0 || pending.Empty()) {
    Refuse();
    return;
  }
  Apply(thread);
}

void gArrayInsert::Apply(YSE::THREAD thread) {
  std::size_t at = 0;
  if (!LoadPosition(at)) return;

  const std::size_t count = pending.Size();

  // Validated before the guard is taken, so the insert is whole-or-nothing:
  // an element past its capacity refuses the whole message and changes
  // nothing.
  for (std::size_t i = 0; i < count; i++) {
    if (pending.AtomLength(i) > ELEMENT_CAPACITY) {
      Refuse();
      return;
    }
  }

  {
    const arrayStoreGuard guard(store->busy);
    if (!guard.Held()) {
      Refuse();
      return;
    }
    // The rest of whole-or-nothing, decided against the array as it stands
    // under the guard: a position past the end (past `count` — equal to it
    // appends), or an insert the array cannot take entirely, is refused
    // entirely — one counted refusal, never a clamp and never a fragment.
    if (at > store->count || store->count + count > MAX_ELEMENTS) {
      Refuse();
      return;
    }
    for (std::size_t i = 0; i < count; i++) {
      // Each element goes after the one inserted before it, so a list lands
      // in the order sent. The pre-checks above make a refusal here
      // unreachable — counted all the same rather than silent, since a
      // partial insert is the one thing this object promises not to do.
      if (!ArrayInsertAt(*store, at + i, pending.AtomText(i), pending.AtomLength(i))) {
        Refuse();
        return;
      }
    }
  }

  // Outside the guard on purpose: a send runs the whole downstream graph,
  // which may well act on this same array, and inside the guard that access
  // would be the one thing the try-lock drops. An unnamed object has no name
  // to pass on — the write happened, the announcement is simply empty.
  if (reference.empty()) return;
  outputs[0].SendList(reference, thread);
}

// ─── .array.remove ────────────────────────────────────────────────────────────

#undef className
#define className gArrayRemove

gArrayRemove::gArrayRemove() : gArrayPositionBase() {
  // The position inlet, hot, and the reference inlet, cold — gArrayAt's
  // shape: the position arrives on the trigger inlet itself, because naming
  // the position is the ask.
  ADD_IN_0;
  REG_BANG_IN(BangIn);
  REG_INT_IN(IntIn);
  REG_FLOAT_IN(FloatIn);
  REG_LIST_IN(ListIn);
  ADD_IN_1;
  REG_LIST_IN(ListIn);

  // ANY on the element outlet: what leaves it is an int, a float or a symbol
  // by the departing element's spelling. The miss outlet is always a bang.
  ADD_OUT_ANY;
  ADD_OUT_BANG;

  // The allocation the send path would otherwise need, taken here on the
  // control thread.
  AtomList::ReserveRender(emitScratch);

  ADD_DESCRIPTION(
      "Drops the element at a position and outputs it — Max's array.remove on the name-addressed "
      "value model .array settled: the array is bound from the creation argument, "
      "\".array.remove <name> [<position>]\", because an array is addressed by name and never "
      "passed down a cord. This is the object a running patch wires a position into — .array's "
      "own \"delete\" needs the index inside the message text, and discards silently where this "
      "emits the departing element, typed the way the patcher spells it. An int removes at that "
      "position and stores it, a bang removes at the stored position, and every element behind "
      "the gap moves down one — the read and the erase one hold of the store's guard, so the "
      "element that leaves is exactly the one that left the array. A position the array does not "
      "have bangs the miss outlet instead: nothing removed, nothing counted, and never a wrap or "
      "a clamp — the family's indexing rule. The message an .array's reference outlet emits on a "
      "bang removes at the stored position, the family gesture. .array.insert's exact inverse.");
  ADD_CATEGORY(pCategory::GENERIC);
  INLET_DOC(0, "position",
            "An int removes the element at that position and stores the position; a bang removes "
            "at the stored position; a float truncates to an int first — Max's float method. A "
            "position the array does not have bangs the miss outlet instead, and the stored "
            "position still moves: a miss is a property of the array at that moment, not of the "
            "request. \"array <name>\" removes at the stored position when it names the array "
            "bound by the first creation argument — the message an .array's reference outlet "
            "emits on a bang. A negative position, a reference naming anything else, or any "
            "other list is refused and counted rather than logged, since this inlet may be the "
            "audio thread; a multi-position remove is not offered, because delete renumbers and "
            "a list of positions would name different elements after each erase than it did when "
            "it was sent.",
            "0-255");
  INLET_DOC(1, "array reference", kReferenceInletDoc, "");
  OUTLET_DOC(0, "element",
             "The removed element, typed the way the patcher spells it: a numeric element leaves "
             "as an int or a float by its spelling and anything else as a symbol. Read and "
             "removed under one hold of the store's guard, sent after it is released.",
             "");
  OUTLET_DOC(1, "miss",
             "Bang when the ask named a position the array does not have — nothing removed, "
             "nothing counted, kept off the element outlet so a patch editing by position can "
             "see the edit did not land. An empty or unnamed (private) array misses on every "
             "position. A lost try-lock is a counted refusal instead: the array's state is "
             "unknown, so neither outlet fires.",
             "");
  PARAM_DOC("name", "",
            std::string(kNameParamDoc) +
                " Empty reads a private, empty array: every ask bangs the miss outlet.",
            "any identifier");
  PARAM_DOC("index", "0", kIndexParamDoc, "0-255");
}

BANG_IN(BangIn) {
  (void)inlet;
  RemoveAtIndex(thread);
}

INT_IN(IntIn) {
  (void)inlet;
  // Negative is refused whole — the position does not move either, so a bang
  // after the refusal removes what it would have removed before it.
  if (value < 0) {
    Refuse();
    return;
  }
  index.store(value, std::memory_order_relaxed);
  Remove(static_cast<std::size_t>(value), thread);
}

FLOAT_IN(FloatIn) {
  int position = 0;
  if (IndexFromFloat(value, position)) IntIn(position, inlet, thread);
}

LIST_IN(ListIn) {
  if (inlet == 1) {
    // The reference inlet acknowledges the array it is already bound to and
    // nothing more — gDictSlice's inlet rule, exactly as on the insert.
    if (!ArrayReferenceNames(value, arrayName)) Refuse();
    return;
  }

  // The array's reference removes at the stored position — the message its
  // .array emits on a bang, so wiring that outlet here gives the family's
  // gesture. Anything else, including a reference naming an array this
  // object is not bound to, is refused: resolving an unrecognised name means
  // the registry's mutex, and this may be the audio thread.
  if (ArrayReferenceNames(value, arrayName)) {
    RemoveAtIndex(thread);
    return;
  }
  Refuse();
}

void gArrayRemove::RemoveAtIndex(YSE::THREAD thread) {
  std::size_t position = 0;
  if (!LoadPosition(position)) return;
  Remove(position, thread);
}

void gArrayRemove::Remove(std::size_t position, YSE::THREAD thread) {
  bool miss = false;
  {
    const arrayStoreGuard guard(store->busy);
    if (!guard.Held()) {
      Refuse();
      return;
    }
    if (position >= store->count) {
      miss = true;
    } else {
      // The read and the erase under one hold, so the element that leaves is
      // exactly the one that left the array — no other thread can renumber
      // between the copy and the gap closing.
      const std::string& element = store->elements[position];
      fetchedLength = element.size();
      std::memcpy(fetched, element.data(), fetchedLength);
      fetched[fetchedLength] = '\0';
      ArrayEraseAt(*store, position);
    }
  }

  // Outside the guard on purpose: a send runs the whole downstream graph,
  // which may well act on this same array, and inside the guard that access
  // would be the one thing the try-lock drops.
  if (miss) {
    outputs[1].SendBang(thread);
    return;
  }
  // Through SendAtom, so the element leaves as the int, float or symbol it
  // spells — the patcher's transport rule.
  SendAtom(outputs[0], fetched, fetchedLength, emitScratch, thread);
}
