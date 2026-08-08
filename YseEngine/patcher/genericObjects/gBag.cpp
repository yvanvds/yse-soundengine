#include "gBag.h"
#include "../math/gExprEval.h"
#include "../pListArgs.h"
#include "../pObjectList.hpp"
#include "../pSelector.h"

using namespace YSE::PATCHER;

#define className gBag

namespace {

  // The bounds of the token starting at or after `from`, or false when there is
  // none. Walked in place rather than through substr: this runs on whichever
  // thread the message arrived on.
  bool NextToken(const char* text, std::size_t length, std::size_t from, std::size_t& begin,
                 std::size_t& end) {
    begin = from;
    while (begin < length && IsSelectorSeparator(text[begin]))
      begin++;
    end = begin;
    while (end < length && !IsSelectorSeparator(text[end]))
      end++;
    return end > begin;
  }

  // Whether the `length` characters at `text` are exactly `word`.
  bool TokenIs(const char* text, std::size_t length, const char* word, std::size_t wordLength) {
    if (length != wordLength) return false;
    for (std::size_t i = 0; i < length; i++) {
      if (text[i] != word[i]) return false;
    }
    return true;
  }

  constexpr char kValueDoc[] =
      "The number to add or remove, and the hot inlet — which of the two happens is decided by the "
      "flag on inlet 1, Max's 'the number is either added to or deleted from the collection ... "
      "depending on the number in the right inlet'. A float is converted to an int. Neither adding "
      "nor removing sends anything: Max's 'no output is triggered by a number received in either "
      "inlet'. A list is Max's list method, '<value> <flag>' — the first item behaves as though it "
      "had been sent to this inlet and the second as though it had been sent to inlet 1, so '60 1' "
      "adds 60 and '60 0' removes it, and the flag it carries stays behind for later bare numbers. "
      "Note the order: the value comes first and the flag second, which is Max's and is the "
      "reverse "
      "of the shorthand in issue #495. A bang sends the whole collection out the outlet one number "
      "at a time, newest first. 'cut' sends the oldest number and deletes it, 'length' reports how "
      "many numbers are stored, and 'clear' empties the collection. An add past 256 numbers is "
      "refused whole and silently, since this inlet may be the audio thread.";

  constexpr char kFlagDoc[] =
      "Whether the next number on inlet 0 is added or removed — Max's 'if non-zero, the number "
      "received in the left inlet is added to the bag; if 0, the number is deleted from the "
      "collection'. A float is converted to an int first, so 0.5 is a 0 and means remove. Cold: "
      "setting it sends nothing and changes nothing already stored. It starts at 0, which Max does "
      "not document either way and is the conservative choice — a value arriving before the patch "
      "has said what to do with it does not silently join the collection. Run-time state, so it "
      "does not survive a save.";

  constexpr char kOutletDoc[] =
      "Numbers leaving the collection, always as ints. A bang sends every stored number one at a "
      "time in Max's 'reverse order from that in which they were stored', so the newest comes out "
      "first; 'cut' sends the single oldest one and takes it out of the collection; 'length' sends "
      "the number of entries. Adding and removing send nothing at all. The whole burst a bang "
      "sends "
      "is captured before the first send, so a patch that wires this outlet back into the inlet "
      "does not change the burst it is still receiving.";

} // namespace

