#include "gUnpack.h"
#include "../../implementations/logImplementation.h"
#include "../math/gExprEval.h"
#include "../pAtomList.h"
#include "../pListArgs.h"
#include "../pObjectList.hpp"
#include "../pSelector.h"
#include <cstddef>
#include <string>

using namespace YSE::PATCHER;

#define className gUnpack

namespace {

  // An int as text, through the patcher's one int formatter rather than
  // std::to_string. Control-thread only (the construction-time log).
  std::string IntText(int value) {
    char digits[YSE::PATCHER::FORMAT_INT_WIDTH];
    const std::size_t written = YSE::PATCHER::WriteInt(value, digits);
    return std::string(digits, written);
  }

  const char* OutletDoc(gUnpack::unpackType type) {
    switch (type) {
    case gUnpack::unpackType::INT:
      return "The element of the incoming list in this position, as an int — a float is truncated, "
             "and a symbol is refused, leaving the element carrying the value it already held and "
             "counting the refusal. Fires only when the list was long enough to reach this "
             "position, and fires in right-to-left order with every other outlet. A bang fires it "
             "whatever has arrived, carrying the creation argument until a list overwrites it.";
    case gUnpack::unpackType::FLOAT:
      return "The element of the incoming list in this position, as a float — an int is promoted "
             "and keeps its decimal point, and a symbol is refused, leaving the element carrying "
             "the value it already held and counting the refusal. Fires only when the list was "
             "long enough to reach this position, and fires in right-to-left order with every "
             "other outlet. A bang fires it whatever has arrived, carrying the creation argument "
             "until a list overwrites it.";
    case gUnpack::unpackType::SYMBOL:
    default:
      break;
    }
    return "The element of the incoming list in this position, verbatim — this outlet was declared "
           "with a symbol argument, so it converts nothing and passes on whatever arrived, which "
           "leaves as the int, float or symbol that element spells. Fires only when the list was "
           "long enough to reach this position, and fires in right-to-left order with every other "
           "outlet. A bang fires it whatever has arrived, carrying the creation argument until a "
           "list overwrites it.";
  }

  const char* OutletRange(gUnpack::unpackType type) {
    switch (type) {
    case gUnpack::unpackType::INT:
      return "any int";
    case gUnpack::unpackType::FLOAT:
      return "any float";
    case gUnpack::unpackType::SYMBOL:
    default:
      break;
    }
    return "any";
  }

} // namespace

