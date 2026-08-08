#include "gCombine.h"
#include "../../implementations/logImplementation.h"
#include "../math/gExprEval.h"
#include "../pListArgs.h"
#include "../pObjectList.hpp"

using namespace YSE::PATCHER;

#define className gCombine

namespace {

  // The separators Parameters::Set and the list outlets use. Hand-rolled rather
  // than std::isspace, which reads locale state another thread may be mutating
  // and is undefined for a negative char.
  inline bool IsSeparator(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
  }

  // Max's leading attribute, spelled here as a creation argument the way
  // .sprintf spells Max's `symout`. Not a reserved word on inlet 0 — see the
  // class documentation on why that distinction matters for this object in
  // particular.
  constexpr const char* kTriggersFlag = "triggers";

  // Max's "-1 makes all inlets hot".
  constexpr int kAllInletsHot = -1;

  constexpr char kInletDocHot[] =
      "Hot. Stores what arrives as this inlet's item and sends the joined symbol. An int or a "
      "float "
      "is stored as the text that spells it; a list is spread one token per item from here "
      "rightwards, and tokens past the last item are dropped; an empty message clears this item, "
      "which is how a patch takes a component back out of the symbol it is building.";

  constexpr char kInletDocCold[] =
      "Cold. Stores what arrives as this inlet's item and sends nothing; the symbol is joined when "
      "a hot inlet next receives, or when inlet 0 is banged. An int or a float is stored as the "
      "text that spells it; a list is spread one token per item from here rightwards; an empty "
      "message clears this item.";

  constexpr char kBangSentence[] = " A bang sends the items as they stand, whatever the triggers "
                                   "setting is — Max's 'bang: sends "
                                   "stored items as combined symbol'.";

  constexpr char kOutletDoc[] =
      "The items written out one after another with nothing between them — one whitespace-free "
      "token, which is what a symbol can be in this patcher, so a .route matches it as one element "
      "and a .forward or a .s will take it as a destination name. Nothing is sent while every item "
      "is empty.";

} // namespace

CONSTRUCT() {
  // Every port is built by ShapePorts(), because how many inlets there are is a
  // property of the arguments. The clear callback is what makes SetParams("")
  // return the object to its no-argument shape rather than leaving the previous
  // argument list's inlets standing.
  REG_PARM_CLEAR;
  REG_PARM_PARSE;

  ADD_PARAM(creationArgs);

  // The unconfigured object: one inlet, one item, nothing in it — and so
  // nothing sent. Also the shape ClearParams() restores.
  ShapePorts();

  ADD_DESCRIPTION(
      "Joins several items, each held in its own inlet, into a single symbol — Max's combine, "
      "which 'combines a list of items into a single symbol' and 'works similar to pack and "
      "sprintf'. The creation arguments are the items: each one becomes an inlet and that inlet's "
      "starting value, every inlet remembers the last item it was given, and the output is those "
      "items written out one after another with nothing in between. That memory is what "
      "distinguishes it from .tosymbol, which collapses one message that has already arrived "
      "whole: .combine assembles a symbol out of parts arriving at different times from different "
      "sources — an address built from a .route branch, a .counter and a constant, each updating "
      "on its own schedule — which nothing in this patcher could express, since .bondo and .buddy "
      "synchronise arrivals but hand them on through separate outlets. There is no separator "
      "either, because every separator is an item: .combine synth . lead . note is six inlets, "
      "three of them constants, and the punctuation may differ between them, where .tosymbol "
      "applies one uniform separator throughout. Every item is text; a number is stored as the "
      "text that spells it, a list is spread one token per item from the receiving inlet "
      "rightwards, and an empty message clears an item, which is how a patch takes a component "
      "back out of an address. An item never set renders as nothing, Max's 'if no value has been "
      "received for a %s argument, that argument will be left blank', and a .combine holding "
      "nothing at all sends nothing rather than an empty message — the .prepend rule that makes an "
      "unconfigured object safe to drop into a working patch. Max's @triggers attribute is a "
      "leading creation argument here, 'triggers -1' for every inlet hot or 'triggers <n>' for one "
      "of them, read and consumed the way .sprintf reads Max's leading symout; it takes exactly "
      "one token, since the argument list has no bracket to end a subset with, and without it the "
      "hot inlet is inlet 0. It is deliberately not a reserved word on inlet 0: this object exists "
      "to carry arbitrary words, and a reserved one would swallow the very items it is asked to "
      "join. A bang on inlet 0 sends the items as they stand whatever triggers says. Max's "
      "@padding is not reproduced — spelling a number into a fixed-width field is what .sprintf's "
      "%03ld already says, and a second spelling of it here would only make the argument list "
      "ambiguous. At most 32 items, each at most 64 characters and refused rather than truncated "
      "past that. Calculate() does nothing, and no message path allocates, locks or blocks: a join "
      "is one walk of the items into a buffer reserved before the object was published for every "
      "item at full length.");
  ADD_CATEGORY(pCategory::GENERIC);

  PARAM_DOC("items", "",
            "An optional leading 'triggers <n>' followed by the items, one per inlet, each the "
            "starting value of the inlet it builds. 'triggers -1' makes every inlet hot and "
            "'triggers <n>' makes inlet n the only hot one; with no flag at all inlet 0 is hot, "
            "Max's default. At most 32 items, each at most 64 characters; a longer one is reported "
            "and left empty. With no arguments at all the object has one inlet, holds nothing, and "
            "sends nothing.",
            "optional 'triggers <n>' then up to 32 items of at most 64 characters");
}

