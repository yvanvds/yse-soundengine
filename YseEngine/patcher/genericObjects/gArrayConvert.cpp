#include "gArrayConvert.h"
#include "../pObjectList.hpp"

#include <cstddef>
#include <string>

using namespace YSE::PATCHER;

namespace {

  // Doc strings shared by the three objects, hoisted out of the constructors
  // because they are long enough that a constructor stops being readable with
  // them inline — gArray's arrangement. What differs per object stays with
  // that object below.
  constexpr char kTriggerInletDoc[] =
      "Bang, or the bound array's reference \"array <name>\": send the converted array.";
  constexpr char kReferenceInletDoc[] =
      "Accepts the bound array's reference \"array <name>\" and ignores it, so the array's "
      "reference outlet can be wired here. Anything else is refused.";
  constexpr char kEmptyOutletDoc[] = "Bang when the array is empty.";
  constexpr char kNameParamDoc[] =
      "Name of the array to use: the .array with this name in the same patcher. Empty uses a "
      "private, empty array.";

} // namespace

// ─── the shared base ──────────────────────────────────────────────────────────

#define className gArrayConvertBase

gArrayConvertBase::gArrayConvertBase(bool typedOutlet) : gArrayEndsBase() {
  // The trigger inlet, hot, and the reference inlet, cold — gArrayJoin's
  // shape: the result is asked for with a bang, never addressed, so there is
  // no int or float method anywhere.
  ADD_IN_0;
  REG_BANG_IN(BangIn);
  REG_LIST_IN(ListIn);
  ADD_IN_1;
  REG_LIST_IN(ListIn);

  // ANY on .array.tolist, whose one-element answer leaves as the int, float
  // or symbol it spells; LIST on the two converters that only ever send
  // text. The empty outlet is always a bang.
  if (typedOutlet) {
    ADD_OUT_ANY;
  } else {
    ADD_OUT_LIST;
  }
  ADD_OUT_BANG;

  // The allocation the send path would otherwise need, taken here on the
  // control thread.
  AtomList::ReserveRender(emitScratch);
}

BANG_IN(BangIn) {
  (void)inlet;
  Ask(thread);
}

LIST_IN(ListIn) {
  if (inlet == 1) {
    // The reference inlet acknowledges the array it is already bound to and
    // nothing more — gDictSlice's inlet rule.
    if (!ArrayReferenceNames(value, arrayName)) Refuse();
    return;
  }

  // The array's reference asks for the conversion — the message its .array
  // emits on a bang, the family's gesture. Anything else is refused:
  // resolving an unrecognised name means the registry's mutex, and this may
  // be the audio thread.
  if (ArrayReferenceNames(value, arrayName)) {
    Ask(thread);
    return;
  }
  Refuse();
}

void gArrayConvertBase::Ask(YSE::THREAD thread) {
  std::size_t lost = 0;
  emitList.Clear();
  {
    const arrayStoreGuard guard(store->busy);
    if (!guard.Held()) {
      Refuse();
      return;
    }
    // One bounded pass under one hold — the family's mid-walk answer: there
    // is no walk to be in the middle of, so the render is the array as it
    // stood at the trigger. An element that does not fit the working list is
    // *counted* here and *judged* in Deliver: the list-text converters lose
    // the tail (getvalue's rule), the one-token converter refuses whole.
    for (std::size_t i = 0; i < store->count; i++) {
      if (!emitList.Add(store->elements[i].data(), store->elements[i].size())) lost++;
    }
  }

  // Outside the guard on purpose: a send runs the whole downstream graph,
  // which may well write into this same array, and inside the guard that
  // write would be the one thing the try-lock drops.
  Deliver(lost, thread);
}

// ─── .array.tolist ────────────────────────────────────────────────────────────

#undef className
#define className gArrayToList

gArrayToList::gArrayToList() : gArrayConvertBase(true) {
  ADD_DESCRIPTION(
      "Outputs all elements of a named array as one list. An array of one element sends that "
      "element as a single int, float or symbol.");
  ADD_CATEGORY(pCategory::ARRAY);
  INLET_DOC(0, "trigger", kTriggerInletDoc, "");
  INLET_DOC(1, "array reference", kReferenceInletDoc, "");
  OUTLET_DOC(0, "list",
             "The elements in order, as a list; a single element leaves as an int, float or "
             "symbol. Items past what a list can hold are dropped.",
             "");
  OUTLET_DOC(1, "empty", kEmptyOutletDoc, "");
  PARAM_DOC("name", "", kNameParamDoc, "any identifier");
}

