#include "gTogEdge.h"
#include "../pListArgs.h"
#include "../pObjectList.hpp"
#include "gChange.h"
#include <cmath>
#include <cstddef>
#include <string>

using namespace YSE::PATCHER;

#define className gTogEdge

namespace {

  // The separators Parameters::Set and the list outlets use. Hand-rolled rather
  // than std::isspace, which reads locale state another thread may be mutating
  // and is undefined for a negative char.
  bool IsSeparator(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
  }

  // The first separator-delimited token of @p text, reported as the half-open
  // range [begin, end). Returns false when there is nothing there. A range
  // rather than a substring, because a substr would allocate on whichever
  // thread the message arrived on.
  bool LeadingToken(const std::string& text, std::size_t& begin, std::size_t& end) {
    begin = 0;
    while (begin < text.size() && IsSeparator(text[begin]))
      begin++;
    end = begin;
    while (end < text.size() && !IsSeparator(text[end]))
      end++;
    return end > begin;
  }

} // namespace

CONSTRUCT() {
  // One inlet, as in Max: numbers to watch, plus the bang that toggles and the
  // family's `reset`.
  ADD_IN_0;
  REG_FLOAT_IN(SetFloat);
  REG_INT_IN(SetInt);
  REG_BANG_IN(SetBang);
  REG_LIST_IN(SetList);

  // Max: "Out left outlet: bang" / "Out right outlet: bang". Bangs rather than
  // .change's int 1 — see "The two objects do not agree about the payload" in
  // the header for why that difference is deliberate and per-reference.
  ADD_OUT_BANG;
  ADD_OUT_BANG;

  // No parameters at all. Max: "Arguments: None", and the header sets out why
  // .change's `initial` argument does not transfer to an object with no value
  // outlet.

  ADD_DESCRIPTION(
      "Bangs the left outlet the moment a value becomes non-zero and the right outlet the moment "
      "it "
      "returns to zero, and says nothing in between — Max's togedge. It turns a continuous stream "
      "into the two moments a patch actually cares about, which is the clean way to derive note-on "
      "/ note-off style gating from a threshold or an envelope, and in a patcher whose message "
      "queue is bounded the silence between the edges is the point rather than a nicety: an "
      "envelope sampled at frame rate would otherwise deliver a message per frame for the whole "
      "time the gate is open. Only transitions are reported, never levels — Max: 'Otherwise, "
      "togedge sends no output' — so 5 followed by 9 is not an event, because both are non-zero "
      "and "
      "no zero crossing happened, and 0 followed by 0 is not an event either. An implementation "
      "that reported 'the value changed' rather than 'the value crossed zero' would bang the left "
      "outlet on every message of a rising envelope. The two outlets are therefore mutually "
      "exclusive and strictly alternate over the object's whole life, which is the property a "
      "downstream note-on / note-off pairing depends on, since two note-ons with no note-off "
      "between them is a stuck note. The object starts holding 0 and takes no creation argument, "
      "as "
      "Max's does not: the first non-zero number it ever receives is a rising edge, while a first "
      "0 "
      "is silent because 0 is what it already held. The .change object's 'initial' argument "
      "deliberately does not transfer, because that argument exists to keep a patch load from "
      "firing a value outlet for a parameter that has not moved and this object has no value "
      "outlet "
      "to fire — an edge that has not happened yet cannot be pre-declared. A bang is not a "
      "passthrough but the object's most surprising message: Max says it 'switches the value "
      "stored "
      "in togedge from 0 to non-zero, or vice versa, and reports the change by sending a bang out "
      "one of the outlets', so a bang toggles the stored value and then reports the transition it "
      "just made itself, and a stream of bangs comes out left, right, left, right. It composes "
      "with "
      "numeric input rather than living beside it, because both go through the same stored value, "
      "so a bang after the object was driven to 5 by a number bangs the right outlet and leaves "
      "the "
      "object holding 0. That is how a patch drives the same note-on / note-off pair from a single "
      "button. Both outlets carry a bang rather than the int 1 that .change's equivalent outlets "
      "send, which is what each Max reference page says and is defensible on its own terms: "
      ".change's edge outlets sit beside a value outlet where an int distinguishes an edge from "
      "nothing, whereas here the outlet's identity carries the whole meaning and a stray 1 would "
      "be "
      "a value nobody asked for on an outlet meant to drive a .trigger or a message box. The zero "
      "test itself is shared with .change rather than rewritten, because it has a corner two "
      "copies "
      "would drift apart on: -0.f == 0.f in IEEE-754, so a negative zero counts as zero on both "
      "sides. A non-finite number is ignored outright — no outlet fires and the stored value is "
      "untouched — rather than read as 0 the way the rest of the patcher reads it, because reading "
      "a NaN as 0 while the object holds 5 would bang the falling outlet and deliver a note-off "
      "for "
      "a note still being held, and storing it raw would leave the object in a state from which "
      "neither outlet could ever fire again. So the stored value is always finite, which is what "
      "guarantees the strict alternation cannot be broken by a value the patch did not choose. "
      "'reset' is the patcher family's word rather than a Max message and returns the object to a "
      "stored 0 silently; the silence is its whole reason for existing, since a patch that wants "
      "the gate re-armed cannot just send 0 without banging the falling outlet and delivering a "
      "note-off it did not mean. There is no 'set', as Max documents none. A symbol is ignored, "
      "zero-ness being a numeric predicate that a symbol neither meets nor fails, and a list is "
      "Max's single-inlet distribution, so '5 6' is the number 5 and the 6 is dropped. Everything "
      "is a float and zero-ness is tested on the float as it arrived: Max's int-only method would "
      "truncate 0.5 to a zero, which would break this object's headline use outright, since an "
      "envelope between 0 and 1 would read as permanently zero except at full scale and the gate "
      "would never open.");
  ADD_CATEGORY(pCategory::MATH);
  INLET_DOC(0, "in",
            "Int or float to watch — the left outlet bangs when it becomes non-zero and the right "
            "when it returns to zero, and nothing comes out while it stays on one side. A "
            "non-finite number is ignored, leaving the stored value alone, rather than read as 0, "
            "which would report a transition the patch never sent. A bang toggles the stored value "
            "between zero and non-zero and reports the transition it just made, so a stream of "
            "bangs alternates the outlets. Also accepts 'reset' (back to a stored 0, silently — "
            "the only way to re-arm without banging the right outlet). A list is reduced to its "
            "first number, as Max's single-inlet distribution does; a symbol is ignored.",
            "any float");
  OUTLET_DOC(0, "rising",
             "Bang when the stored value was 0 and the input is not 0. Silent while the value "
             "stays non-zero, however much it changes.",
             "bang");
  OUTLET_DOC(1, "falling",
             "Bang when the stored value was not 0 and the input is 0. Silent while the value "
             "stays zero.",
             "bang");
}