void gCombine::ShapePorts() {
  // Rebuilt rather than patched: the inlet *count* comes from the arguments, so
  // the items, the trigger mask and the ports all have to agree. Safe because
  // every caller runs before the object is wired or published — the constructor,
  // and the two parameter callbacks, which
  // patcherImplementation::CreateObjectUnlocked runs before AssignGraphIds. A
  // *live* SetParams never reaches here on a published object: registering the
  // callbacks makes ParamsNeedRebuild() true, so #234 replaces the object.

  // Max's @triggers, as a leading argument. Consumed only when it is followed by
  // a whole integer: `triggers` on its own, or followed by something that is not
  // a number, is a patch author's typo rather than a request, and reading it as
  // an item silently would move every item after it.
  int requested = 0;
  std::size_t first = 0;
  if (!creationArgs.empty() && creationArgs[0] == kTriggersFlag) {
    std::size_t cursor = 0;
    int parsed = 0;
    if (creationArgs.size() >= 2 && ReadIntArgAt(creationArgs[1], cursor, parsed) &&
        cursor == creationArgs[1].size()) {
      requested = parsed;
      first = 2;
    } else {
      INTERNAL::LogImpl().emit(E_ERROR,
                               std::string("patcher: ") + Type() +
                                   " expects an inlet number (or -1 for all) after \"triggers\"; "
                                   "read as an item instead");
    }
  }

  std::size_t itemCount = creationArgs.size() - first;
  if (itemCount > MAX_ITEMS) {
    INTERNAL::LogImpl().emit(E_ERROR, std::string("patcher: ") + Type() +
                                          " takes at most 32 items; the rest are ignored");
    itemCount = MAX_ITEMS;
  }

  // Always at least one inlet: an object with no arguments still has somewhere
  // for a patch to send, and it is the identity on the one item it then holds.
  const std::size_t inletCount = itemCount == 0 ? 1 : itemCount;

  items.clear();
  items.resize(inletCount);
  for (std::size_t i = 0; i < inletCount; i++) {
    // The one allocation a store would otherwise need, taken here on the control
    // thread. Reserved after resize(), which is what may reallocate the vector.
    items[i].reserve(ITEM_CAPACITY);
  }

  for (std::size_t i = 0; i < itemCount; i++) {
    const std::string& arg = creationArgs[first + i];
    // Refused rather than truncated, and loudly: this is the control thread, so
    // unlike the inlet path it can afford a reason. The item is left empty,
    // which renders as nothing, rather than as half a word no .route matches.
    if (arg.size() > ITEM_CAPACITY) {
      INTERNAL::LogImpl().emit(E_ERROR, std::string("patcher: ") + Type() + " item \"" + arg +
                                            "\" is longer than 64 characters; left empty");
      continue;
    }
    items[i].assign(arg);
  }

  // Max: "setting to -1 makes all inlets hot". An index outside the inlets the
  // arguments actually built would leave the object unable to send at all, which
  // is never what was meant.
  if (requested == kAllInletsHot) {
    triggerMask = inletCount >= 32 ? 0xFFFFFFFFu : ((1u << inletCount) - 1u);
  } else if (requested >= 0 && (std::size_t)requested < inletCount) {
    triggerMask = 1u << requested;
  } else {
    if (first == 2) {
      INTERNAL::LogImpl().emit(E_ERROR, std::string("patcher: ") + Type() +
                                            " has no such inlet to trigger on; inlet 0 kept");
    }
    triggerMask = 1u;
  }

  inputs.clear();
  outputs.clear();

  for (std::size_t pin = 0; pin < inletCount; pin++) {
    if (pin == 0) {
      // Inlet 0 is the object's active one, as it is everywhere else in the
      // patcher, and the only one a bang means anything on: a cold inlet has
      // nothing to do with one, so it does not register a handler and
      // GetAcceptedTypes() keeps reporting the real contract.
      ADD_IN_0;
      REG_BANG_IN(SetBang);
    } else {
      inputs.emplace_back(this, false, (int)pin);
    }
    REG_INT_IN(SetInt);
    REG_FLOAT_IN(SetFloat);
    REG_LIST_IN(SetList);

    const bool hot = ((triggerMask >> pin) & 1u) != 0u;
    std::string doc = hot ? kInletDocHot : kInletDocCold;
    if (pin == 0) doc += kBangSentence;
    inputs.back().SetDoc(InletLabel((int)pin), doc, "any");
  }

  // ANY rather than LIST: the object always sends text, but a reader should not
  // have to know that to wire to it.
  ADD_OUT_ANY;
  outputs.back().SetDoc("out", kOutletDoc, "any");

  // The allocation the join would otherwise need, taken here for every item at
  // full length.
  outText.clear();
  outText.reserve((inletCount * ITEM_CAPACITY) + 1);
}