void gArrayToList::Deliver(std::size_t lost, YSE::THREAD thread) {
  // A list that lost its tail is a shorter list, and the patch can see the
  // count — getvalue's rule, one counted refusal per lost element, #796's
  // own prescription for the list-text converters.
  for (std::size_t i = 0; i < lost; i++)
    Refuse();
  if (emitList.Empty()) {
    outputs[1].SendBang(thread);
    return;
  }
  // Through SendAtoms, so an array of one element sends that element rather
  // than a list of one — the patcher's transport rule.
  SendAtoms(outputs[0], emitList, emitScratch, thread);
}

// ─── .array.tostring ──────────────────────────────────────────────────────────

#undef className
#define className gArrayToString

gArrayToString::gArrayToString() : gArrayConvertBase(false) {
  ADD_DESCRIPTION(
      "Outputs all elements of a named array as one text message, separated by spaces. Unlike "
      ".array.tolist, a single number is sent as text, ready for objects that work on text.");
  ADD_CATEGORY(pCategory::ARRAY);
  INLET_DOC(0, "trigger", kTriggerInletDoc, "");
  INLET_DOC(1, "array reference", kReferenceInletDoc, "");
  OUTLET_DOC(0, "string",
             "The elements in order, separated by spaces, always as text. Items past what a "
             "list can hold are dropped.",
             "");
  OUTLET_DOC(1, "empty", kEmptyOutletDoc, "");
  PARAM_DOC("name", "", kNameParamDoc, "any identifier");
}

void gArrayToString::Deliver(std::size_t lost, YSE::THREAD thread) {
  // The same tail-loss rule as .array.tolist — the two list-text converters
  // judge an overflow identically; only the send differs.
  for (std::size_t i = 0; i < lost; i++)
    Refuse();
  if (emitList.Empty()) {
    outputs[1].SendBang(thread);
    return;
  }
  // Rendered whole and sent as text, whatever it spells — never through
  // SendAtoms, whose single-atom retyping is exactly what a string is not.
  emitList.Render(emitScratch);
  outputs[0].SendList(emitScratch, thread);
}

// ─── .array.tosymbol ──────────────────────────────────────────────────────────

#undef className
#define className gArrayToSymbol

gArrayToSymbol::gArrayToSymbol() : gArrayConvertBase(false) {
  ADD_DESCRIPTION(
      "Joins all elements of a named array, without spaces, into one symbol: kick and .wav "
      "become kick.wav. The result is always a symbol, even when it looks like a number.");
  ADD_CATEGORY(pCategory::ARRAY);
  INLET_DOC(0, "trigger", kTriggerInletDoc, "");
  INLET_DOC(1, "array reference", kReferenceInletDoc, "");
  OUTLET_DOC(0, "symbol",
             "The elements joined into one word, always a symbol. Nothing is sent when the "
             "array is too long to fit in one message.",
             "");
  OUTLET_DOC(1, "empty", kEmptyOutletDoc, "");
  PARAM_DOC("name", "", kNameParamDoc, "any identifier");
}

void gArrayToSymbol::Deliver(std::size_t lost, YSE::THREAD thread) {
  // One token leaves whole or not at all: a token that lost elements is a
  // different name — truncation by another name — so the ask is one counted
  // refusal and nothing fires. .array.join's rule (#793) for the same
  // one-token send, chosen over getvalue's tail-loss on purpose; see the
  // class notes.
  if (lost != 0) {
    Refuse();
    return;
  }
  if (emitList.Empty()) {
    outputs[1].SendBang(thread);
    return;
  }
  // Butted, not spaced: the collapse that makes one token is the whole
  // difference from .array.tostring. The butted text can only be shorter
  // than the collected list text, so the reserve taken at construction
  // covers it and nothing here allocates.
  emitScratch.clear();
  for (std::size_t i = 0; i < emitList.Size(); i++)
    emitScratch.append(emitList.AtomText(i), emitList.AtomLength(i));
  outputs[0].SendList(emitScratch, thread);
}
