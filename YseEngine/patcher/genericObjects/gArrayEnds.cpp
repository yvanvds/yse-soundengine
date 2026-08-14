#include "gArrayEnds.h"
#include "../pObjectList.hpp"
#include "../patcherImplementation.h"

#include <cstddef>
#include <cstring>
#include <string>

using namespace YSE::PATCHER;

namespace {

  constexpr std::size_t kReferenceLength = sizeof(kArrayReferenceWord) - 1;

  // Doc strings shared by the four objects, hoisted out of the constructors
  // because they are long enough that a constructor stops being readable with
  // them inline — gArray's arrangement. What differs per object stays with
  // that object below.
  constexpr char kReferenceInletDoc[] =
      "\"array <name>\" is accepted silently when it names the array bound by the creation "
      "argument, so a patch may wire the array's reference outlet across. It sets nothing — the "
      "binding is the creation argument, resolved on the control thread — and anything else is "
      "refused and counted.";
  constexpr char kWriterOutletDoc[] =
      "The bound array's reference, \"array <name>\", sent after an add that landed — the way an "
      "array leaves an object on the value model, so wiring it onward chains the family: into "
      ".array.length it reports the new depth, into a remover's trigger it acts on the array just "
      "grown. A refused add emits nothing, and an unnamed object stays silent — the write "
      "happens, but there is no name to pass on.";
  constexpr char kEmptyOutletDoc[] =
      "Bang when the trigger found the array empty — the state a patch draining a queue must be "
      "able to see, and its loop's exit condition rather than an error. An unnamed (private) "
      "array is always empty. A lost try-lock is a counted refusal instead: the array's state is "
      "unknown, so neither outlet fires.";

} // namespace

// ─── the shared base ──────────────────────────────────────────────────────────

#define className gArrayEndsBase

gArrayEndsBase::gArrayEndsBase() : pObject(false) {
  ADD_PARAM(arrayName);
  REG_PARM_CLEAR;
  REG_PARM_PARSE;

  // A private, empty store to start with, so `store` is never null and no
  // message handler needs a null check. Rebind() trades it for a shared one
  // as soon as there is both a name and a patcher to prefix it with.
  Rebind();
}

// A re-parse must not leave half of the previous configuration standing:
// Parameters::Set() calls this before parsing and returns early on an empty
// argument string, so this is what makes SetParams("") a real reset —
// dropping the name and going back to a private array. gArray's rule.
PARM_CLEAR() {
  arrayName.clear();
  Rebind();
  ParamsChanged();
}

PARM_PARSE() {
  Rebind();
  ParamsChanged();
}

// `parent` is a patcherImplementation by construction (the patcher hands
// itself to every object via SetParent); the cast mirrors gArray's.
void gArrayEndsBase::SetParent(pObject* newParent) {
  pObject::SetParent(newParent);
  Rebind();
}

void gArrayEndsBase::RefreshBinding() {
  Rebind();
}

void gArrayEndsBase::Rebind() {
  // No name, or no patcher to prefix it with, means no address — and no
  // address means a private, empty array. See gArray.h for why an unnamed
  // object does not pool on "<patcherName>.".
  std::string address;
  if (!arrayName.empty() && parent != nullptr) {
    auto* p = static_cast<patcherImplementation*>(parent);
    address = p->Name() + "." + arrayName;
  }

  // Unchanged binding: keep the store. A live SetParams that leaves the name
  // alone must not re-anchor it, and neither must the second Rebind() a
  // Set() makes (clear, then parse).
  if (store != nullptr && address == boundAddress) return;

  bool created = false;
  store = address.empty() ? std::make_shared<arrayStore>()
                          : AcquireNamedStore<arrayStore>(address, created);
  boundAddress = address;
}

// ─── the adding half ──────────────────────────────────────────────────────────

#undef className
#define className gArrayEndsWriter

gArrayEndsWriter::gArrayEndsWriter(bool front) : gArrayEndsBase(), atFront(front) {
  // The element inlet, hot, and the reference inlet, cold — the family's
  // shape for an object that consumes a reference rather than producing one.
  // No bang method: an add has to be told what to add.
  ADD_IN_0;
  REG_INT_IN(IntIn);
  REG_FLOAT_IN(FloatIn);
  REG_LIST_IN(ListIn);
  ADD_IN_1;
  REG_LIST_IN(ListIn);

  // The reference, always list text.
  ADD_OUT_LIST;
}

void gArrayEndsWriter::ParamsChanged() {
  RefreshReference();
}

void gArrayEndsWriter::RefreshReference() {
  reference.clear();
  if (arrayName.empty()) return;
  reference.reserve(kReferenceLength + 1 + arrayName.size());
  reference += kArrayReferenceWord;
  reference += ' ';
  reference += arrayName;
}