PARM_CLEAR() {
  // Runs on the control thread before the parameter string is re-read, and is
  // the whole of `SetParams("")`: Parameters::Set returns without calling the
  // parse callback for an empty argument, so this has to leave the no-argument
  // object behind rather than one still wearing the previous arguments' inlets.
  creationArgs.clear();
  ShapePorts();
}

PARM_PARSE() {
  ShapePorts();
}

const std::string& gCombine::Item(int index) const {
  static const std::string empty;
  if (index < 0 || index >= (int)items.size()) return empty;
  return items[(std::size_t)index];
}

void gCombine::StoreToken(std::size_t index, const char* text, std::size_t length) {
  if (index >= items.size()) return;
  // Refused rather than truncated, and silently, since this may be the audio
  // thread. Half an item is a different item.
  if (length > ITEM_CAPACITY) return;
  items[index].assign(text, length);
}

void gCombine::Distribute(const std::string& text, std::size_t first) {
  // Max: "each item in the list is treated as if it had been received in a
  // separate inlet, up to the number of inlets". Tokens past the last item have
  // nowhere to go and are dropped. Walked in place — a substr per token would
  // allocate on whichever thread the message arrived on.
  const std::size_t size = text.size();
  std::size_t i = 0;
  std::size_t index = first;
  bool any = false;

  while (i < size && index < items.size()) {
    while (i < size && IsSeparator(text[i]))
      i++;
    if (i >= size) break;

    const std::size_t begin = i;
    while (i < size && !IsSeparator(text[i]))
      i++;

    StoreToken(index, text.c_str() + begin, i - begin);
    index++;
    any = true;
  }

  // Nothing but whitespace is a real request, not a malformed one — the
  // .prepend reading of an empty message. It clears the receiving item, which
  // is the only way a patch can take a component back out of the symbol it is
  // building.
  if (!any) StoreToken(first, "", 0);
}

void gCombine::Emit(YSE::THREAD thread) {
  // Filled here rather than kept between sends: the send path is synchronous, so
  // a patch looping the outlet back into an inlet re-enters this function inside
  // the SendList below. Appends only, into capacity reserved by ShapePorts().
  outText.clear();
  for (const std::string& item : items)
    outText.append(item);

  // A .combine holding nothing is inert rather than a source of empty messages —
  // the .prepend / .sprintf rule that makes an unconfigured object safe to drop
  // into a working patch.
  if (outText.empty()) return;

  outputs[0].SendList(outText, thread);
}

BANG_IN(SetBang) {
  // Max: "bang — sends stored items as combined symbol." Registered on inlet 0
  // alone, so there is nothing to guard against here, and it sends whatever the
  // triggers setting is: a patch always has an explicit way to ask.
  (void)inlet;
  Emit(thread);
}

INT_IN(SetInt) {
  // ExprFormatValue is the patcher's number-to-text writer: no allocation, no
  // locale (so the decimal separator is always '.', which is what the reader on
  // the other end expects) and no exception.
  char text[kExprValueTextMax];
  const int length = ExprFormatValue(ExprValue::Int(value), text, kExprValueTextMax);
  // The store settles before the send, so a patch that loops the outlet back
  // into an inlet finds the message it is being handed.
  StoreToken((std::size_t)inlet, text, length > 0 ? (std::size_t)length : 0);
  if (IsHot(inlet)) Emit(thread);
}

FLOAT_IN(SetFloat) {
  // A float keeps its decimal point, so it stays visibly a float inside the
  // symbol it has just become part of.
  char text[kExprValueTextMax];
  const int length = ExprFormatValue(ExprValue::Float(value), text, kExprValueTextMax);
  StoreToken((std::size_t)inlet, text, length > 0 ? (std::size_t)length : 0);
  if (IsHot(inlet)) Emit(thread);
}

LIST_IN(SetList) {
  Distribute(value, (std::size_t)inlet);
  if (IsHot(inlet)) Emit(thread);
}

#undef className
