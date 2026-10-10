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
      "Appends incoming values to a named array and keeps only the most recent ones: a "
      "sliding window of a set size. Sends the array's reference each time the window is "
      "full.");
  ADD_CATEGORY(pCategory::ARRAY);
  INLET_DOC(0, "value",
            "An int, float, word or list to append; once the window is full, the oldest "
            "elements drop off the front. A bang reports again without appending, and so does "
            "the bound array's reference.",
            "one or more atoms, each at most 64 characters");
  INLET_DOC(1, "size",
            "Sets the window size. A float is truncated; values outside 1-256 are refused.",
            "1-256");
  INLET_DOC(2, "array reference",
            "Accepts the bound array's reference \"array <name>\" and ignores it, so the "
            "array's reference outlet can be wired here. Anything else is refused.",
            "");
  OUTLET_DOC(0, "reference",
             "The array's reference \"array <name>\" after each message, but only when the "
             "window is full. Sent after outlet 1. Nothing is sent when the object has no name.",
             "");
  OUTLET_DOC(1, "shortfall",
             "How many more values the window needs before it is full; 0 when it is full. Sent "
             "after every value or bang.",
             "0-255");
  PARAM_DOC("name", "",
            "Name of the array to fill: the .array with this name in the same patcher. Empty "
            "uses a private array.",
            "any identifier");
  PARAM_DOC("size", "0",
            "The window size: how many of the latest values the array keeps. Without one, "
            "everything is refused until a size arrives on the middle inlet.",
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
