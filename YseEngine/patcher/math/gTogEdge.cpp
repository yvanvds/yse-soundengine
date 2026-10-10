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

  ADD_DESCRIPTION("Bangs the left outlet when a value becomes non-zero and the right outlet when "
                  "it returns to 0, and stays silent in between. A bang flips the value between "
                  "zero and non-zero.");
  ADD_CATEGORY(pCategory::MATH);
  INLET_DOC(0, "in",
            "Number to watch. Bang: flip between zero and non-zero, and report it. 'reset': set "
            "the value to 0 without sending.",
            "any float");
  OUTLET_DOC(0, "rising", "Bang when the value changes from 0 to non-zero.", "bang");
  OUTLET_DOC(1, "falling", "Bang when the value changes from non-zero to 0.", "bang");
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
