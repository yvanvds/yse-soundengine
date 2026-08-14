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
      "The state, and the only inlet - everything here emits, which is what a hot inlet in this "
      "patcher does, so a message that addresses nothing still re-sends. Latching (the default), "
      "an "
      "int or a float *is* the state: 0 turns it off and anything else turns it on, and a bang "
      "flips it. Momentary (the 'momentary' creation keyword), any int, float or bang is a press "
      "whatever its value, which is what .b does and what 'the user clicked' has to mean for a "
      "control with no state to be set to; the press stays lit until the next GUI poll consumes "
      "it. "
      "A list of one number is the whole state, issue #551's round trip - and in momentary mode "
      "that is deliberately not the same thing as an int, because an int is the user pressing "
      "while "
      "a whole-state write is a host restoring the flag, and only the second can be asked to write "
      "a 0. 'set 0 <value>' is #551's cell write; there is one cell, so any other index addresses "
      "nothing, and the value is stored absolutely rather than toggled. Any other leading token is "
      "dropped - in particular Max's 'settext' and 'text', which are not ported because the label "
      "is a creation parameter: a string written from a message path is exactly the write the "
      "parameter system itself refuses, since strings cannot be written allocation-free and are "
      "read on both threads once the object is published. Max's silent 'set <value>' is not ported "
      "either: 'set' is the cell write, which emits, and the two forms do not even coincide by "
      "accident - Max's carries no index, so a 'set 1' from a Max patch is a cell write with no "
      "value and is dropped rather than misread.";

  constexpr char kStateOutDoc[] =
      "The state, as an int - 0 or 1, and Max's only outlet on both objects. Latching, this is the "
      "state the last message left; momentary, it is 1 for the press that caused the send, and 0 "
      "only when a whole-state write cleared it. One sample of the state is taken at the top of "
      "the "
      "send, so a press arriving down one of the cords mid-send cannot make the two outlets "
      "describe two different states. Sent last, after outlet 1.";

  constexpr char kLabelOutDoc[] =
      "The control's label, as a list. Max has no such outlet on either object, and it is what "
      "makes this more than a decorated .t: a bank of labelled buttons into one .route or one .s "
      "needs the label to say which one fired, and without it every host would keep a second copy "
      "of the names it drew. It is the same outlet .umenu grew for the same reason. A control "
      "built "
      "with no label sends an empty list, which is honest - there is no name to send. The label is "
      "sent as the object's own stored copy rather than a new one, which is sound precisely "
      "because "
      "it is a creation parameter and cannot change under a send. Sent first, before outlet 0, so "
      "the name is in hand by the time the state lands on a hot inlet downstream.";

  constexpr char kLabelParamDoc[] =
      "An optional leading 'momentary' keyword, then the label. Everything after the keyword is "
      "the "
      "label, joined with single spaces, so 'filter cutoff' is one control called 'filter cutoff' "
      "rather than two arguments; a control given no label has an empty one and is then a .t a "
      "host "
      "draws differently. A label that is literally 'momentary' cannot be the first word - the "
      "word "
      "is spoken for, and it is one position away from working. Without the keyword the control "
      "latches (an int is the state, a bang flips it), which is .t's model; with it every input is "
      "a press that stays lit until the next GUI poll clears it, which is .b's. The keyword is "
      "offered on both .led and .textbutton rather than one each, because one press against one "
      "latch is a property of the value and not of the drawing, and this family's premise is that "
      "the value model is shared; both default to latching, which diverges from Max's textbutton "
      "on "
      "purpose, since a default that differed per name would break the one thing the family claims "
      "- that swapping the name changes only what a host draws. It is a creation argument and not "
      "Max's live 'mode' message because it changes what the GUI cell means, a state against an "
      "event. The label is a creation parameter and nothing changes it afterwards: not the GUI "
      "cell, which is live state a host polls and .preset stores, and not guiProperties, which the "
      "engine never reads while this object has to emit the label on outlet 1 - and which a #234 "
      "replacement inherits wholesale from the object it replaced, so a label mirrored there would "
      "be reverted by the very rebuild meant to change it. Registering it as a list parameter "
      "makes "
      "a live SetParams replace the object through the #234 graph swap rather than rewrite a "
      "string "
      "underneath the audio thread, which is what lets every path read it with no synchronisation "
      "at all. The live state is not a parameter and does not survive a save; GetGuiValue() is the "
      "form a host stores it in, and inlet 0 takes it straight back.";

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
  Document(
      "A labelled on/off indicator - Max's led, and one of the two renderings of the patcher's "
      "labelled switch (issue #557). .b and .t carry no name, so a host rendering a headless patch "
      "has nothing to title a control with and draws a grid of anonymous squares; this holds the "
      "same value and can say what it is. It and .textbutton are one implementation under two "
      "names "
      "- same inlet, same two outlets, same arguments, same messages - because the value model is "
      "a "
      "named on/off in both and only the widget a host draws differs: a lamp here, a pressable "
      "labelled rectangle there. They are not one object with a display parameter, .zl's answer to "
      "its own modes, because the rendering is not behaviour a patch drives: it never changes at "
      "run time, and a host reading the type is exactly how it learns which widget to build. "
      "Arguments are an optional leading 'momentary' keyword and then the label, joined with "
      "single "
      "spaces; without the keyword the control latches (an int is the state, a bang flips it) and "
      "with it every input is a press that stays lit until the next GUI poll consumes it, which is "
      "Max's led responding to a bang. The keyword is offered on both names because one press "
      "against one latch is a property of the value rather than of the drawing. The label is a "
      "creation parameter and nothing changes it afterwards - not the GUI cell, which is live "
      "state "
      "a host polls and .preset stores, and not guiProperties, which the engine never reads while "
      "this object has to emit the label on outlet 1, and which a #234 replacement inherits from "
      "the object it replaced. Registering it as a list parameter makes a live SetParams replace "
      "the object through the #234 graph swap rather than rewrite a string underneath the audio "
      "thread, which is why Max's settext and text are not ported: that write is precisely the one "
      "the parameter system itself refuses. Inlet 0 is hot and everything on it emits: an int or a "
      "float is the state (a press in momentary mode), a list of one number is the whole state and "
      "issue #551's round trip, 'set 0 <value>' is #551's cell write, and a bang flips or presses. "
      "Outlet 0 carries the state and outlet 1 the label; the two fire right to left. The GUI cell "
      "is '0' or '1' rather than .t's 'on' / 'off', and the object promises GuiValueIsSettable() - "
      "inlet 0 takes a number, so the string a host polls is the string it can push back, and "
      ".preset can store and restore a named switch. (.t has since joined that promise through "
      "issue #846, taking its own 'on' / 'off' back; .b stays out, its value being a "
      "consume-on-read press.) In "
      "momentary mode that poll is destructive: it reports and clears a pending press in one "
      "indivisible step, which is the lost-press fix of issue #197. Nothing about drawing is "
      "modelled - colours, blinktime, size, the whole attribute set - since the patcher is "
      "headless.");
}