CONSTRUCT() {
  // The duplicate flag is read for its presence, so the callbacks are what turn
  // an argument into the mode — and what returns the object to Max's
  // no-argument shape when SetParams("") clears it.
  REG_PARM_CLEAR;
  REG_PARM_PARSE;

  ADD_PARAM(duplicateArg);

  // Inlet 0 is the value and is hot; inlet 1 is the flag and is cold. Max's
  // split, and the object's whole interface.
  ADD_IN_0;
  REG_BANG_IN(BangIn);
  REG_INT_IN(IntIn);
  REG_FLOAT_IN(FloatIn);
  REG_LIST_IN(ListIn);

  ADD_IN_1;
  REG_INT_IN(IntIn);
  REG_FLOAT_IN(FloatIn);

  // Everything that leaves is a number: Max's bag stores ints and hands back
  // ints, and `length` is an int too.
  ADD_OUT_INT;

  // The whole table, taken once here on the control thread. Nothing on a
  // message path ever resizes it, which is what makes an add from a rendering
  // graph allocation-free.
  entries.resize(MAX_ENTRIES);

  ADD_DESCRIPTION(
      "Stores an unordered collection of numbers that a patch adds to and removes from — Max's "
      "bag, which 'stores and manages a collection of numbers' that 'you can add to or delete an "
      "integer from ... as well as report its contents'. A multiset, and the classic use is the "
      "one "
      "nothing else in the patcher can spell: tracking which notes are currently held, so a patch "
      "can answer what is sounding right now. A note-on adds the note number, the matching "
      "note-off "
      "removes it, and a bang hands back exactly the notes still down, which pairs directly with "
      "YSE's synth voice model. It is not .coll, which is a keyed store whose point is to look an "
      "address up; this has no addresses at all and answers only how many and which ones. Nor is "
      "it "
      ".bucket, which remembers the last N values positionally. Inlet 0 is the value and is hot, "
      "inlet 1 is the flag and is cold: non-zero adds the number, 0 removes it, and a float is "
      "converted to an int on either inlet. A list is Max's list method and its order is '<value> "
      "<flag>' — Max's 'the first list item was sent to the left inlet and the second list item "
      "was "
      "sent to the right inlet' — so 60 1 adds 60 and 60 0 removes it, the reverse of the "
      "shorthand "
      "in the issue; a patcher whose argument order is the reverse of Max's would be a trap for "
      "every patch brought across. The flag starts at 0, which Max does not document and is the "
      "conservative choice, and the list form sets it before applying the value. Any creation "
      "argument turns on duplicate entries: Max's 'bag with any argument maintains multiple "
      "entries "
      "with the same item; otherwise it holds only one of each', and the argument is read for its "
      "presence rather than its value. Without it, adding a value already present is nothing at "
      "all and the entry keeps the position it had. A bang sends every stored number one at a time "
      "in Max's 'reverse order from that in which they were stored', newest first; 'cut' sends the "
      "oldest and deletes it; 'length' reports the count; 'clear' empties the collection. Adding "
      "and removing are silent, which is Max's 'no output is triggered by a number received in "
      "either inlet'. Which instance a remove takes when duplicates are on is the one thing Max "
      "leaves open: the newest is taken, so an add and a remove of the same value are an exact "
      "undo "
      "of each other and a held-note tracker unwinds in the order the keys came up. The store is "
      "bounded at 256 numbers, allocated whole at construction and never resized, with .value's "
      "non-blocking guard around it — .coll's model and for .coll's reason, since a copy-on-write "
      "GraphState publish assumes the writer is the control thread while a .bag is written by "
      "whichever thread its message arrived on and in-patcher delivery dispatches on the audio "
      "thread. An add past the capacity is refused whole and silently, a loser of the guard drops "
      "rather than waiting, and the guard is never held across a send: a bang captures the whole "
      "collection into a stack array first, .bucket's trick, so a patch looping the outlet back "
      "into the inlet cannot change the burst it is still receiving. Calculate() does nothing. The "
      "duplicate flag survives a save because it is a creation argument; the contents deliberately "
      "do not, since Max's bag has no 'save data with patcher' flag — that is coll's — and a "
      "reloaded patch holding the notes that were down when it was saved would be holding notes "
      "nothing is sounding. Not ported: Max's 'send <receive-name>', which redirects a bang's "
      "output to receive objects by name, deferred as issue #685.");
  ADD_CATEGORY(pCategory::GENERIC);
  INLET_DOC(0, "value", kValueDoc, "any int");
  INLET_DOC(1, "flag", kFlagDoc, "0 or non-zero");
  OUTLET_DOC(0, "out", kOutletDoc, "any int");
  PARAM_DOC("duplicates", "",
            "Any argument at all turns on duplicate entries — Max's 'the presence of any symbol "
            "argument causes the bag to store duplicate values'. The argument is read for its "
            "presence and never for its value. With no argument the collection holds only one of "
            "each number at a time.",
            "any symbol");
}

// ─── parameters ───────────────────────────────────────────────────────────────

PARM_CLEAR() {
  // Parameters::Set leaves a STRING parameter alone when there are no arguments
  // at all, so the no-argument shape has to be restored here or `SetParams("")`
  // would keep the previous object's duplicate mode.
  duplicateArg.clear();
  duplicates = false;
}

PARM_PARSE() {
  duplicates = !duplicateArg.empty();
}

// ─── the store ────────────────────────────────────────────────────────────────

void gBag::Insert(int value) {
  // Max: "otherwise it holds only one of each number at a time". The entry that
  // is already there keeps the position it had — "holds only one of each" is a
  // statement about the contents, not about their order.
  if (!duplicates) {
    for (std::size_t i = 0; i < count; i++) {
      if (entries[i] == value) return;
    }
  }

  // Full: refused rather than grown, since growing the table would allocate on
  // whichever thread the message arrived on.
  if (count >= MAX_ENTRIES) return;

  entries[count] = value;
  count++;
}

void gBag::Remove(int value) {
  // The newest matching instance, walking back from the new end. Max leaves the
  // choice open and it is observable once duplicates are on; taking the newest
  // makes an add and a remove of the same value an exact undo of each other,
  // which is what a held-note tracker needs to unwind in key order.
  for (std::size_t i = count; i > 0; i--) {
    if (entries[i - 1] != value) continue;
    for (std::size_t j = i - 1; j + 1 < count; j++) {
      entries[j] = entries[j + 1];
    }
    count--;
    return;
  }
}

void gBag::Apply(int value) {
  storeGuard guard(busy);
  if (!guard.Held()) return;
  if (addMode.load(std::memory_order_relaxed)) {
    Insert(value);
  } else {
    Remove(value);
  }
}