void gTogEdge::Receive(float value, YSE::THREAD thread) {
  // Refused rather than substituted — see "Non-finite input is ignored" in the
  // header. Reading a NaN as 0 would bang the falling outlet for a transition
  // the patch never sent, and storing it raw would leave the object in a state
  // neither predicate could ever fire from again.
  if (!std::isfinite(value)) return;

  const float previous = stored;

  // The shared predicates, not a local `previous != 0`: .change's middle and
  // right outlets are these two outlets, and `-0.f == 0.f` is the corner two
  // hand-written copies would eventually disagree about.
  const bool rising = ZeroToNonZero(previous, value);
  const bool falling = NonZeroToZero(previous, value);

  // Settled before anything is sent, so nothing reached from an outlet can
  // observe the object half-updated.
  stored = value;

  // Right to left, the order .change, .mean, .cartopol and .peak already use.
  // At most one of these can be true — a transition is either rising or falling
  // and never both — so the order is unobservable here; it is written this way
  // because the family's rule should not have exceptions a later outlet would
  // silently inherit.
  if (falling) outputs[1].SendBang(thread);
  if (rising) outputs[0].SendBang(thread);
}

FLOAT_IN(SetFloat) {
  if (inlet != 0) return;
  Receive(value, thread);
}

INT_IN(SetInt) {
  // One numeric type throughout — see "One numeric type" in the header.
  SetFloat(static_cast<float>(value), inlet, thread);
}

BANG_IN(SetBang) {
  if (inlet != 0) return;

  // Max: "Switches the value stored in togedge from 0 to non-zero, or vice
  // versa, and reports the change by sending a bang out one of the outlets."
  // Routed through Receive() rather than banging directly, so the toggle is
  // reported by the same predicates every other path uses and cannot drift from
  // them. The literal 1 is the non-zero side; nothing downstream can observe
  // which non-zero number is held, since only zero-ness is ever tested.
  Receive(stored == 0.f ? 1.f : 0.f, thread);
}

LIST_IN(SetList) {
  if (inlet != 0) return;

  // The family's word, with .counter's / .accum's / .peak's / .past's /
  // .change's meaning: back to how the object was created, which here is a
  // stored 0. Silent, and that silence is the entire reason it exists — sending
  // a plain 0 to re-arm would bang the falling outlet.
  if (value == "reset") {
    stored = 0.f;
    return;
  }

  // Max's single-inlet list distribution: the first element goes to the
  // int / float method and the rest is dropped. Only the leading token is ever
  // examined, and it counts only if the whole of it is a finite number — which
  // is what makes a symbol an ignored message rather than a zero, and what
  // keeps `nan` and `inf` out of the stored value.
  //
  // A message with no leading number and no word this object knows is ignored
  // rather than guessed at, as in .slide, .mean, .accum, .maximum, .peak,
  // .past and .change.
  std::size_t begin = 0;
  std::size_t end = 0;
  float number = 0.f;
  if (LeadingToken(value, begin, end) &&
      ReadNumericToken(value.c_str() + begin, end - begin, number)) {
    Receive(number, thread);
  }
}

GUI_VALUE() {
  // The one bit that decides what the next input does. The stored number itself
  // is not shown because nothing can observe it: only its zero-ness is ever
  // tested.
  return IsHigh() ? "1" : "0";
}
