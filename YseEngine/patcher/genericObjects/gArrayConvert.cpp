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
      "A bang asks for the conversion — every element in order, collected under one hold of the "
      "store's guard and sent after it is released, so the answer is the array as it stood at "
      "the trigger. \"array <name>\" does the same when it names the array bound by the creation "
      "argument — the message an .array's reference outlet emits on a bang, the family's "
      "gesture. A reference naming anything else, or any other message, is refused and counted "
      "rather than logged, since this inlet may be the audio thread.";
  constexpr char kReferenceInletDoc[] =
      "\"array <name>\" is accepted silently when it names the array bound by the creation "
      "argument, so a patch may wire the array's reference outlet across. It sets nothing — the "
      "binding is the creation argument, resolved on the control thread — and anything else is "
      "refused and counted.";
  constexpr char kEmptyOutletDoc[] =
      "Bang when the array holds nothing to convert — an empty or unnamed (private) array. \"No "
      "data\" is a state a patch must be able to route on, not an error. A lost try-lock is a "
      "counted refusal instead: the array's state is unknown, so neither outlet fires.";
  constexpr char kNameParamDoc[] =
      "The array's shared name, addressed as \"<patcherName>.<name>\" — the sequence an .array "
      "of the same name in this patcher holds. Resolved once, on the control thread, which is "
      "why no message re-points it at run time. Empty reads a private, empty array: every ask "
      "bangs the empty outlet.";

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
      "Outputs an array as a list — Max's array.tolist on the name-addressed value model .array "
      "settled: the array is bound from the creation argument, \".array.tolist <name>\", because "
      "an array is addressed by name and never passed down a cord. A bang sends every element in "
      "order as the message the array spells, typed the way the patcher spells it — an array of "
      "one element sends that element as the int, float or symbol it is rather than as a list of "
      "one, and several elements leave as one list message. The elements are collected under one "
      "hold of the store's guard and sent after it is released, so the answer is the array as it "
      "stood at the trigger; an empty or unnamed (private) array bangs the empty outlet instead. "
      "An array whose elements together outrun what a cord carries loses its tail, every lost "
      "element a counted refusal — .array's own getvalue rule, and this object is exactly that "
      "message's patching form: it takes a reference, acts on a bang, and reports through "
      "outlets of its own.");
  ADD_CATEGORY(pCategory::GENERIC);
  INLET_DOC(0, "trigger", kTriggerInletDoc, "");
  INLET_DOC(1, "array reference", kReferenceInletDoc, "");
  OUTLET_DOC(0, "list",
             "The array as the message it spells, typed: one element leaves as the int, float "
             "or symbol its spelling reads as, several as one space-separated list message — "
             "lossless into another .array by the store's one-atom-per-element rule. A longer "
             "array than a cord carries loses its tail, every lost element a counted refusal. "
             "Silent on an empty array: the empty outlet carries that half of the answer.",
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
      "Outputs an array as a string — Max's array.tostring on the name-addressed value model "
      ".array settled: the array is bound from the creation argument, \".array.tostring "
      "<name>\", because an array is addressed by name and never passed down a cord. This "
      "patcher has no atom types — a message is text, and the serialized form of an array IS "
      "its list text, an array and the list it spells being the same thing seen twice — so the "
      "one honest reading of \"a string\" is the same space-separated characters .array.tolist "
      "renders, sent always as text and never retyped: where .array.tolist's single numeric "
      "element leaves as the int it is, this object's leaves as the characters that spell it, "
      "which is what .sprintf, .substitute, .textedit and everything else that works on text "
      "wants — .tosymbol's own send discipline, applied to an array. Collected under one hold "
      "of the store's guard, sent after release; an empty or unnamed (private) array bangs the "
      "empty outlet, and a longer array than a cord carries loses its tail, every lost element "
      "a counted refusal — getvalue's rule.");
  ADD_CATEGORY(pCategory::GENERIC);
  INLET_DOC(0, "trigger", kTriggerInletDoc, "");
  INLET_DOC(1, "array reference", kReferenceInletDoc, "");
  OUTLET_DOC(0, "string",
             "The array as the characters it spells: every element in order, a single space "
             "between each pair, always as text — a single numeric element leaves as the text "
             "that spells it, never retyped to the int or float it reads as, because a string "
             "is characters, not a value. A longer array than a cord carries loses its tail, "
             "every lost element a counted refusal. Silent on an empty array: the empty outlet "
             "carries that half of the answer.",
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
      "Outputs an array as a single symbol — Max's array.tosymbol on the name-addressed value "
      "model .array settled: the array is bound from the creation argument, \".array.tosymbol "
      "<name>\", because an array is addressed by name and never passed down a cord. The only "
      "reading of \"a single symbol\" this patcher can represent is a message that is a single "
      "whitespace-free token — .tosymbol's rule, inherited whole — so the elements leave butted "
      "together into one token: \"kick .wav\" becomes kick.wav, which is the file-name use the "
      "issue names, and the token goes wherever a name goes — a .s, a .route, a .forward. Never "
      "retyped: a symbol is a name, so \"1 2\" collapses to the characters 12, where "
      ".array.join's empty-separator default deliberately leaves the int 12 — .array.join is "
      "the object with a separator, and space-separated text is .array.tostring's answer. "
      "Collected under one hold of the store's guard, sent after release; an empty or unnamed "
      "(private) array bangs the empty outlet. A result that cannot leave whole is refused "
      "whole and counted — a token that lost elements is a different name, so getvalue's "
      "tail-loss rule deliberately does not apply here, .array.join's whole-reply rule instead.");
  ADD_CATEGORY(pCategory::GENERIC);
  INLET_DOC(0, "trigger", kTriggerInletDoc, "");
  INLET_DOC(1, "array reference", kReferenceInletDoc, "");
  OUTLET_DOC(0, "symbol",
             "The elements butted together into one whitespace-free token, always as text — "
             "never retyped to the number the collapsed characters may spell, because a symbol "
             "is a name, not a value. A single-element array answers that element's characters "
             "alone. An array whose collected elements outrun what a cord carries is refused "
             "whole and counted — a partial token would be a different name. Silent on an "
             "empty array: the empty outlet carries that half of the answer.",
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