// ─── the writers' messages ────────────────────────────────────────────────────

INT_IN(IntIn) {
  (void)inlet;
  pending.Clear();
  // AddInt cannot refuse a first atom, but the check keeps a formatting
  // failure counted rather than silently applying an empty add.
  if (!pending.AddInt(value)) {
    Refuse();
    return;
  }
  Apply(thread);
}

FLOAT_IN(FloatIn) {
  (void)inlet;
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
  if (inlet == 1) {
    // The reference inlet acknowledges the array it is already bound to and
    // nothing more — the binding is the creation argument, resolved on the
    // control thread, so there is nothing to set. gDictSlice's inlet rule.
    if (!ArrayReferenceNames(value, arrayName)) Refuse();
    return;
  }

  // The bound array's reference on the element inlet is a mis-wired cord,
  // not data: a reference is an identity, not an element, so it is refused
  // and counted rather than pushing the words "array <name>" into the
  // sequence. Only the bound name can be recognised at all — the bounded
  // compare is the whole of what a message path may do with a name — so any
  // other list is simply atoms.
  if (ArrayReferenceNames(value, arrayName)) {
    Refuse();
    return;
  }

  // Every atom one element, added whole or refused whole — see the class
  // notes. AddTokens refusing means more atoms or characters than a list
  // carries, which no array of 256 one-atom elements can take either.
  pending.Clear();
  if (pending.AddTokens(value) != 0 || pending.Empty()) {
    Refuse();
    return;
  }
  Apply(thread);
}

