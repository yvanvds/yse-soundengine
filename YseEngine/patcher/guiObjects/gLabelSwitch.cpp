#include "gLabelSwitch.h"
#include "../math/gExprEval.h"
#include "../pSelector.h"
#include <cstddef>

using namespace YSE::PATCHER;

#define className gLabelSwitchBase

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

  // The next token that reads as a number, advancing `from` past it. Tokens that
  // are not numbers are *skipped* rather than ending the walk, which is
  // ExprParseFloatList's policy and the one the rest of the GUI family walks a
  // list with.
  bool NextNumber(const char* text, std::size_t length, std::size_t& from, float& out) {
    std::size_t begin = 0;
    std::size_t end = 0;
    while (NextToken(text, length, from, begin, end)) {
      from = end;
      if (ReadNumericToken(text + begin, end - begin, out)) return true;
    }
    return false;
  }

  // Whether the `length` characters at `text` are exactly `word`. The command
  // word is compared in place for the same reason the tokens are walked in
  // place: a list may arrive on the audio thread.
  bool TokenIs(const char* text, std::size_t length, const char* word, std::size_t wordLength) {
    if (length != wordLength) return false;
    for (std::size_t i = 0; i < length; i++) {
      if (text[i] != word[i]) return false;
    }
    return true;
  }

  constexpr char kInDoc[] =
      "Latching: a number sets the state (0 off, anything else on) and a bang flips it. "
      "Momentary: any number or bang is a press. In both modes a one-number list or "
      "'set 0 <value>' sets the state outright. Everything here sends.";

  constexpr char kStateOutDoc[] = "The state. Sent last.";

  constexpr char kLabelOutDoc[] =
      "The label, so a bank of controls wired into one object can say which one fired. Empty "
      "when the control has no label. Sent first.";

  constexpr char kLabelParamDoc[] =
      "An optional 'momentary' keyword, then the label. The words of the label are joined with "
      "single spaces.";

} // namespace

CONSTRUCT() {
  ADD_IN_0;
  REG_INT_IN(IntIn);
  REG_FLOAT_IN(FloatIn);
  REG_LIST_IN(ListIn);
  REG_BANG_IN(BangIn);

  ADD_OUT_INT; // 0: the state
  ADD_OUT_LIST; // 1: the label

  // The label and the mode are built by ShapeLabel(), so a saved
  // `.textbutton momentary play` comes back momentary and called "play". The
  // clear callback is what makes `SetParams("")` return the object to the
  // no-argument shape rather than leaving the previous label in place.
  REG_PARM_CLEAR;
  REG_PARM_PARSE;

  ADD_PARAM(creationArgs);

  // Also the shape ClearParams() restores: no label, latching, off.
  ShapeLabel();

  ADD_CATEGORY(pCategory::GUI);
  INLET_DOC(0, "state", kInDoc, "0 or 1");
  OUTLET_DOC(0, "state", kStateOutDoc, "0 or 1");
  OUTLET_DOC(1, "label", kLabelOutDoc, "the label, or empty");
  PARAM_DOC("label", "", kLabelParamDoc, "optional 'momentary', then any text");
}

void gLabelSwitchBase::Document(const char* summary) {
  ADD_DESCRIPTION(summary);
}

// ─── the label ────────────────────────────────────────────────────────────────

void gLabelSwitchBase::ShapeLabel() {
  // Rebuilt rather than patched: the keyword decides what every message means,
  // so the two have to be read together. Safe because every caller runs before
  // the object is wired or published — the constructor, and the two parameter
  // callbacks, which patcherImplementation::CreateObjectUnlocked runs before
  // AssignGraphIds. A *live* SetParams never reaches here on a published
  // object: registering the callbacks makes ParamsNeedRebuild() true, so #234
  // replaces the object instead.
  momentary = false;
  label.clear();

  // Parameters::Set splits on single spaces, so a run of them yields empty
  // tokens; an empty token is not part of a label.
  std::size_t index = 0;
  while (index < creationArgs.size() && creationArgs[index].empty())
    index++;

  if (index < creationArgs.size() && creationArgs[index] == "momentary") {
    momentary = true;
    index++;
  }

  for (; index < creationArgs.size(); index++) {
    if (creationArgs[index].empty()) continue;
    if (!label.empty()) label.push_back(' ');
    label += creationArgs[index];
  }

  // Explicitly at rest: an atomic that is merely default-constructed holds no
  // defined value under C++17, and "the control starts off" is what a fresh
  // object and a reloaded patch both have to mean.
  on.store(false, std::memory_order_relaxed);
}

PARM_CLEAR() {
  // Runs on the control thread before the parameter string is re-read, and is
  // the whole of `SetParams("")`: Parameters::Set returns without calling the
  // parse callback for an empty argument, so this has to leave the no-argument
  // object behind rather than one still holding the previous label.
  creationArgs.clear();
  ShapeLabel();
}

PARM_PARSE() {
  ShapeLabel();
}

// ─── the state ────────────────────────────────────────────────────────────────

void gLabelSwitchBase::Press() {
  on.store(true, std::memory_order_relaxed);
}