CONSTRUCT() {
  // The one inlet, and the only one this object will ever have — the outlets
  // are what the creation arguments shape. Built here rather than in
  // ShapePorts() so that a re-parse rebuilding the outlets cannot drop the
  // handlers off the inlet a patch is already wired to.
  ADD_IN_0;
  REG_BANG_IN(UnpackBang);
  REG_INT_IN(UnpackInt);
  REG_FLOAT_IN(UnpackFloat);
  REG_LIST_IN(UnpackList);

  // Every outlet is built by ShapePorts(), because the argument list *is* the
  // outlet list. The clear callback is what makes `SetParams("")` return the
  // object to Max's no-argument shape rather than leaving the previous outlet
  // count in place.
  REG_PARM_CLEAR;
  REG_PARM_PARSE;

  ADD_PARAM(creationArgs);

  // Max: "If no argument is typed in, unpack will have two int outlets." Also
  // the shape ClearParams() restores.
  ShapePorts();

  // The one allocation the object makes outside its two lists, and it happens
  // here rather than on an arrival: an element is rendered into storage that is
  // already long enough, on whichever thread the list came in on — routinely
  // the audio callback.
  AtomList::ReserveRender(render);

  ADD_DESCRIPTION(
      "Breaks a list into its elements and sends each one out a separate outlet — Max's unpack, "
      "and the mirror image of .pack. It is the only way a patch gets at the individual elements "
      "of a list that arrives from somewhere else, including the list payloads the named bus "
      "delivers, a three-element list being how sound.<name>.position is addressed; without it a "
      "list can be held, measured and passed along but never taken apart into the numbers a patch "
      "computes with. There is one outlet per creation argument, and each argument's spelling "
      "decides what its outlet emits as well as what that element starts as: a whole integer makes "
      "an int outlet, a token with a decimal point or an exponent makes a float outlet, and "
      "anything else makes a symbol outlet. With no arguments the object is Max's default, two int "
      "outlets starting at 0. The type is enforced on the way in, Max's 'the inlet type is forced "
      "to the outlet type that is defined', and it is .pack's enforcement read backwards: an int "
      "outlet truncates a float, a float outlet promotes an int and keeps its decimal point, a "
      "symbol outlet passes on whatever arrived verbatim, and a number outlet handed a symbol "
      "keeps the value it already held and counts the refusal rather than emitting a 0 that would "
      "read downstream as a value the list carried. Because the coercion happens on the way in, an "
      "int outlet only ever sends ints and a float outlet only ever floats, which is what they are "
      "declared as; a symbol outlet is declared as accepting anything, since it stores what "
      "arrived and what arrived may spell a number. Outlets fire right to left, Max's universal "
      "order, and each send completes in full — the whole subgraph behind that outlet, depth first "
      "— before the next one starts, so the elements to the right land before the leftmost one "
      "arrives to set the result off. Only the outlets the message actually reached fire: a "
      "two-item list into a three-outlet .unpack fires outlets 1 and 0 and leaves outlet 2 silent, "
      "and a bare int or float fires the leftmost outlet only, which is Max's 'the number is sent "
      "out the left outlet'. Items past the last outlet are dropped and counted. A bang sends the "
      "set as it stands out every outlet, Max's 'causes each stored item of a list to be sent out "
      "the corresponding outlet', which is why the object holds the elements rather than "
      "distributing them straight through; an element no list has reached carries its creation "
      "argument. There is no 'set' message, Max documents none and there would be nothing for it "
      "to mean, this object's whole output being the distribution. The held elements are the "
      "bounded pre-allocated storage the whole list family shares: at most 256 elements spanning "
      "at most 1024 characters between them, in memory reserved when the object is built. A store "
      "whose result would not fit is refused whole, counted, and sends nothing at all — unlike "
      ".pack, which still releases its unchanged list, because here the output is the incoming "
      "elements and firing the outlets with the values they already held would present stale state "
      "as the list that just arrived; the count is a counter rather than a log line because the "
      "refusing thread may be the audio callback, while a creation argument that does not fit is "
      "logged, parameter parsing being control-thread only. Re-typing the creation arguments "
      "rebuilds the object, since the arguments are the outlet count and the outlet types. "
      "Calculate() does nothing and no message path allocates, locks or blocks: numbers are "
      "rendered into a stack buffer, the store is a rebuild inside a list reserved at "
      "construction, and the sends render into a buffer reserved at the same time. Two threads "
      "writing the same object are serialised by a single test-and-set guard whose loser is "
      "dropped and counted rather than made to spin, which is also what stops an object wired back "
      "into its own inlet from recursing on the audio thread.");

  ADD_CATEGORY(pCategory::GENERIC);

  INLET_DOC(0, "in",
            "The list to break apart. Its first item goes to the leftmost outlet, its second to "
            "the next one along, and so on for as many outlets as there are; items past the last "
            "outlet are dropped and counted. An int or a float is a one-item list and so reaches "
            "the leftmost outlet only. A bang sends the elements as they stand out every outlet, "
            "including ones no list has reached, which carry their creation argument. Each item is "
            "converted to the type its outlet was declared with, and an item that cannot be "
            "converted — a symbol arriving at a number outlet — leaves that element carrying the "
            "value it already held and is counted.",
            "any");

  PARAM_DOC("elements", "0 0",
            "One argument per outlet, and each one is that element's starting value as well as "
            "its type. A token spelled as a whole integer declares an int outlet, one carrying a "
            "decimal point or an exponent declares a float outlet, and anything else declares a "
            "symbol outlet whose starting value is the token itself. With no arguments the object "
            "is Max's default: two int outlets starting at 0. At most 256 outlets are built, "
            "spanning at most 1024 characters between them; an argument list that does not fit is "
            "clamped and the clamp is logged.",
            "one argument per outlet, 1-256; int / float / symbol by spelling");
}

// ─── the creation arguments ─────────────────────────────────────────────────