void gArrayEndsWriter::Apply(YSE::THREAD thread) {
  const std::size_t count = pending.Size();

  // Validated before the guard is taken, so the add is whole-or-nothing: an
  // element past its capacity refuses the whole message and changes nothing.
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
    // The other half of whole-or-nothing: an add the array cannot take
    // entirely is refused entirely, one counted refusal, rather than landing
    // a fragment of a list the patch sent as one thing.
    if (store->count + count > MAX_ELEMENTS) {
      Refuse();
      return;
    }
    for (std::size_t i = 0; i < count; i++) {
      // At the front each element goes after the last one inserted, so
      // "unshift a b c" leaves a, b, c in the order they were written; at
      // the back every insert is an append. The pre-checks above make a
      // refusal here unreachable — counted all the same rather than silent,
      // since a partial add is the one thing this object promises not to do.
      const std::size_t at = atFront ? i : store->count;
      if (!ArrayInsertAt(*store, at, pending.AtomText(i), pending.AtomLength(i))) {
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

// ─── the removing half ────────────────────────────────────────────────────────

#undef className
#define className gArrayEndsRemover

gArrayEndsRemover::gArrayEndsRemover(bool front) : gArrayEndsBase(), atFront(front) {
  // The trigger inlet, hot, and the reference inlet, cold — gArrayLength's
  // shape: a removal is asked for with a bang, never addressed, so there is
  // no int or float method anywhere.
  ADD_IN_0;
  REG_BANG_IN(BangIn);
  REG_LIST_IN(ListIn);
  ADD_IN_1;
  REG_LIST_IN(ListIn);

  // ANY on the element outlet: what leaves it is an int, a float or a symbol
  // by the departing element's spelling. The empty outlet is always a bang.
  ADD_OUT_ANY;
  ADD_OUT_BANG;

  // The allocation the send path would otherwise need, taken here on the
  // control thread.
  AtomList::ReserveRender(emitScratch);
}

BANG_IN(BangIn) {
  (void)inlet;
  Remove(thread);
}

LIST_IN(ListIn) {
  if (inlet == 1) {
    // The reference inlet acknowledges the array it is already bound to and
    // nothing more — gDictSlice's inlet rule, exactly as on the writers.
    if (!ArrayReferenceNames(value, arrayName)) Refuse();
    return;
  }

  // The array's reference removes — the message its .array emits on a bang,
  // so wiring that outlet here gives the family's gesture: bang the array,
  // out comes an element. Anything else, including a reference naming an
  // array this object is not bound to, is refused: resolving an unrecognised
  // name means the registry's mutex, and this may be the audio thread.
  if (ArrayReferenceNames(value, arrayName)) {
    Remove(thread);
    return;
  }
  Refuse();
}

void gArrayEndsRemover::Remove(YSE::THREAD thread) {
  bool empty = false;
  {
    const arrayStoreGuard guard(store->busy);
    if (!guard.Held()) {
      Refuse();
      return;
    }
    if (store->count == 0) {
      empty = true;
    } else {
      // The read and the erase under one hold, so the element that leaves is
      // exactly the one that left the array — no other thread can renumber
      // between the copy and the gap closing.
      const std::size_t at = atFront ? 0 : store->count - 1;
      const std::string& element = store->elements[at];
      fetchedLength = element.size();
      std::memcpy(fetched, element.data(), fetchedLength);
      fetched[fetchedLength] = '\0';
      ArrayEraseAt(*store, at);
    }
  }

  // Outside the guard on purpose: a send runs the whole downstream graph,
  // which may well act on this same array, and inside the guard that access
  // would be the one thing the try-lock drops.
  if (empty) {
    outputs[1].SendBang(thread);
    return;
  }
  // Through SendAtom, so the element leaves as the int, float or symbol it
  // spells — the patcher's transport rule.
  SendAtom(outputs[0], fetched, fetchedLength, emitScratch, thread);
}

// ─── .array.push ──────────────────────────────────────────────────────────────

#undef className
#define className gArrayPush

gArrayPush::gArrayPush() : gArrayEndsWriter(false) {
  ADD_DESCRIPTION(
      "Adds an element at the end of an array — Max's array.push on the name-addressed value "
      "model .array settled: the array is bound from the creation argument, \".array.push "
      "<name>\", because an array is addressed by name and never passed down a cord. An int or a "
      "float in the element inlet is appended as the text that spells it, and a list is appended "
      "whole, every atom one element in the order sent — or refused whole, one counted refusal "
      "and nothing changed, when the array cannot take all of it. After an add that lands, the "
      "outlet emits the array's reference, \"array <name>\", so the family chains: into "
      ".array.length it reports the new depth, into .array.pop's trigger it is a stack, into "
      ".array.shift's a queue. With .array.pop this is the stack, and with .array.shift the "
      "event buffer, a generative patch actually uses.");
  ADD_CATEGORY(pCategory::GENERIC);
  INLET_DOC(0, "element",
            "An int or a float is appended as the text that spells it; a symbol is appended as "
            "itself; a list is appended whole, every atom one element in the order sent, or "
            "refused whole — one counted refusal and nothing changed — when the array cannot "
            "take all of it or an element outruns 64 characters. \"array <name>\" is refused "
            "even naming the bound array: a reference is an identity, not an element, so a "
            "mis-wired reference cord shows up in the refusal count instead of in the data. "
            "Refusals are counted rather than logged, since this inlet may be the audio thread.",
            "at most 256 elements");
  INLET_DOC(1, "array reference", kReferenceInletDoc, "");
  OUTLET_DOC(0, "reference", kWriterOutletDoc, "");
  PARAM_DOC("name", "",
            "The array's shared name, addressed as \"<patcherName>.<name>\" — the sequence an "
            ".array of the same name in this patcher holds. Resolved once, on the control "
            "thread, which is why no message re-points it at run time. Empty writes into a "
            "private, empty array: the pushes land, but nothing shares them and no reference "
            "leaves.",
            "any identifier");
}

// ─── .array.pop ───────────────────────────────────────────────────────────────

#undef className
#define className gArrayPop

gArrayPop::gArrayPop() : gArrayEndsRemover(false) {
  ADD_DESCRIPTION(
      "Removes the last element of an array and outputs it — Max's array.pop on the "
      "name-addressed value model .array settled: the array is bound from the creation argument, "
      "\".array.pop <name>\", because an array is addressed by name and never passed down a "
      "cord. A bang removes the last element and emits it out the element outlet, typed the way "
      "the patcher spells it — the read and the erase one hold of the store's guard, so the "
      "element that leaves is exactly the one that left the array, and the difference from "
      ".array's own \"delete\", which discards silently. An empty array bangs the empty outlet "
      "instead: \"nothing left\" is a queue-draining loop's exit condition, not an error. The "
      "message an .array's reference outlet emits on a bang triggers the same removal, so "
      "wiring that outlet here gives the family's gesture. With .array.push this is a stack.");
  ADD_CATEGORY(pCategory::GENERIC);
  INLET_DOC(0, "trigger",
            "A bang removes the last element and emits it out the element outlet; on an empty "
            "array it bangs the empty outlet instead. \"array <name>\" does the same when it "
            "names the array bound by the creation argument — the message an .array's reference "
            "outlet emits on a bang. A reference naming anything else, or any other message, is "
            "refused and counted rather than logged, since this inlet may be the audio thread.",
            "");
  INLET_DOC(1, "array reference", kReferenceInletDoc, "");
  OUTLET_DOC(0, "element",
             "The removed last element, typed the way the patcher spells it: a numeric element "
             "leaves as an int or a float by its spelling and anything else as a symbol. Read "
             "and removed under one hold of the store's guard, sent after it is released.",
             "");
  OUTLET_DOC(1, "empty", kEmptyOutletDoc, "");
  PARAM_DOC("name", "",
            "The array's shared name, addressed as \"<patcherName>.<name>\" — the sequence an "
            ".array of the same name in this patcher holds. Resolved once, on the control "
            "thread, which is why no message re-points it at run time. Empty reads a private, "
            "empty array: every trigger bangs the empty outlet.",
            "any identifier");
}

// ─── .array.shift ─────────────────────────────────────────────────────────────

#undef className
#define className gArrayShift

gArrayShift::gArrayShift() : gArrayEndsRemover(true) {
  ADD_DESCRIPTION(
      "Removes the first element of an array and outputs it — Max's array.shift on the "
      "name-addressed value model .array settled: the array is bound from the creation argument, "
      "\".array.shift <name>\", because an array is addressed by name and never passed down a "
      "cord. A bang removes the first element and emits it out the element outlet, typed the way "
      "the patcher spells it — the read and the erase one hold of the store's guard, so the "
      "element that leaves is exactly the one that left the array, and every element behind it "
      "moves down one position, which is what an ordered sequence means. An empty array bangs "
      "the empty outlet instead: \"nothing left\" is a queue-draining loop's exit condition, not "
      "an error. The message an .array's reference outlet emits on a bang triggers the same "
      "removal. With .array.push this is the queue a generative patch pushes events onto and "
      "takes them off in arrival order.");
  ADD_CATEGORY(pCategory::GENERIC);
  INLET_DOC(0, "trigger",
            "A bang removes the first element and emits it out the element outlet; on an empty "
            "array it bangs the empty outlet instead. \"array <name>\" does the same when it "
            "names the array bound by the creation argument — the message an .array's reference "
            "outlet emits on a bang. A reference naming anything else, or any other message, is "
            "refused and counted rather than logged, since this inlet may be the audio thread.",
            "");
  INLET_DOC(1, "array reference", kReferenceInletDoc, "");
  OUTLET_DOC(0, "element",
             "The removed first element, typed the way the patcher spells it: a numeric element "
             "leaves as an int or a float by its spelling and anything else as a symbol. Read "
             "and removed under one hold of the store's guard, sent after it is released.",
             "");
  OUTLET_DOC(1, "empty", kEmptyOutletDoc, "");
  PARAM_DOC("name", "",
            "The array's shared name, addressed as \"<patcherName>.<name>\" — the sequence an "
            ".array of the same name in this patcher holds. Resolved once, on the control "
            "thread, which is why no message re-points it at run time. Empty reads a private, "
            "empty array: every trigger bangs the empty outlet.",
            "any identifier");
}

// ─── .array.unshift ───────────────────────────────────────────────────────────

#undef className
#define className gArrayUnshift

gArrayUnshift::gArrayUnshift() : gArrayEndsWriter(true) {
  ADD_DESCRIPTION(
      "Adds an element at the front of an array — Max's array.unshift on the name-addressed "
      "value model .array settled: the array is bound from the creation argument, "
      "\".array.unshift <name>\", because an array is addressed by name and never passed down a "
      "cord. An int or a float in the element inlet is inserted at the front as the text that "
      "spells it, every element already there moving up one position, and a list lands whole, in "
      "the order sent — \"unshift a b c\" leaves a b c at the front — or is refused whole, one "
      "counted refusal and nothing changed, when the array cannot take all of it. After an add "
      "that lands, the outlet emits the array's reference, \"array <name>\", so the family "
      "chains. .array.shift's inverse: what unshift put in front is what shift takes out first.");
  ADD_CATEGORY(pCategory::GENERIC);
  INLET_DOC(0, "element",
            "An int or a float is inserted at the front as the text that spells it; a symbol is "
            "inserted as itself; a list lands whole, in the order sent, or is refused whole — "
            "one counted refusal and nothing changed — when the array cannot take all of it or "
            "an element outruns 64 characters. \"array <name>\" is refused even naming the "
            "bound array: a reference is an identity, not an element, so a mis-wired reference "
            "cord shows up in the refusal count instead of in the data. Refusals are counted "
            "rather than logged, since this inlet may be the audio thread.",
            "at most 256 elements");
  INLET_DOC(1, "array reference", kReferenceInletDoc, "");
  OUTLET_DOC(0, "reference", kWriterOutletDoc, "");
  PARAM_DOC("name", "",
            "The array's shared name, addressed as \"<patcherName>.<name>\" — the sequence an "
            ".array of the same name in this patcher holds. Resolved once, on the control "
            "thread, which is why no message re-points it at run time. Empty writes into a "
            "private, empty array: the adds land, but nothing shares them and no reference "
            "leaves.",
            "any identifier");
}