void gLabelSwitchBase::Store(float value) {
  // Anything but 0 is on, which is how the whole patcher reads a switch. A NaN
  // is not 0 and is therefore on; it cannot reach here from a creation
  // argument, only from a patch that computed one.
  on.store(value != 0.f, std::memory_order_relaxed);
}

// ─── inlets ───────────────────────────────────────────────────────────────────

BANG_IN(BangIn) {
  (void)inlet;
  (void)thread;

  if (momentary) {
    Press();
    return;
  }

  // Latching: an atomic flip. A plain `on = !on` is a load followed by a
  // separate store, so two concurrent bangs (a host thread racing the audio
  // thread) can both read the same state and collapse into a single flip — the
  // lost update of issue #197, and `gToggle`'s bug. compare_exchange makes it a
  // real read-modify-write; the retry loop is lock-free and never allocates, so
  // it is audio-thread safe. std::atomic<bool> has no fetch_xor, hence the CAS.
  bool current = on.load(std::memory_order_relaxed);
  while (!on.compare_exchange_weak(current, !current, std::memory_order_relaxed)) {
    // `current` is refreshed with the latest value on failure; retry.
  }
}

INT_IN(IntIn) {
  FloatIn((float)value, inlet, thread);
}

FLOAT_IN(FloatIn) {
  if (inlet != 0) return;
  (void)thread;

  // Momentary: a press, whatever the number is. That is `.b`'s reading of an
  // int, and the only one available to a control that holds an event rather
  // than a state — "the user clicked" cannot be spelled 0.
  if (momentary) {
    Press();
    return;
  }
  Store(value);
}

LIST_IN(ListIn) {
  // Registered on inlet 0 only, but routed anyway: a later inlet must never
  // start writing the state because someone added a handler above.
  if (inlet != 0) return;
  (void)thread;

  const char* text = value.c_str();
  const std::size_t length = value.size();

  std::size_t begin = 0;
  std::size_t end = 0;
  // An empty list addresses nothing and falls through to the re-send a bang
  // gives.
  if (!NextToken(text, length, 0, begin, end)) return;

  if (TokenIs(text + begin, end - begin, "set", 3)) {
    // "set <index> <value>" — issue #551's cell write. The keyword is what
    // tells it from the whole-state form, which for a scalar control is the
    // same single number.
    std::size_t cursor = end;
    float cell = 0.f;
    float cellValue = 0.f;
    if (!NextNumber(text, length, cursor, cell)) return;
    if (!NextNumber(text, length, cursor, cellValue)) return;
    // Range-checked rather than indexed, as the protocol requires: a cell
    // outside the state — or a NaN, which fails this compare — is dropped,
    // never folded onto the one cell there is. This is also what makes Max's
    // two-token `set <value>` a no-op rather than a misreading.
    if (!(cell >= 0.f)) return;
    if (ExprToInt(cell) != 0) return;
    Store(cellValue);
    return;
  }

  float number = 0.f;
  if (!ReadNumericToken(text + begin, end - begin, number)) {
    // A leading token that is not a number addresses nothing — Max's `settext`
    // and `text` land here, deliberately. Dropped, and the hot inlet re-sends.
    return;
  }

  // The whole state, which is exactly the string GetGuiValue() produced.
  // Further numbers are ignored, `.rslider`'s policy for a longer list. Written
  // absolutely in both modes: in momentary mode this is a host restoring the
  // pending-press flag, not a user pressing, which is why it is the one path
  // that can write a 0 there.
  Store(number);
}

// ─── the GUI value protocol (issue #551) ──────────────────────────────────────

GUI_VALUE() {
  if (momentary) {
    // Atomic read-and-clear. A plain `bool v = on; on = false;` is a load then
    // a separate store, so a press that lands between the two is silently
    // cleared and never reported — the lost press of issue #197, and
    // `gButton`'s bug. exchange() reads and clears in one indivisible step.
    return on.exchange(false, std::memory_order_relaxed) ? "1" : "0";
  }
  // Latching: a poll is a look, not a consume.
  return on.load(std::memory_order_relaxed) ? "1" : "0";
}

// ─── output ───────────────────────────────────────────────────────────────────

CALC() {
  // Sampled once, so both outlets describe the same state even if a message
  // arriving down one of the cords presses the control while this send is in
  // flight.
  const bool state = on.load(std::memory_order_relaxed);

  // Right to left, `.trigger`'s ordering guarantee: the label first, then the
  // state — so the name is in hand by the time the state lands on a hot inlet
  // downstream. The label is the object's own stored copy, which is sound
  // because it cannot change under a send.
  outputs[1].SendList(label, thread);
  outputs[0].SendInt(state ? 1 : 0, thread);
}

#undef className

// ─── the two renderings ───────────────────────────────────────────────────────

gLed::gLed() : gLabelSwitchBase() {
  Document("A labelled on/off indicator. It latches by default (a number sets it, a bang flips "
           "it) or registers presses with the 'momentary' keyword, and sends its state and its "
           "label.");
}

gTextButton::gTextButton() : gLabelSwitchBase() {
  Document("A button carrying a text label. It works exactly like .led; a host draws it as a "
           "labelled button instead of a lamp.");
}
