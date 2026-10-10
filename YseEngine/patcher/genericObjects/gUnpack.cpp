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
      return "The item in this position, as an int; a float is truncated.";
    case gUnpack::unpackType::FLOAT:
      return "The item in this position, as a float.";
    case gUnpack::unpackType::SYMBOL:
    default:
      break;
    }
    return "The item in this position, unchanged, as the int, float or symbol it spells.";
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
      "Splits a list into its items and sends each one out its own outlet, right to left. Each "
      "creation argument sets an outlet's type and starting value. The reverse of .pack.");

  ADD_CATEGORY(pCategory::LIST);

  INLET_DOC(0, "in",
            "The list to split: the first item goes to the leftmost outlet, the second to the "
            "next, and so on. An int or float is a list of one. A bang sends the stored items out "
            "every outlet again.",
            "any");

  PARAM_DOC("elements", "0 0",
            "One argument per outlet, setting its type and starting value: a whole number makes an "
            "int outlet, a number with a decimal point or exponent a float outlet, anything else "
            "an outlet that passes items unchanged. Without arguments: two int outlets starting at "
            "0.",
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