gTextButton::gTextButton() : gLabelSwitchBase() {
  Document(
      "A button or toggle carrying a text label - Max's textbutton, and one of the two renderings "
      "of the patcher's labelled switch (issue #557). It holds the same value as .led, a named "
      "on/off, and shares its implementation exactly: same inlet, same two outlets, same "
      "arguments, "
      "same messages. What differs is only what a host draws - a pressable labelled rectangle "
      "rather than a lamp - which is why the two are separate names rather than one object with a "
      "display parameter: the rendering never changes at run time, and the type is how a host "
      "learns which widget to build. .b and .t carry no name, so a host rendering a headless patch "
      "had nothing to title a control with; the label is the point of this object, and it is on "
      "outlet 1 as well as in the parameters, so a bank of labelled buttons into one .route or one "
      ".s can say which one fired without the host keeping a second copy of the names it drew. "
      "Arguments are an optional leading 'momentary' keyword and then the label, joined with "
      "single "
      "spaces, so '.textbutton filter cutoff' is one control called 'filter cutoff'. Without the "
      "keyword the control latches, which is Max's mode 1; with it every input is a press that "
      "stays lit until the next GUI poll consumes it, which is Max's default. Latching is the "
      "default on both names on purpose, diverging from Max here, because the family's whole claim "
      "is that swapping the name changes only what a host draws - and a press with no label is "
      "already .b, one word away. The label is a creation parameter and nothing changes it "
      "afterwards, which is what lets every path read it with no synchronisation: registering it "
      "as "
      "a list parameter makes a live SetParams replace the object rather than rewrite a string "
      "underneath the audio thread, and Max's settext and text are not ported for that reason. "
      "Inlet 0 is hot and everything on it emits: an int or a float is the state (a press in "
      "momentary mode), a list of one number is the whole state and issue #551's round trip, 'set "
      "0 "
      "<value>' is #551's cell write, and a bang flips or presses. Max's silent 'set <value>' is "
      "not ported - 'set' is the cell write, which emits, and Max's form carries no index so it is "
      "dropped rather than misread. Outlet 0 carries the state and outlet 1 the label; the two "
      "fire "
      "right to left. The GUI cell is '0' or '1' rather than .b's 'on' / 'off', which is what lets "
      "this object promise GuiValueIsSettable(); in momentary mode the poll reports and clears a "
      "pending press in one indivisible step, the lost-press fix of issue #197, so a host that "
      "snapshots a press and restores it later replays it. Nothing about drawing is modelled - "
      "colours, fonts, rounding, texton / textoff, the whole attribute set - since the patcher is "
      "headless.");
}
