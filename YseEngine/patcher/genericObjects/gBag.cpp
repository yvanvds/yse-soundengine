#include "gBag.h"
#include "../../internal/global.h"
#include "../../internal/namedBus.h"
#include "../math/gExprEval.h"
#include "../pListArgs.h"
#include "../pObjectList.hpp"
#include "../pSelector.h"
#include "../patcherImplementation.h"

using namespace YSE::PATCHER;

#define className gBag

namespace {

  using YSE::INTERNAL::Bus;
  using YSE::INTERNAL::BusValue;

  // The bus is owned by INTERNAL::Global() between init() and close(); skip
  // publishing outside that window so tests instantiating a patcher without
  // first calling `System::init()` keep working through the local path — the
  // gSend / gForward / gTable rule, and for the same reason.
  inline bool busAvailable() {
    return YSE::INTERNAL::Global().isActive();
  }

  // The bus truncates a published name at kNameCapacity while the in-patcher
  // PassData path does not, so the two would disagree about where an over-long
  // destination points. `send` refuses such a name outright; this keeps the limit
  // it refuses by pinned to the slot share of the patcher's address budget,
  // whose sum patcherImplementation.cpp asserts fits the bus (#921) —
  // gForward's assertion.
  static_assert(gBag::MAX_NAME_LENGTH == patcherImplementation::MAX_SLOT_NAME_LENGTH,
                "gBag::MAX_NAME_LENGTH must track the patcher's slot-name budget");

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

  // The two allocations `send` would otherwise need on the message path. Taken
  // here, on the control thread, for the longest name the object will accept;
  // RefreshBusPrefix() grows the address again once the patcher name it is
  // prefixed with is known — gForward's and gTable's pattern.
  sendName.reserve(MAX_NAME_LENGTH);
  busAddress.reserve(MAX_NAME_LENGTH);

  ADD_DESCRIPTION(
      "Holds a collection of numbers that a patch adds to and removes from, such as the notes "
      "currently held down. A bang sends every number, newest first.");
  ADD_CATEGORY(pCategory::COLLECTION);
  INLET_DOC(0, "value",
            "A number to add or remove, depending on the flag. Also bang, '<value> <flag>', "
            "'cut', 'length', 'clear' and 'send <name>'.",
            "any int");
  INLET_DOC(1, "flag",
            "Non-zero: numbers on the left inlet are added. 0: they are removed. Starts at 0.",
            "0 or non-zero");
  OUTLET_DOC(0, "out", "Stored numbers, on bang or 'cut', and the count on 'length'.", "any int");
  PARAM_DOC("duplicates", "",
            "Any argument lets the same number be stored more than once. Without one, each "
            "number is held at most once.",
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

// ─── the patcher's name, for `send` ───────────────────────────────────────────

// `parent` is a patcherImplementation by construction (the patcher hands itself
// to every object via SetParent); the cast mirrors the PassData call in SendTo.
void gBag::SetParent(pObject* newParent) {
  pObject::SetParent(newParent);
  RefreshBusPrefix();
}

void gBag::RefreshBusPrefix() {
  if (parent == nullptr) {
    busPrefix.clear();
  } else {
    auto* p = static_cast<patcherImplementation*>(parent);
    busPrefix = p->ScopedAddressPrefix();
  }
  // Control thread. Size the address for the longest name `send` will ever
  // accept under the current prefix, so the message path only refills it.
  busAddress.reserve(busPrefix.size() + MAX_NAME_LENGTH);
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

// ─── send ─────────────────────────────────────────────────────────────────────

void gBag::SendTo(const char* name, std::size_t nameLength, YSE::THREAD thread) {
  // Nothing but whitespace names nothing, and an over-long name would address one
  // receiver locally and a truncated one on the bus — gForward's rule, refused
  // silently since this may be the audio thread.
  if (nameLength == 0 || nameLength > MAX_NAME_LENGTH) return;
  if (parent == nullptr) return;

  // The name strings are members, and PassData delivers synchronously when this
  // is the audio thread, so a `send` that came back round into this object would
  // rewrite the name under the burst still using it. The second one drops — the
  // object's loser-of-the-guard discipline — which also bounds the recursion a
  // send-to-itself cycle would run at 1 KB of burst per frame.
  storeGuard reentry(sending);
  if (!reentry.Held()) return;

  // Max: "sends the result of a bang message", so this is BangIn's capture — a
  // bang's numbers in a bang's order, taken whole before the first send with the
  // store guard released again.
  int burst[MAX_ENTRIES];
  const std::size_t written = CaptureNewestFirst(burst);
  // An empty collection sends nothing, because that is what its bang sends.
  if (written == 0) return;

  // Both strings were reserved for this on the control thread, so refilling them
  // here allocates nothing.
  sendName.assign(name, nameLength);
  busAddress.assign(busPrefix);
  busAddress.append(sendName);

  auto* p = static_cast<patcherImplementation*>(parent);
  // CallingThread, not the tag: this send is reachable from a deferred delivery,
  // where T_GUI is carried on the audio callback and the bus's parked-publish
  // path takes a mutex. gSend.cpp carries the note (issue #690). Everything here
  // is an int, which the bus carries on both paths — only bang and list payloads
  // fall under its audio-thread drop contract. Asked once: the answer cannot
  // change part-way through a burst.
  const YSE::THREAD physical = p->CallingThread(thread);
  const bool toBus = busAvailable();

  for (std::size_t i = 0; i < written; i++) {
    // Outlet 0 is deliberately not touched: Max's "instead of out the bag
    // object's outlet".
    p->PassData(burst[i], sendName, thread);
    if (toBus) {
      Bus().publish(busAddress, BusValue{burst[i]}, physical);
    }
  }
}

// ─── commands ─────────────────────────────────────────────────────────────────

bool gBag::HandleCommand(const char* word, std::size_t length, const std::string& message,
                         std::size_t argOffset, YSE::THREAD thread) {
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

  if (TokenIs(word, length, "send", 4)) {
    // Max: "the word send, followed by the name of a receive object, sends the
    // result of a bang message to all receive objects with that name, instead of
    // out the bag object's outlet". One message, not a mode — the shape .table's
    // send (#498) already ports from the same family of objects.
    std::size_t begin = 0;
    std::size_t end = 0;
    // A bare `send` names nothing and does nothing: there is no mode to clear,
    // and the empty name is .forward's "not configured" rather than an address.
    if (!NextToken(message.c_str(), message.size(), argOffset, begin, end)) return true;
    // The first token only, which is what Max does with surplus arguments and
    // what a name has to be to match a .r at all — Parameters::Set tokenises a
    // creation argument the same way.
    SendTo(message.c_str() + begin, end - begin, thread);
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

  // A command first. Only four words are reserved and none of them is a
  // number, so nothing this inlet legitimately carries can collide with one.
  if (HandleCommand(text + begin, end - begin, value, end, thread)) return;

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
