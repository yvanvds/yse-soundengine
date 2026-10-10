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
      "The x value, and the inlet that acts — Max's 'the number specifies the x value of an x,y "
      "pair'. What it does depends on whether inlet 1 has armed a y: 'if a y value has been "
      "received in the right inlet, the two numbers are stored together in offer; otherwise, offer "
      "looks for an x value that matches the incoming number, sends out the corresponding y value, "
      "then deletes the stored pair'. So the same inlet stores when a y is waiting and withdraws "
      "when one is not, and storing spends the armed y so the next x is a query again. An x that "
      "matches nothing sends nothing: Max's 'if there is no x value stored in offer that matches "
      "the number received, offer does nothing'. A float is converted to an int. A list is Max's "
      "inlet distribution written on one cord, '<x> <y>' — the second element arms the y first, "
      "under Max's right-to-left delivery, so '60 72' stores the pair 60,72 while a bare '60' is a "
      "query. Further elements are ignored. 'clear' is Max's 'deletes the entire contents of "
      "offer': it sends nothing and it also disarms a y that was waiting, since a y carried across "
      "a clear would swallow the first x after it. A bang sends the y of every pair currently "
      "stored, oldest first, and deletes nothing. A store past 256 pairs is refused whole and "
      "silently, since this inlet may be the audio thread.";

  constexpr char kYInletDoc[] =
      "Arms one y value — Max's 'the number specifies a y value to be stored in offer. The next x "
      "value (int) received in the left inlet causes the two numbers to be stored together as an "
      "x,y pair'. It stores nothing and sends nothing by itself: only an x completes a pair. The "
      "armed y is a flag rather than a non-zero test, so 0 is a perfectly good y — it is the "
      "velocity a note-off carries, and the note streams this object was designed for are full of "
      "it. Arming a second y before any x arrives replaces the first: only one can be waiting. "
      "Ints, floats and a list whose leading token is a number all arm it; a float is converted to "
      "an int. The value is stored exactly as given, unclamped and never refused for being outside "
      "MIDI's 0-127 — this object is a keyed store, not a note formatter.";

  constexpr char kOutletDoc[] =
      "The y values leaving the store, always as ints. An x that matches a stored pair sends that "
      "pair's y and the pair is deleted — Max's 'when a pair is retrieved, it is deleted from the "
      "collection', which is the whole of 'one-time number pairs'. When the same x was stored more "
      "than once the newest pair is the one that answers, so a store and a withdrawal are an exact "
      "undo of each other. A bang sends the y of every stored pair one at a time, oldest first, "
      "and deletes none of them. Nothing else sends at all: arming a y, storing a pair, 'clear' "
      "and an x that matches nothing are all silent. The whole burst a bang sends is captured "
      "before the first send, so a patch that wires this outlet back into an inlet does not change "
      "the burst it is still receiving; a withdrawal likewise releases the store before it sends, "
      "so such a patch works rather than deadlocking, and cannot run away — every re-entrant query "
      "deletes the pair it answered.";

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

  ADD_DESCRIPTION(
      "Stores two ints as an x,y pair and hands the y back exactly once, by x — Max's 'offer', "
      "which 'stores one-time number pairs'. Max: 'store two ints as an x, y pair, and access them "
      "by x value. When a pair is retrieved, it is deleted from the collection.' One-time is the "
      "whole object: a pair answers one question and is then gone. Max's own discussion gives the "
      "use it was built for — 'offer was designed for use with algorithms that transform the pitch "
      "of an incoming note stream. By storing the original and transformed note together ... when "
      "the pitch of the note-on is changed, the transformed pitch can be retrieved when the "
      "note-off is received' — which is the bookkeeping problem every transposer and harmoniser "
      "has and nothing else in the patcher spells: a note-off carries the pitch the player "
      "released, not the one the synth is sounding, so something has to remember the mapping per "
      "note and forget it the moment it is used. Inlet 1 arms one y and does nothing else: Max's "
      "'the next x value received in the left inlet causes the two numbers to be stored together'. "
      "Inlet 0 is the x and is the inlet that acts, and which of two things it does depends only "
      "on whether a y is armed — 'if a y value has been received in the right inlet, the two "
      "numbers are stored together; otherwise, offer looks for an x value that matches the "
      "incoming number, sends out the corresponding y value, then deletes the stored pair'. "
      "Storing spends the armed y, so the next x is a query again, and an x that matches nothing "
      "sends nothing. The armed y is a flag rather than a non-zero test, because 0 is a perfectly "
      "good y and is the velocity a note-off carries. A float is converted to an int on either "
      "inlet. A list in inlet 0 is Max's inlet distribution on one cord and its order is '<x> <y>' "
      "— the second element arms the y first, under Max's right-to-left delivery — so '60 72' "
      "stores the pair and a bare '60' is a query. It is not .funbuff, .coll, .table or .bag, all "
      "four of which Max lists as related: .funbuff is the closest and is what a patch wants when "
      "the mapping is a function it keeps consulting, .coll and .table are read and re-read, and "
      ".bag has no addresses at all. This one is for a mapping that is true exactly once. Storing "
      "an x that is already there keeps both pairs rather than overwriting, which Max leaves open "
      "and which matters for the same key struck twice before its first note-off: overwriting "
      "would lose a note the synth was still sounding. A query then takes the newest matching "
      "pair, .bag's rule, so a store and a withdrawal are an exact undo of each other. A bang is "
      "Max's 'output every y-value received since the last clear message', read as the y of every "
      "pair currently held — the description's own 'when a pair is retrieved, it is deleted' rules "
      "out a second copy of everything ever handed in — sent one at a time, oldest first, and it "
      "deletes nothing: dumping what the store holds and spending it are different gestures and "
      "only one is recoverable. 'clear' is Max's 'deletes the entire contents of offer', silently, "
      "and it disarms a waiting y with it, since a y carried across a clear would swallow the "
      "first x after it. The store is bounded at 256 pairs, allocated whole at construction and "
      "never resized, with .value's non-blocking guard around it — .bag's and .coll's model and "
      "for their reason, since a message handler runs on whichever thread dispatched it and "
      "in-patcher delivery dispatches on the audio thread. A store past the capacity is refused "
      "whole and silently and spends the armed y anyway, a wrong answer being worse than a missing "
      "one; a loser of the guard drops rather than waiting; and the guard is never held across a "
      "send, so a patch that wires the outlet back into an inlet works rather than deadlocking and "
      "cannot run away, every re-entrant query deleting the pair it answered. Nothing on any "
      "message path allocates, locks or blocks. Like .flush and .sustain it opens no device and "
      "needs none, so it works on every platform. Calculate() does nothing, and nothing persists "
      "across a save: Max's offer takes no arguments, and a reloaded patch holding the mappings "
      "for notes that were sounding when it was saved would be holding answers to note-offs that "
      "are never coming.");
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