void gUnpack::ShapePorts() {
  // Rebuilt rather than resized: the outlet *count* comes from the arguments,
  // so the outlets, the type table and the held elements all have to agree.
  // Safe because every caller runs before the object is wired or published —
  // the constructor, and the two parameter callbacks, which
  // patcherImplementation::CreateObjectUnlocked runs before AssignGraphIds.
  types.clear();
  slots.Clear();
  work.Clear();

  // Parameters::Set splits on single spaces, so a run of them yields empty
  // tokens; an empty token declares no outlet.
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

    // The argument is the element's starting value *and*, by its spelling, the
    // outlet's type — Max's "the arguments can be any combination of ints,
    // floats, and symbols", setting the output type of each outlet.
    if (!slots.Add(token)) break;

    float number = 0.f;
    if (!ReadNumericToken(token, number)) {
      types.push_back(unpackType::SYMBOL);
    } else if (TokenLooksLikeFloat(token)) {
      types.push_back(unpackType::FLOAT);
    } else {
      types.push_back(unpackType::INT);
    }
    taken++;
  }

  if (taken != requested) {
    // The control thread, before the object is wired or published, so this is
    // the one place a clamp can be *said* rather than merely observable through
    // PortCount().
    INTERNAL::LogImpl().emit(
        E_WARNING, std::string("patcher: ") + YSE::OBJ::G_UNPACK + " asked for " +
                       IntText(requested) + " elements; only " + IntText(taken) +
                       " fitted (at most " + IntText(MAX_PORTS) + " elements spanning " +
                       IntText((int)AtomList::TEXT_CAPACITY) + " characters between them)");
  }

  if (taken == 0) {
    // Max's default object, and the fallback for an argument list where not
    // even the first element fitted: "if no argument is typed in, unpack will
    // have two int outlets". Rebuilt from scratch so a partial first pass
    // cannot leak in.
    slots.Clear();
    for (int i = 0; i < DEFAULT_PORTS; i++) {
      slots.AddInt(0);
      types.push_back(unpackType::INT);
    }
  }

  outputs.clear();

  for (const unpackType type : types) {
    // The coercion is enforced on the way in, so an int element only ever holds
    // int-spelled text and a float element only ever float-spelled text. That
    // makes the typed outlet the honest declaration rather than ANY — which a
    // symbol element does need, since it holds whatever arrived and that may
    // spell a number.
    switch (type) {
    case unpackType::INT:
      ADD_OUT_INT;
      break;
    case unpackType::FLOAT:
      ADD_OUT_FLOAT;
      break;
    case unpackType::SYMBOL:
    default:
      ADD_OUT_ANY;
      break;
    }
  }

  ApplyDocs();
}

void gUnpack::ApplyDocs() {
  for (int i = 0; i < (int)outputs.size(); i++)
    outputs[(std::size_t)i].SetDoc(OutletLabel(i), OutletDoc(types[(std::size_t)i]),
                                   OutletRange(types[(std::size_t)i]));
}

PARM_CLEAR() {
  // Runs on the control thread before the parameter string is re-read, and is
  // the whole of `SetParams("")`: Parameters::Set returns without calling the
  // parse callback for an empty argument, so this has to leave Max's
  // no-argument object behind rather than one still holding the previous
  // outlets.
  creationArgs.clear();
  ShapePorts();
}

PARM_PARSE() {
  ShapePorts();
}

// ─── the elements ───────────────────────────────────────────────────────────

gUnpack::unpackType gUnpack::SlotType(int index) const {
  if (index < 0 || index >= (int)types.size()) return unpackType::SYMBOL;
  return types[(std::size_t)index];
}

std::string gUnpack::Stored() const {
  // Diagnostics only: this builds a string, which is exactly what the object's
  // own send path is written to avoid.
  std::string out;
  AtomList::ReserveRender(out);
  slots.Render(out);
  return out;
}

// ─── the guard ──────────────────────────────────────────────────────────────

bool gUnpack::Enter() {
  if (busy.exchange(true, std::memory_order_acq_rel)) {
    // Another thread is mid-message, or this object is wired back into its own
    // inlet and a send has come round again. Counted rather than spun on: this
    // is a path the audio callback takes.
    CountDrop();
    return false;
  }
  return true;
}

void gUnpack::Leave() {
  busy.store(false, std::memory_order_release);
}

void gUnpack::CountDrop(std::size_t count) {
  dropped.fetch_add((std::uint64_t)count, std::memory_order_relaxed);
}

// ─── storing ────────────────────────────────────────────────────────────────

bool gUnpack::AddCoerced(AtomList& into, int index, const char* token, std::size_t length,
                         bool& refused) {
  float number = 0.f;
  const bool numeric = ReadNumericToken(token, length, number);

  switch (types[(std::size_t)index]) {
  case unpackType::INT:
  case unpackType::FLOAT:
    if (!numeric) {
      // Max: "the inlet type is forced to the outlet type that is defined" —
      // and a symbol has nothing to force. The element keeps what it had rather
      // than taking a 0 that would read downstream as a value the list carried,
      // which is the rule `.pack`'s number element already follows.
      refused = true;
      return into.Add(slots.AtomText(index), slots.AtomLength(index));
    }
    // Truncated through the range-checked conversion rather than cast, casting
    // a float outside the int range being undefined.
    return types[(std::size_t)index] == unpackType::INT ? into.AddInt(ExprToInt(number))
                                                        : into.AddFloat(number);

  case unpackType::SYMBOL:
  default:
    // Whatever arrived, verbatim — the outlet was declared with a symbol
    // argument, so it converts nothing.
    return into.Add(token, length);
  }
}

