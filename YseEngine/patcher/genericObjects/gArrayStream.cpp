#include "gArrayStream.h"
#include "../math/gExprEval.h"
#include "../pObjectList.hpp"

#include <cstddef>
#include <string>

using namespace YSE::PATCHER;

#define className gArrayStream

gArrayStream::gArrayStream() : gArrayEndsBase() {
  // The value inlet, hot; the size inlet and the reference inlet, cold —
  // gArrayFill's arrangement: the ask on the left, configuration on the
  // right, with .zl stream's right-inlet window on the cold side.
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

  // The reference, always list text; the shortfall, always an int. Sent
  // right to left: the shortfall first, then the reference.
  ADD_OUT_LIST;
  ADD_OUT_INT;

  // After gArrayEndsBase's name — the second creation argument.
  ADD_PARAM(size);

  ADD_DESCRIPTION(
      "Collects incoming values into a sliding array — Max's array.stream on the name-addressed "
      "value model .array settled: the array is bound from the creation argument, "
      "\".array.stream <name> [<size>]\", because an array is addressed by name and never "
      "passed down a cord. Every value arriving on the value inlet is appended as the text that "
      "spells it, the oldest element sliding off the front once the array holds the window, so "
      "the array is always the last <size> values — the window an analysis patch runs its "
      "statistics over. A list is collected whole, in the order sent, every atom one step of "
      "the slide, or refused whole when any atom outruns 64 characters. The shortfall — how "
      "many values the window still needs — leaves the shortfall outlet after every collect "
      "that lands, and the array's reference leaves the reference outlet only when the window "
      "is full, .zl stream's rule: a partial window is announced by its shortfall, never by "
      "its reference. A bang re-announces without collecting, trimming the array to the window "
      "first. The size inlet stores a new window silently (1-256, refused rather than "
      "clamped); no window configured means every trigger is refused, counted. However many "
      "atoms one message carries, the whole collect is one hold of the store's guard.");
  ADD_CATEGORY(pCategory::GENERIC);
  INLET_DOC(0, "value",
            "An int, a float or a symbol is collected: appended to the bound array as the text "
            "that spells it (7.5 stays visibly a float), the oldest element sliding off the "
            "front first when the array already holds the window. A list is collected whole, "
            "in the order sent — every atom one step of the slide, one announcement per "
            "message — or refused whole, one counted refusal and nothing changed, when any "
            "atom outruns 64 characters. A bang re-announces without collecting, trimming the "
            "array to the window first — .zl stream's bang. \"array <name>\" does the same "
            "when it names the array bound by the creation argument — the message an .array's "
            "reference outlet emits on a bang, the family's gesture; a reference is an "
            "identity, not an element, so it is never collected into the data. Only the bound "
            "name can be recognised at all — any other list, \"array\"-leading included, is "
            "simply atoms. A non-finite float or a trigger while no window is configured is "
            "refused and counted rather than logged, since this inlet may be the audio "
            "thread.",
            "one or more atoms, each at most 64 characters");
  INLET_DOC(1, "size",
            "An int stores the window size, silently — the cold half of the idiom, .zl "
            "stream's right-inlet length on .array.fill's count inlet. 1 to the store's 256: "
            "0 is not a window, negative is malformed and past the store could never fill, so "
            "all three are refused before they are stored — refused, never clamped — and a "
            "trigger after the refusal slides the window it would have slid before it. A "
            "float truncates to an int first; a non-finite one is refused rather than quietly "
            "becoming size 0. Nothing shared moves here: a narrower window trims at the next "
            "trigger, under that trigger's hold.",
            "1-256");
  INLET_DOC(2, "array reference",
            "\"array <name>\" is accepted silently when it names the array bound by the "
            "creation argument, so a patch may wire the array's reference outlet across. It "
            "sets nothing — the binding is the creation argument, resolved on the control "
            "thread — and anything else is refused and counted.",
            "");
  OUTLET_DOC(0, "reference",
             "The bound array's reference, \"array <name>\", sent after a collect or a bang "
             "ONLY when the array holds the full window — .zl stream's rule, so the "
             "statistics wired downstream never run over a population the patch did not ask "
             "for. Sent after the shortfall outlet, Max's outlets firing right to left. A "
             "refused collect emits nothing, and an unnamed object stays silent — the values "
             "land in its private array, but there is no name to pass on.",
             "");
  OUTLET_DOC(1, "shortfall",
             "How many values the window still needs — 0 once it is full — sent first (right "
             "to left) after every collect or bang that lands, so a patch can watch the "
             "window fill and .sel 0 on it is the \"window ready\" edge. A refused trigger — "
             "a lost try-lock, no window configured, an atom no element can hold — emits "
             "nothing at all. An unnamed (private) array still counts: an answer is a value, "
             "not an identity.",
             "0-255");
  PARAM_DOC("name", "",
            "The array's shared name, addressed as \"<patcherName>.<name>\" — the sequence an "
            ".array of the same name in this patcher holds. Resolved once, on the control "
            "thread, which is why no message re-points it at run time. Empty streams into a "
            "private, empty array: the values land and the shortfall counts, but nothing "
            "shares them and no reference leaves.",
            "any identifier");
  PARAM_DOC("size", "0",
            "The window size — how many of the most recent values the array holds. 1 to the "
            "store's 256; absent (0) means unconfigured, and every trigger is refused, "
            "counted, until an int on the size inlet sets a window: malformed, not a miss. "
            "An out-of-range creation argument is refused at the trigger, never clamped.",
            "1-256");
}

