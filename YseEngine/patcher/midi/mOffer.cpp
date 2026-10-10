// `.offer` (issue #544). See mOffer.h for the design; this file is the grammar,
// the pair table and the one bit of state that decides store from withdrawal.
// No platform guard, deliberately — see the header.
#include "mOffer.h"

#include "../math/gExprEval.h"
#include "../pListArgs.h"
#include "../pObjectList.hpp"
#include "../pSelector.h"

using namespace YSE::PATCHER;

#define className mOffer

namespace {

  // The bounds of one whitespace-separated token, found in place: a substr here
  // would allocate on whichever thread the message arrived on, and that is
  // routinely the audio callback. `.bag`, `.sustain` and `.flush` read their
  // lists the same way.
  bool NextToken(const std::string& text, std::size_t from, std::size_t& begin, std::size_t& end) {
    begin = from;
    while (begin < text.size() && IsSelectorSeparator(text[begin]))
      begin++;
    end = begin;
    while (end < text.size() && !IsSelectorSeparator(text[end]))
      end++;
    return end > begin;
  }

  // A token compared against a command word, without building a string to do it.
  bool TokenIs(const std::string& text, std::size_t begin, std::size_t length, const char* word,
               std::size_t wordLength) {
    return length == wordLength && text.compare(begin, length, word, wordLength) == 0;
  }

  constexpr char kXInletDoc[] =
      "With a y armed, stores the pair x,y. Otherwise looks up x: sends the matching pair's y and "
      "deletes the pair. A list \"x y\" stores a pair; a bang sends every stored y; \"clear\" "
      "deletes all pairs.";

  constexpr char kYInletDoc[] =
      "Arms a y for the next x; sends nothing. A second y replaces the first. 0 is a valid y.";

  constexpr char kOutletDoc[] =
      "The y of a pair looked up by x, or every stored y, oldest first, on a bang.";

} // namespace

CONSTRUCT() {
  // Inlet 0 is the x and is hot; inlet 1 is the y and is cold. Max's split, and
  // the object's whole interface.
  ADD_IN_0;
  REG_BANG_IN(BangIn);
  REG_INT_IN(IntIn);
  REG_FLOAT_IN(FloatIn);
  REG_LIST_IN(ListIn);

  ADD_IN_1;
  REG_INT_IN(IntIn);
  REG_FLOAT_IN(FloatIn);
  REG_LIST_IN(ListIn);

  // One outlet, and everything that leaves it is an int: Max's offer stores ints
  // and hands ints back.
  ADD_OUT_INT;

  // No creation arguments — Max's offer has none, so there is nothing for a save
  // to carry.

  // The whole table, taken once here on the control thread. Nothing on a message
  // path ever resizes it, which is what makes a store from a rendering graph
  // allocation-free.
  entries.resize(MAX_PAIRS);

  ADD_DESCRIPTION("Stores x,y pairs of ints and hands each y back once: an x with no y waiting "
                  "looks up its pair, sends the y and deletes the pair.");
  ADD_CATEGORY(pCategory::MIDI);

  INLET_DOC(0, "x", kXInletDoc, "int, float, list '<x> <y>', bang, 'clear'");
  INLET_DOC(1, "y", kYInletDoc, "any int");
  OUTLET_DOC(0, "y", kOutletDoc, "any int");
}

// ─── the store ────────────────────────────────────────────────────────────────

void mOffer::Insert(int x, int y) {
  // Full: refused rather than grown, since growing the table would allocate on
  // whichever thread the message arrived on.
  if (count >= MAX_PAIRS) return;

  // Duplicates of an existing x are kept. Overwriting would be shorter and it
  // would lose a note — see the class notes.
  entries[count].x = x;
  entries[count].y = y;
  count++;
}

bool mOffer::Withdraw(int x, int& y) {
  // The newest matching pair, walking back from the new end. Max leaves the
  // choice open and it is observable once the same x has been stored twice;
  // taking the newest makes a store and a withdrawal an exact undo of each
  // other, which is what a re-struck key needs to unwind in the order it came
  // down.
  for (std::size_t i = count; i > 0; i--) {
    if (entries[i - 1].x != x) continue;
    y = entries[i - 1].y;
    for (std::size_t j = i - 1; j + 1 < count; j++) {
      entries[j] = entries[j + 1];
    }
    count--;
    return true;
  }
  return false;
}

void mOffer::ArmY(int y) {
  storeGuard guard(busy);
  if (!guard.Held()) {
    dropped.fetch_add(1, std::memory_order_relaxed);
    return;
  }
  // Max: "the number specifies a y value to be stored in offer". Only one can
  // wait, so a second y before any x replaces the first.
  pendingY = y;
  hasPendingY = true;
}

void mOffer::Apply(int x, YSE::THREAD thread) {
  int y = 0;
  bool send = false;
  {
    storeGuard guard(busy);
    if (!guard.Held()) {
      dropped.fetch_add(1, std::memory_order_relaxed);
      return;
    }

    if (hasPendingY) {
      // Max: "if a y value has been received in the right inlet, the two numbers
      // are stored together in offer". The y is spent either way — a refused
      // store that kept it would pair it with the *next* x, which is a wrong
      // answer where dropping it is merely a missing one.
      Insert(x, pendingY);
      hasPendingY = false;
      pendingY = 0;
      return;
    }

    // Max: "offer looks for an x value that matches the incoming number, sends
    // out the corresponding y value, then deletes the stored pair" — and does
    // nothing at all when there is no match.
    send = Withdraw(x, y);
  }

  // Sent with the guard released: holding it across a synchronous fan-out would
  // make a patch that wires the outlet back into this object's inlet lose its
  // own message to the guard it is still holding.
  if (send) outputs[0].SendInt(y, thread);
}

