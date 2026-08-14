#include "gArrayFill.h"
#include "../math/gExprEval.h"
#include "../pObjectList.hpp"

#include <cstddef>
#include <cstring>
#include <string>

using namespace YSE::PATCHER;

#define className gArrayFill

gArrayFill::gArrayFill() : gArrayEndsBase() {
  // The value inlet, hot; the count inlet and the reference inlet, cold —
  // Max's own arrangement for array.fill (datum left, length right), which
  // is also .array.insert's: configuration on the right, the ask on the
  // left.
  ADD_IN_0;
  REG_BANG_IN(BangIn);
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

  // After gArrayEndsBase's name — the second and third creation arguments.
  ADD_PARAM(count);
  ADD_PARAM(value);

  ADD_DESCRIPTION(
      "Fills an array with a repeated value — Max's array.fill on the name-addressed value "
      "model .array settled: the array is bound from the creation argument, \".array.fill "
      "<name> [<count>] [<value>]\", because an array is addressed by name and never passed "
      "down a cord. A fill REPLACES the contents: the array becomes exactly count copies of "
      "the value, so a shorter fill shrinks it and a count of 0 clears it — which is what "
      "makes it the initialiser, sizing an array in one message so an index-addressed write "
      "pattern has positions to land on, since an index past the end is refused rather than "
      "growing the array. A bang fills with the stored count and value (count seeded by the "
      "second creation argument, moved silently by the count inlet; value the third creation "
      "argument, 0 when absent — Max's default); an int, float or symbol on the value inlet "
      "fills with the stored count of copies of that value at the moment it arrives and "
      "stores nothing. The count is bounded at the store's 256 elements and refused rather "
      "than truncated; the value is one atom of at most 64 characters. The whole fill is one "
      "hold of the store's guard, and a fill that lands emits the array's reference, so the "
      "family chains.");
  ADD_CATEGORY(pCategory::GENERIC);
  INLET_DOC(0, "value",
            "A bang fills the bound array with the stored count of copies of the stored value "
            "— the array becomes exactly that, whatever it held. An int or a float fills with "
            "the stored count of copies of THAT value at the moment it arrives, spelled the "
            "way it arrived (7.5 stays a float), and stores nothing — a bang afterwards fills "
            "with the author's value. A symbol arrives as a one-atom list, kept equivalent to "
            "the number. A non-finite float is refused: it has no spelling that reads back. A "
            "list of more than one atom is refused whole — the fill value is one atom, and a "
            "content list belongs to .array's own set message. \"array <name>\" fills with "
            "the stored count and value when it names the array bound by the creation "
            "argument — the message an .array's reference outlet emits on a bang, the "
            "family's gesture. Anything else is refused and counted rather than logged, "
            "since this inlet may be the audio thread.",
            "one atom, at most 64 characters");
  INLET_DOC(1, "count",
            "An int stores the count the next fill sizes the array to, silently — the cold "
            "half of the Max idiom, Max's right-inlet length. Negative is malformed (a count "
            "is a size, not a distance) and past the store's 256 could never land: both are "
            "refused before they are stored, so a bang after the refusal fills with what it "
            "would have filled before it. A float truncates to an int first; a non-finite "
            "one is refused rather than quietly becoming count 0.",
            "0-256");
  INLET_DOC(2, "array reference",
            "\"array <name>\" is accepted silently when it names the array bound by the "
            "creation argument, so a patch may wire the array's reference outlet across. It "
            "sets nothing — the binding is the creation argument, resolved on the control "
            "thread — and anything else is refused and counted.",
            "");
  OUTLET_DOC(0, "reference",
             "The bound array's reference, \"array <name>\", sent after a fill that landed — "
             "the way an array leaves an object on the value model, so wiring it onward "
             "chains the family: into .array.length it reports the new size. A refused fill "
             "emits nothing — a lost try-lock, an out-of-range count only a creation argument "
             "can plant, a value no element can hold — and an unnamed object stays silent: "
             "the fill happens, but there is no name to pass on.",
             "");
  PARAM_DOC("name", "",
            "The array's shared name, addressed as \"<patcherName>.<name>\" — the sequence an "
            ".array of the same name in this patcher holds. Resolved once, on the control "
            "thread, which is why no message re-points it at run time. Empty fills a private, "
            "empty array: the fill lands, but nothing shares it and no reference leaves.",
            "any identifier");
  PARAM_DOC("count", "0",
            "The initial stored count — how many copies a bang fills the array with before "
            "any int on the count inlet has moved it. 0, the default, makes a bare bang "
            "clear the array. Bounded at the store's 256 elements; an out-of-range creation "
            "argument is refused at the fill, counted, never clamped.",
            "0-256");
  PARAM_DOC("value", "0",
            "The stored fill value — what a bang repeats. One atom, at most 64 characters, "
            "spelled the way it will be stored (7 an int, 7. a float, anything else a "
            "symbol). Absent means 0 — Max's \"without any initial data, the array will be "
            "filled with 0s\". An inline value on the value inlet never rewrites this: the "
            "author's argument is the author's.",
            "one atom, at most 64 characters");

  // The default derivation, so an object that never sees a SetParams — an
  // unnamed one, banged as built — already fills with 0 rather than refusing
  // on the empty marker.
  RefreshStoredValue();
}