// A re-parse must not leave half of the previous configuration standing:
// SetParams("") drops the stored size back to 0 — unconfigured — along with
// the name. gArrayPositionBase's rule.
PARM_CLEAR() {
  size.store(0, std::memory_order_relaxed);
  gArrayEndsBase::ClearParams();
}

void gArrayStream::ParamsChanged() {
  RefreshReference();
}

void gArrayStream::RefreshReference() {
  reference.clear();
  if (arrayName.empty()) return;
  reference.reserve(sizeof(kArrayReferenceWord) + arrayName.size());
  reference += kArrayReferenceWord;
  reference += ' ';
  reference += arrayName;
}

// ─── messages ─────────────────────────────────────────────────────────────────

BANG_IN(BangIn) {
  (void)inlet;
  AnnounceStored(thread);
}

INT_IN(IntIn) {
  if (inlet == 1) {
    // Stored silently — the cold half of the idiom.
    StoreSize(value);
    return;
  }
  pending.Clear();
  // AddInt cannot refuse a first atom, but the check keeps a formatting
  // failure counted rather than silently collecting nothing.
  if (!pending.AddInt(value)) {
    Refuse();
    return;
  }
  Collect(thread);
}

FLOAT_IN(FloatIn) {
  if (inlet == 1) {
    // Max's float method on an int attribute is "convert to int", .table's
    // precedent. The negated range test keeps a NaN or an infinity — which
    // ExprToInt folds to 0 — from quietly becoming size 0.
    if (!(value >= -2147483648.f && value < 2147483648.f)) {
      Refuse();
      return;
    }
    StoreSize(ExprToInt(value));
    return;
  }
  // A non-finite float has no spelling that reads back — ExprFormatValue
  // renders it as "0." — and collecting that would be silent corruption.
  // Refused, with the negated range test so a NaN takes this branch too.
  // gArrayEndsWriter's rule.
  if (!(value >= -3.402823466e38f && value <= 3.402823466e38f)) {
    Refuse();
    return;
  }
  pending.Clear();
  if (!pending.AddFloat(value)) {
    Refuse();
    return;
  }
  Collect(thread);
}

LIST_IN(ListIn) {
  if (inlet == 2) {
    // The reference inlet acknowledges the array it is already bound to and
    // nothing more — the binding is the creation argument, resolved on the
    // control thread, so there is nothing to set. gDictSlice's inlet rule.
    if (!ArrayReferenceNames(value, arrayName)) Refuse();
    return;
  }

  // The array's reference re-announces — the message its .array emits on a
  // bang, so wiring that outlet here gives the family's gesture. A reference
  // is an identity, not an element, so it is never collected into the data.
  // Only the bound name can be recognised at all — resolving an unrecognised
  // name means the registry's mutex, and this may be the audio thread — so
  // any other list is simply atoms.
  if (ArrayReferenceNames(value, arrayName)) {
    AnnounceStored(thread);
    return;
  }

  // Every atom one step of the slide, collected whole or refused whole — see
  // the class notes. AddTokens refusing means more atoms or characters than
  // a list carries, which no message of one-atom elements can outrun either.
  pending.Clear();
  if (pending.AddTokens(value) != 0 || pending.Empty()) {
    Refuse();
    return;
  }
  Collect(thread);
}