void mOffer::ClearStore() {
  storeGuard guard(busy);
  if (!guard.Held()) {
    dropped.fetch_add(1, std::memory_order_relaxed);
    return;
  }
  // Only `count` says which entries are live, so clearing is O(1) and the
  // storage a later store needs is still there. The waiting y goes with it —
  // see the class notes.
  count = 0;
  hasPendingY = false;
  pendingY = 0;
}

std::size_t mOffer::CaptureOldestFirst(int* out) const {
  storeGuard guard(busy);
  if (!guard.Held()) {
    dropped.fetch_add(1, std::memory_order_relaxed);
    return 0;
  }
  // Oldest first: Max's bang reads "every y-value received", which is the order
  // they were received in. Deliberately the opposite of `.bag`'s newest-first
  // bang, and spelled out in both objects for that reason.
  for (std::size_t i = 0; i < count; i++) {
    out[i] = entries[i].y;
  }
  return count;
}

// ─── inlets ───────────────────────────────────────────────────────────────────

BANG_IN(BangIn) {
  (void)inlet;
  // Captured whole before the first send — `.bucket`'s and `.bag`'s stack array,
  // and for their reason: a member buffer would be overwritten by exactly the
  // re-entrant burst the capture exists to protect against. 1 KB of stack is the
  // cheapest honest way to make each burst independent.
  int burst[MAX_PAIRS];
  const std::size_t written = CaptureOldestFirst(burst);
  for (std::size_t i = 0; i < written; i++) {
    outputs[0].SendInt(burst[i], thread);
  }
}

INT_IN(IntIn) {
  if (inlet == 1) {
    ArmY(value);
    return;
  }
  Apply(value, thread);
}

FLOAT_IN(FloatIn) {
  // Max converts a float sent to an int inlet; this object stores ints and
  // nothing else. ExprToInt rather than a cast: a value outside the int range is
  // undefined behaviour to cast.
  const int truncated = ExprToInt(value);
  if (inlet == 1) {
    ArmY(truncated);
    return;
  }
  Apply(truncated, thread);
}

LIST_IN(ListIn) {
  std::size_t begin = 0;
  std::size_t end = 0;
  if (!NextToken(value, 0, begin, end)) return;
  const std::size_t length = end - begin;

  if (inlet == 1) {
    float number = 0.f;
    if (!ReadNumericToken(value.c_str() + begin, length, number)) return;
    ArmY(ExprToInt(number));
    return;
  }

  // Max's one command, left inlet only and a word rather than a number, so it
  // can never be mistaken for an x.
  if (TokenIs(value, begin, length, "clear", 5)) {
    ClearStore();
    return;
  }

  float x = 0.f;
  if (!ReadNumericToken(value.c_str() + begin, length, x)) return;

  // Max's list distributes across the inlets right to left, so a second element
  // is the y and it is armed *before* the x is applied — exactly as if it had
  // arrived at the right inlet first. That is what makes '60 72' one cord's way
  // of storing a pair. Further elements are ignored.
  std::size_t secondBegin = 0;
  std::size_t secondEnd = 0;
  if (NextToken(value, end, secondBegin, secondEnd)) {
    float y = 0.f;
    if (ReadNumericToken(value.c_str() + secondBegin, secondEnd - secondBegin, y)) {
      ArmY(ExprToInt(y));
    }
  }

  Apply(ExprToInt(x), thread);
}

// ─── diagnostics ──────────────────────────────────────────────────────────────

std::size_t mOffer::Count() const {
  storeGuard guard(busy);
  if (!guard.Held()) return 0;
  return count;
}

bool mOffer::HasPendingY() const {
  storeGuard guard(busy);
  if (!guard.Held()) return false;
  return hasPendingY;
}

int mOffer::PendingY() const {
  storeGuard guard(busy);
  if (!guard.Held()) return 0;
  return hasPendingY ? pendingY : 0;
}

int mOffer::XAt(std::size_t position) const {
  storeGuard guard(busy);
  if (!guard.Held()) return 0;
  if (position >= count) return 0;
  return entries[position].x;
}

int mOffer::YAt(std::size_t position) const {
  storeGuard guard(busy);
  if (!guard.Held()) return 0;
  if (position >= count) return 0;
  return entries[position].y;
}

bool mOffer::Contains(int x) const {
  storeGuard guard(busy);
  if (!guard.Held()) return false;
  for (std::size_t i = 0; i < count; i++) {
    if (entries[i].x == x) return true;
  }
  return false;
}

std::size_t mOffer::CountOf(int x) const {
  storeGuard guard(busy);
  if (!guard.Held()) return 0;
  std::size_t found = 0;
  for (std::size_t i = 0; i < count; i++) {
    if (entries[i].x == x) found++;
  }
  return found;
}

#undef className