// A re-parse must not leave half of the previous configuration standing:
// SetParams("") — and the clear half of every Set() — drops the stored count
// back to 0 and the stored value back to its default along with the name.
// gArrayPositionBase's rule.
PARM_CLEAR() {
  count.store(0, std::memory_order_relaxed);
  value.clear();
  gArrayEndsBase::ClearParams();
}

void gArrayFill::ParamsChanged() {
  RefreshReference();
  RefreshStoredValue();
}

void gArrayFill::RefreshReference() {
  reference.clear();
  if (arrayName.empty()) return;
  reference.reserve(sizeof(kArrayReferenceWord) + arrayName.size());
  reference += kArrayReferenceWord;
  reference += ' ';
  reference += arrayName;
}

void gArrayFill::RefreshStoredValue() {
  // Control thread only — SetParams / SetParent / SetName, the places every
  // derivation from the creation arguments runs. An absent value fills with
  // 0 — Max's "without any initial data, the array will be filled with 0s" —
  // and an author's token no element can hold is left as length 0, which
  // Fill refuses, counted: refusal, never truncation. One token by
  // construction: Parameters::Set hands each parameter one
  // whitespace-separated token.
  if (value.empty()) {
    storedValue[0] = '0';
    storedValueLength = 1;
    return;
  }
  if (value.size() > ELEMENT_CAPACITY) {
    storedValueLength = 0;
    return;
  }
  std::memcpy(storedValue, value.data(), value.size());
  storedValueLength = value.size();
}

BANG_IN(BangIn) {
  (void)inlet;
  FillStored(thread);
}

INT_IN(IntIn) {
  if (inlet == 1) {
    // Stored silently — the cold half of the Max idiom.
    StoreCount(value);
    return;
  }
  // A number on the value inlet is the fill value at the moment it arrives,
  // stored nowhere — gArrayIndexMap's trigger rule: a bang afterwards fills
  // with the author's value, not the inline one. Rendered by ExprFormatValue
  // into the fixed pending list, so the patcher has one spelling of a
  // number. AddInt cannot refuse a first atom, but the check keeps a
  // formatting failure counted rather than silently filling with nothing.
  pending.Clear();
  if (!pending.AddInt(value)) {
    Refuse();
    return;
  }
  Fill(pending.AtomText(0), pending.AtomLength(0), thread);
}

FLOAT_IN(FloatIn) {
  if (inlet == 1) {
    // Max's float method on an int attribute is "convert to int", .table's
    // precedent. The negated range test keeps a NaN or an infinity — which
    // ExprToInt folds to 0 — from quietly becoming count 0.
    if (!(value >= -2147483648.f && value < 2147483648.f)) {
      Refuse();
      return;
    }
    StoreCount(ExprToInt(value));
    return;
  }
  // A non-finite float has no spelling that reads back — ExprFormatValue
  // renders it as "0." — and filling an array with that would be silent
  // corruption. Refused, with the negated range test so a NaN takes this
  // branch too. gArrayEndsWriter's rule.
  if (!(value >= -3.402823466e38f && value <= 3.402823466e38f)) {
    Refuse();
    return;
  }
  pending.Clear();
  if (!pending.AddFloat(value)) {
    Refuse();
    return;
  }
  Fill(pending.AtomText(0), pending.AtomLength(0), thread);
}