void gArrayStream::StoreSize(int newSize) {
  // 0 is not a window (it is the unconfigured default the creation argument
  // leaves), negative is malformed and past MAX_ELEMENTS could never fill.
  // All three are refused before they are stored, gArrayFill's count rule: a
  // trigger after the refusal slides the window it would have slid before
  // it. Refused, never clamped — the family's rule where .zl clamps.
  if (newSize < 1 || static_cast<std::size_t>(newSize) > MAX_ELEMENTS) {
    Refuse();
    return;
  }
  size.store(newSize, std::memory_order_relaxed);
}

// ─── the collect itself ───────────────────────────────────────────────────────

void gArrayStream::Collect(YSE::THREAD thread) {
  // Validated before the guard is taken, so the collect is whole-or-nothing:
  // an atom past its capacity refuses the whole message and changes nothing.
  const std::size_t count = pending.Size();
  for (std::size_t i = 0; i < count; i++) {
    if (pending.AtomLength(i) > ELEMENT_CAPACITY) {
      Refuse();
      return;
    }
  }

  // No window, no collect: 0 is the unconfigured default and out of range is
  // an argument only a creation string can plant — the inlet refuses one
  // before storing it. Malformed, not a miss, and never clamped.
  const int stored = size.load(std::memory_order_relaxed);
  if (stored < 1 || static_cast<std::size_t>(stored) > MAX_ELEMENTS) {
    Refuse();
    return;
  }
  const auto window = static_cast<std::size_t>(stored);

  std::size_t shortfall = 0;
  {
    const arrayStoreGuard guard(store->busy);
    if (!guard.Held()) {
      Refuse();
      return;
    }
    for (std::size_t i = 0; i < count; i++) {
      // The slide: the oldest elements fall off the front until there is
      // room inside the window, then the new value lands at the end — the
      // O(n) shift the class notes accept, bounded by MAX_ELEMENTS. The
      // while rather than an if is what absorbs a foreign writer that grew
      // the array past the window between messages.
      while (store->count >= window)
        ArrayEraseAt(*store, 0);
      if (!ArrayAppend(*store, pending.AtomText(i), pending.AtomLength(i))) {
        // Unreachable — the slide just made room and the atom was validated
        // — but counted rather than silent, since a partial collect is the
        // one thing this object promises not to do.
        Refuse();
        return;
      }
    }
    shortfall = window - store->count;
  }

  // Outside the guard on purpose: a send runs the whole downstream graph,
  // which may well act on this same array, and inside the guard that access
  // would be the one thing the try-lock drops.
  Announce(shortfall, thread);
}

void gArrayStream::AnnounceStored(YSE::THREAD thread) {
  // A bang and the reference gesture: re-announce without collecting — .zl
  // stream's bang, which re-sends the window without sliding it. The same
  // no-window refusal as a collect: an unconfigured object has no window to
  // announce.
  const int stored = size.load(std::memory_order_relaxed);
  if (stored < 1 || static_cast<std::size_t>(stored) > MAX_ELEMENTS) {
    Refuse();
    return;
  }
  const auto window = static_cast<std::size_t>(stored);

  std::size_t shortfall = 0;
  {
    const arrayStoreGuard guard(store->busy);
    if (!guard.Held()) {
      Refuse();
      return;
    }
    // Trimmed on a bang as well as on a collect — .zl's rule — so a window
    // narrowed live, or an array a foreign writer overgrew, is honest by the
    // time it is announced.
    while (store->count > window)
      ArrayEraseAt(*store, 0);
    shortfall = window - store->count;
  }

  Announce(shortfall, thread);
}

void gArrayStream::Announce(std::size_t shortfall, YSE::THREAD thread) {
  // The shortfall first — right to left, so the number has arrived wherever
  // it is wired by the time the reference triggers the family downstream —
  // and the reference only when it is 0: a partial window is announced by
  // its shortfall, never by its reference, .zl stream's rule. An unnamed
  // object still counts (an answer is a value, not an identity) but has no
  // name to pass on.
  outputs[1].SendInt(static_cast<int>(shortfall), thread);
  if (shortfall != 0) return;
  if (reference.empty()) return;
  outputs[0].SendList(reference, thread);
}