bool gUnpack::Store(const char* text, std::size_t length, int& written) {
  // The backing text of an AtomList is append-only until Clear(), so replacing
  // the elements is a rebuild: every element written into the scratch list,
  // then one Assign. Bounded, and allocation-free — both lists reserved their
  // text in the constructor. Written into `work` rather than in place so that a
  // store which does not fit leaves the held elements exactly as they were.
  const int count = (int)types.size();
  work.Clear();
  written = 0;

  std::size_t i = 0;
  bool refusedByType = false;

  for (int j = 0; j < count; j++) {
    bool wrote = false;

    // Max: "each item in the list is sent out the outlet corresponding to its
    // position in the list" — so item 0 goes to element 0 and the walk is
    // strictly left to right, whatever order the sends then happen in.
    while (i < length && IsSelectorSeparator(text[i]))
      i++;
    if (i < length) {
      const std::size_t begin = i;
      while (i < length && !IsSelectorSeparator(text[i]))
        i++;
      if (!AddCoerced(work, j, text + begin, i - begin, refusedByType)) return false;
      wrote = true;
    }

    // Every element the list did not reach keeps what it holds, and its outlet
    // stays silent — Max's "up to the number of outlets" read from the other
    // end. A later bang is what sends it.
    if (!wrote && !work.Add(slots.AtomText(j), slots.AtomLength(j))) return false;
    if (wrote) written++;
  }

  slots.Assign(work);

  // Items past the last outlet have nowhere to go. Max drops them; counted here
  // so a patch can see that it happened.
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

void gUnpack::Emit(int count, YSE::THREAD thread) {
  // Right to left, and each Send returns only once the whole subgraph behind
  // that outlet has run — Max's universal order, the one `.trigger` states as a
  // guarantee. Walking forwards here would break every patch that wires the
  // right-hand elements into cold inlets, so it is worth being loud: the loop
  // counts down.
  //
  // One atom per outlet through the family's shared SendAtom, which sends the
  // int, float or symbol the element spells. Never a list: lists arrive here
  // and single values leave.
  for (int i = count - 1; i >= 0; i--)
    SendAtom(outputs[(std::size_t)i], slots, (std::size_t)i, render, thread);
}

void gUnpack::Take(const char* text, std::size_t length, YSE::THREAD thread) {
  if (!Enter()) return;

  int written = 0;
  if (Store(text, length, written)) {
    // The store is settled before the sends, so a patch that loops an outlet
    // back into the inlet finds the elements it is being handed — and finds the
    // guard taken, which is what stops it recursing on the audio thread.
    Emit(written, thread);
  } else {
    // Refused whole, and silent with it: the output *is* the incoming elements,
    // so firing the outlets with the values they already held would present
    // stale state as the list that just arrived. A bang is how a patch asks for
    // the held set on purpose.
    CountDrop();
  }

  Leave();
}

// ─── the inlet ──────────────────────────────────────────────────────────────

BANG_IN(UnpackBang) {
  // Max: "bang: Causes each stored item of a list to be sent out the
  // corresponding outlet." Every outlet, including ones no list has reached —
  // they carry their creation argument.
  if (inlet != 0) return;
  if (!Enter()) return;
  Emit((int)types.size(), thread);
  Leave();
}

INT_IN(UnpackInt) {
  // Max: "int: The number is sent out the left outlet." A one-item list, which
  // is exactly what the list path makes of it, so it goes through the same
  // store and the same coercion. Formatted into a stack buffer, through the
  // patcher's one spelling of a number.
  if (inlet != 0) return;
  char text[kExprValueTextMax];
  const int written = ExprFormatValue(ExprValue::Int(value), text, kExprValueTextMax);
  if (written <= 0) return;
  Take(text, (std::size_t)written, thread);
}

FLOAT_IN(UnpackFloat) {
  if (inlet != 0) return;
  char text[kExprValueTextMax];
  const int written = ExprFormatValue(ExprValue::Float(value), text, kExprValueTextMax);
  if (written <= 0) return;
  Take(text, (std::size_t)written, thread);
}

LIST_IN(UnpackList) {
  // Max: "list: Each item in the list (up to the number of outlets) is sent out
  // the outlet corresponding to its position in the list", and "anything:
  // Performs the same function as list" — which is why there is no message word
  // to strip here. `.unpack` has no `set`.
  if (inlet != 0) return;
  Take(value.c_str(), value.size(), thread);
}

#undef className