LIST_IN(ListIn) {
  if (inlet == 2) {
    // The reference inlet acknowledges the array it is already bound to and
    // nothing more — the binding is the creation argument, resolved on the
    // control thread, so there is nothing to set. gDictSlice's inlet rule.
    if (!ArrayReferenceNames(value, arrayName)) Refuse();
    return;
  }

  // The array's reference fills with the stored count and value — the
  // message its .array emits on a bang, so wiring that outlet here gives the
  // family's gesture. Only the bound name can be recognised at all —
  // resolving an unrecognised name means the registry's mutex, and this may
  // be the audio thread.
  if (ArrayReferenceNames(value, arrayName)) {
    FillStored(thread);
    return;
  }

  // A list spelling exactly one atom is the fill value it spells — how a
  // symbol arrives at all, and kept equivalent to the number so a
  // value-producing outlet still lands. Anything longer is refused whole:
  // the fill value is one atom (an element is one atom, the store's rule),
  // and Max's fill-with-these-contents list gesture deliberately does not
  // land here — a content list quietly re-read as <count> <value> would be
  // silent corruption of exactly the message a ported patch sends. Contents
  // belong to .array's own set. A reference naming an array this object is
  // not bound to arrives here too, and is refused with the rest.
  pending.Clear();
  if (pending.AddTokens(value) != 0 || pending.Size() != 1) {
    Refuse();
    return;
  }
  Fill(pending.AtomText(0), pending.AtomLength(0), thread);
}

void gArrayFill::StoreCount(int newCount) {
  // Negative is malformed — a count is a size, not a distance, so the
  // family's negative refusal applies where .array.rotate's amount inlet
  // deliberately waived it — and past MAX_ELEMENTS could never land. Both
  // are refused before they are stored, gArrayPositionBase's index rule: a
  // bang after the refusal fills with what it would have filled before it.
  if (newCount < 0 || static_cast<std::size_t>(newCount) > MAX_ELEMENTS) {
    Refuse();
    return;
  }
  count.store(newCount, std::memory_order_relaxed);
}

void gArrayFill::FillStored(YSE::THREAD thread) {
  // storedValueLength is 0 only for an author's value no element can hold —
  // Fill refuses it there, counted.
  Fill(storedValue, storedValueLength, thread);
}

void gArrayFill::Fill(const char* text, std::size_t length, YSE::THREAD thread) {
  // Validated before the guard is taken, so a fill is whole-or-nothing: a
  // value no element can hold refuses the whole ask and changes nothing.
  // Length 0 is the marker RefreshStoredValue leaves for exactly that.
  if (length == 0 || length > ELEMENT_CAPACITY) {
    Refuse();
    return;
  }
  // Only a creation argument can plant an out-of-range count — the inlet
  // refuses one before storing it. Malformed, not a miss, and never clamped.
  const int stored = count.load(std::memory_order_relaxed);
  if (stored < 0 || static_cast<std::size_t>(stored) > MAX_ELEMENTS) {
    Refuse();
    return;
  }
  const auto wanted = static_cast<std::size_t>(stored);

  {
    const arrayStoreGuard guard(store->busy);
    if (!guard.Held()) {
      Refuse();
      return;
    }
    // The array becomes exactly `wanted` copies of the value — a fill
    // replaces, never appends, see the class notes. Bounded assigns into
    // storage the store reserved at construction, the slots a shorter fill
    // vacates cleared behind the new count — ArrayEraseAt's hygiene,
    // gArrayPermuteBase's apply. All of it one hold, so the array is never
    // observable half-filled.
    const std::size_t old = store->count;
    for (std::size_t i = 0; i < wanted; i++)
      store->elements[i].assign(text, length);
    for (std::size_t i = wanted; i < old; i++)
      store->elements[i].clear();
    store->count = wanted;
  }
  Announce(thread);
}

void gArrayFill::Announce(YSE::THREAD thread) {
  // Outside the guard on purpose: a send runs the whole downstream graph,
  // which may well act on this same array, and inside the guard that access
  // would be the one thing the try-lock drops. An unnamed object has no name
  // to pass on — the fill happened, the announcement is simply empty.
  if (reference.empty()) return;
  outputs[0].SendList(reference, thread);
}
