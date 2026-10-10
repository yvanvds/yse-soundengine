#include "gToggle.h"
#include "../math/gExprEval.h"
#include "../pListArgs.h"
#include "../pObjectList.hpp"
#include "../pSelector.h"
#include <cstddef>

using namespace YSE::PATCHER;

#define className gToggle

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

  // Whether the `length` characters at `text` are exactly `word`, compared in
  // place for the same reason the tokens are walked in place.
  bool TokenIs(const char* text, std::size_t length, const char* word, std::size_t wordLength) {
    if (length != wordLength) return false;
    for (std::size_t i = 0; i < length; i++) {
      if (text[i] != word[i]) return false;
    }
    return true;
  }

  // Reads a token as a switch state: the words "on" / "off" — the exact
  // strings GetGuiValue() produces, which is the half the settable promise
  // requires — or any number, where anything but 0 is on, the way the whole
  // patcher reads a switch. False for anything else.
  bool ReadSwitchToken(const char* text, std::size_t length, bool& out) {
    if (TokenIs(text, length, "on", 2)) {
      out = true;
      return true;
    }
    if (TokenIs(text, length, "off", 3)) {
      out = false;
      return true;
    }
    float number = 0.f;
    if (ReadNumericToken(text, length, number)) {
      out = number != 0.f;
      return true;
    }
    return false;
  }

} // namespace

CONSTRUCT() {
  ADD_IN_0;
  REG_INT_IN(SetValue);
  REG_BANG_IN(Bang);
  REG_LIST_IN(SetList);

  ADD_OUT_INT;

  value = false;

  ADD_DESCRIPTION("Latching on/off toggle. Int sets the state directly (0 = off, non-zero = on); "
                  "bang flips it. Emits 0 or 1 every calculate tick. Settable per the GUI value "
                  "protocol (issues #551/#846): a one-token list on inlet 0 is the whole state - "
                  "'on' or 'off', the exact string GetGuiValue() produces, or a number read like "
                  "the int - and 'set 0 <value>' writes the one cell; both set the state "
                  "absolutely, never flip, which is what lets .preset capture and restore the "
                  "toggle.");
  ADD_CATEGORY(pCategory::GUI);
  INLET_DOC(0, "set/toggle",
            "Int sets state (0 = off, !=0 = on); bang flips the current state. A one-token list "
            "- 'on', 'off' or a number - is the whole state and 'set 0 <value>' the cell write; "
            "both set absolutely, never flip.",
            "0 or 1");
  OUTLET_DOC(0, "out", "Current state — 0 or 1.", "0 or 1");
}

INT_IN(SetValue) {
  if (value == 0)
    this->value = false;
  else
    this->value = true;
}

BANG_IN(Bang) {
  // Atomic flip. A plain `value = !value` is an atomic load followed by a
  // separate atomic store, so two concurrent bangs (GUI/timer thread racing
  // the audio thread) can both read the same state and collapse into a single
  // flip — a lost update (issue #197). compare_exchange makes it a real RMW;
  // the retry loop is lock-free and never allocates, so it is audio-thread
  // safe. std::atomic<bool> has no fetch_xor, hence the CAS.
  bool current = value.load(std::memory_order_relaxed);
  while (!value.compare_exchange_weak(current, !current, std::memory_order_relaxed)) {
    // `current` is refreshed with the latest value on failure; retry.
  }
}

LIST_IN(SetList) {
  (void)inlet;
  (void)thread;

  const char* text = value.c_str();
  const std::size_t length = value.size();

  std::size_t begin = 0;
  std::size_t end = 0;
  // An empty list addresses nothing and falls through to the re-send a bang
  // gives.
  if (!NextToken(text, length, 0, begin, end)) return;

  if (TokenIs(text + begin, end - begin, "set", 3)) {
    // "set <index> <value>" — issue #551's cell write. The index has to read
    // as the one cell there is; anything else — including a NaN, which fails
    // the compare — is dropped, never folded onto cell 0.
    std::size_t cursor = end;
    std::size_t cellBegin = 0;
    std::size_t cellEnd = 0;
    if (!NextToken(text, length, cursor, cellBegin, cellEnd)) return;
    float cell = 0.f;
    if (!ReadNumericToken(text + cellBegin, cellEnd - cellBegin, cell)) return;
    if (!(cell >= 0.f)) return;
    if (ExprToInt(cell) != 0) return;

    std::size_t valueBegin = 0;
    std::size_t valueEnd = 0;
    if (!NextToken(text, length, cellEnd, valueBegin, valueEnd)) return;
    bool state = false;
    if (!ReadSwitchToken(text + valueBegin, valueEnd - valueBegin, state)) return;
    this->value.store(state, std::memory_order_relaxed);
    return;
  }

  // The whole state — "on" / "off", which is exactly the string GetGuiValue()
  // produced, or a number read like the int inlet reads one. Written
  // absolutely: a restore sets the state, it never flips it. Anything else
  // addresses nothing and is dropped.
  bool state = false;
  if (!ReadSwitchToken(text + begin, end - begin, state)) return;
  this->value.store(state, std::memory_order_relaxed);
}

GUI_VALUE() {
  return value == 0 ? "off" : "on";
}

CALC() {
  outputs[0].SendInt(value == false ? 0 : 1, thread);
}