std::size_t gBag::CaptureNewestFirst(int* out) const {
  storeGuard guard(busy);
  if (!guard.Held()) return 0;
  // Newest first: Max's "in reverse order from that in which they were stored".
  for (std::size_t i = 0; i < count; i++) {
    out[i] = entries[count - 1 - i];
  }
  return count;
}

// ─── commands ─────────────────────────────────────────────────────────────────

bool gBag::HandleCommand(const char* word, std::size_t length, YSE::THREAD thread) {
  if (TokenIs(word, length, "clear", 5)) {
    storeGuard guard(busy);
    if (!guard.Held()) return true;
    // Only `count` says which entries are live, so clearing is O(1) and the
    // storage a later add needs is still there.
    count = 0;
    return true;
  }

  if (TokenIs(word, length, "cut", 3)) {
    // Max: "sends out the oldest (earliest received) number stored in the bag
    // object, and deletes it from the bag" — a pop from the old end, which is
    // index 0.
    int oldest = 0;
    bool have = false;
    {
      storeGuard guard(busy);
      if (!guard.Held()) return true;
      if (count > 0) {
        oldest = entries[0];
        for (std::size_t i = 0; i + 1 < count; i++) {
          entries[i] = entries[i + 1];
        }
        count--;
        have = true;
      }
    }
    // Sent with the guard released: holding it across a synchronous fan-out
    // would make a patch that wires the outlet back into this object's inlet
    // lose its own message to the guard it is still holding.
    if (have) outputs[0].SendInt(oldest, thread);
    return true;
  }

  if (TokenIs(word, length, "length", 6)) {
    std::size_t live = 0;
    {
      storeGuard guard(busy);
      if (!guard.Held()) return true;
      live = count;
    }
    outputs[0].SendInt((int)live, thread);
    return true;
  }

  return false;
}

// ─── inlets ───────────────────────────────────────────────────────────────────

BANG_IN(BangIn) {
  (void)inlet;
  // Captured whole before the first send — .bucket's stack array, and for
  // .bucket's reason: a member buffer would be overwritten by exactly the
  // re-entrant burst the capture exists to protect against. 1 KB of stack is
  // the cheapest honest way to make each burst independent.
  int burst[MAX_ENTRIES];
  const std::size_t written = CaptureNewestFirst(burst);
  for (std::size_t i = 0; i < written; i++) {
    outputs[0].SendInt(burst[i], thread);
  }
}

INT_IN(IntIn) {
  if (inlet == 1) {
    addMode.store(value != 0, std::memory_order_relaxed);
    return;
  }
  Apply(value);
}

FLOAT_IN(FloatIn) {
  // Max's float method is "converted to int", on both inlets.
  const int truncated = ExprToInt(value);
  if (inlet == 1) {
    addMode.store(truncated != 0, std::memory_order_relaxed);
    return;
  }
  Apply(truncated);
}

LIST_IN(ListIn) {
  (void)inlet;
  const char* text = value.c_str();
  const std::size_t length = value.size();

  std::size_t begin = 0;
  std::size_t end = 0;
  if (!NextToken(text, length, 0, begin, end)) return;

  // A command first. Only three words are reserved and none of them is a
  // number, so nothing this inlet legitimately carries can collide with one.
  if (HandleCommand(text + begin, end - begin, thread)) return;

  // Max documents no `anything` method for bag, so a message that does not
  // start with a number is not one of its messages and is ignored.
  float first = 0.f;
  if (!ReadNumericToken(text + begin, end - begin, first)) return;

  // Max's list method: "<value> <flag>", the first item as though it had been
  // sent to the left inlet and the second as though it had been sent to the
  // right. The flag is applied first, which is what right-to-left delivery
  // means, and it stays behind for later bare numbers.
  std::size_t flagBegin = 0;
  std::size_t flagEnd = 0;
  float flag = 0.f;
  if (NextToken(text, length, end, flagBegin, flagEnd) &&
      ReadNumericToken(text + flagBegin, flagEnd - flagBegin, flag)) {
    addMode.store(ExprToInt(flag) != 0, std::memory_order_relaxed);
  }

  // Anything past the second item is ignored: Max's list method reads "any list
  // composed of two numbers".
  Apply(ExprToInt(first));
}

// ─── diagnostics ──────────────────────────────────────────────────────────────

int gBag::ValueAt(std::size_t position) const {
  storeGuard guard(busy);
  if (!guard.Held()) return 0;
  if (position >= count) return 0;
  return entries[position];
}

bool gBag::Contains(int value) const {
  storeGuard guard(busy);
  if (!guard.Held()) return false;
  for (std::size_t i = 0; i < count; i++) {
    if (entries[i] == value) return true;
  }
  return false;
}

std::size_t gBag::CountOf(int value) const {
  storeGuard guard(busy);
  if (!guard.Held()) return 0;
  std::size_t found = 0;
  for (std::size_t i = 0; i < count; i++) {
    if (entries[i] == value) found++;
  }
  return found;
}

#undef className
