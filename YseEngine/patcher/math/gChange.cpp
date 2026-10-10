#include "gChange.h"
#include "../pListArgs.h"
#include "../pObjectList.hpp"
#include <cmath>
#include <cstddef>
#include <string>

using namespace YSE::PATCHER;

#define className gChange

namespace {

  // The separators Parameters::Set and the list outlets use. Hand-rolled rather
  // than std::isspace, which reads locale state another thread may be mutating
  // and is undefined for a negative char.
  bool IsSeparator(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
  }

  // The first separator-delimited token of @p text at or after @p offset,
  // reported as the half-open range [begin, end). Returns false when there is
  // nothing there. A range rather than a substring, because a substr would
  // allocate on whichever thread the message arrived on — every caller below
  // hands the range straight to ReadNumericToken or ReadMode, both of which
  // take one.
  bool LeadingToken(const std::string& text, std::size_t offset, std::size_t& begin,
                    std::size_t& end) {
    begin = offset;
    while (begin < text.size() && IsSeparator(text[begin]))
      begin++;
    end = begin;
    while (end < text.size() && !IsSeparator(text[end]))
      end++;
    return end > begin;
  }

} // namespace

CONSTRUCT() {
  // One inlet, as in Max: numbers to filter, plus the `set`, `mode` and `reset`
  // words. No bang — see "What this object does not accept" in the header.
  ADD_IN_0;
  REG_FLOAT_IN(SetFloat);
  REG_INT_IN(SetInt);
  REG_LIST_IN(SetList);

  // The value, then Max's two zero/non-zero edge outlets. Outlet 0 stays a
  // float in every mode: an outlet's type is part of the object's shape and
  // cannot depend on a `mode` message that arrives later. The other two are
  // ints because 1 is the only value they can carry.
  ADD_OUT_FLOAT;
  ADD_OUT_INT;
  ADD_OUT_INT;

  // Both creation arguments arrive as one token list rather than a float and a
  // string parameter, because Parameters::Set feeds a FLOAT parameter through
  // std::stof, which *throws* on a token that is not a number — and `.change +`
  // is a legal object whose first token is not one. Reading the tokens here
  // also keeps the object's one non-finite rule in one place: a token is only a
  // number when ReadNumericToken says the whole of it is a finite one.
  ADD_PARAM(args);
  REG_PARM_CLEAR;
  REG_PARM_PARSE;

  // Max: "If there is no argument, the initial value is 0." ClearParams() is
  // the same reset, and is not called for an object never given parameters at
  // all.
  stored = 0.f;
  initial = 0.f;
  mode = Mode::DIFFERENT;
  initialMode = Mode::DIFFERENT;

  ADD_DESCRIPTION(
      "Passes a number on only when it differs from the last one, which is what keeps a control "
      "source that polls at frame rate from filling a bounded message queue with a value nothing "
      "downstream will act on. Max's change, and it looks trivial in a way it is not. The stored "
      "value starts at the 'initial' creation argument rather than at 'nothing received yet' — "
      "Max: "
      "'If there is no argument, the initial value is 0' — so a bare .change sent 0 as its very "
      "first message emits nothing, because 0 is what it already held, while the same object sent "
      "5 "
      "first emits 5. That is the point of the argument: .change 5 declares what the patch already "
      "believes the value to be, so loading a patch does not fire an event for a parameter that "
      "has "
      "not moved, and an object that always emitted its first input could not express it. The "
      "comparison is exact, with no tolerance, for the reason .sel gives from the other side: a "
      "tolerance in a matcher makes it match values it was not given, and a tolerance in a "
      "suppressor is worse, because swallowing a value that genuinely differs is invisible and "
      "leaves a patch whose parameter stopped updating at the fourth decimal place with no symptom "
      "to trace. Put a .round on the way in, where the patch's real tolerance is visible. Negative "
      "zero is not a change from zero, since they are the same number. A non-finite number is "
      "ignored outright — nothing is emitted, no outlet fires and the stored value is untouched — "
      "rather than read as 0 the way the rest of the patcher reads it: reading a NaN as 0 would "
      "emit a zero the patch never sent and then swallow the next genuine one, and storing it raw "
      "is worse, because a NaN compares unequal to everything including itself and a stream of "
      "them "
      "would emit on every single message, which is the exact repetition flood this object exists "
      "to stop. So the stored value is always finite, whether it came from the argument, from "
      "'set' "
      "or from the inlet. Three outlets, as in Max: outlet 0 carries the number when it differs; "
      "outlet 1 sends 1 when the stored value was 0 and the input is not; outlet 2 sends 1 when "
      "the "
      "stored value was not 0 and the input is. The last two are .togedge built into the object "
      "and "
      "are ported because Max ships them, so a patch translated from Max does not silently lose "
      "two "
      "cords; neither can fire on a repetition, and both are evaluated against the value held "
      "before this input. The three fire right to left, so whatever the value triggers already "
      "sees "
      "the matching edge reports. Max's second argument and its 'mode' message turn the object "
      "from "
      "a difference detector into a direction detector: 'mode +' sends 1 when the number is "
      "greater "
      "than the previous one, 'mode -' sends -1 when it is less, and a bare 'mode' returns to the "
      "default. The stored value is replaced by every number received in every mode — it is the "
      "previously received number, not the last one emitted — so a .change + fed 5, 3, 4 emits "
      "nothing, nothing, then 1. A mode flag this object does not know leaves the mode alone. 'set "
      "<n>' replaces the stored value without emitting, which is the whole of what makes it "
      "useful: "
      "if it emitted it would just be a spelling of sending n at the inlet, whereas staying silent "
      "lets a patch re-synchronise the filter with a value that reached the parameter by another "
      "route. 'reset' is the patcher family's word rather than a Max message and returns both the "
      "stored value and the mode to what the object was created with, silently, since after a "
      "'set' "
      "or a 'mode' there is otherwise no way back to the creation argument. A bang is not "
      "accepted, "
      "because outlet 0 means 'this number differs from the last one' and a bang would have to "
      "either fabricate a change or do nothing. A symbol is not accepted either: Max's change has "
      "no symbol method, two of its three outlets are numeric predicates and two of its three "
      "modes "
      "are orderings, none of which a symbol has, so suppressing repeated symbols is a different "
      "object rather than a mode of this one. A list is Max's single-inlet distribution — the "
      "first "
      "element is the number and the rest is dropped, so '5 6' is the number 5 — and a message "
      "that "
      "is neither a word this object knows nor a list beginning with a number is ignored. "
      "Everything is a float and outlet 0 is a float outlet, so Max's "
      "int-unless-the-argument-has-a-decimal-point duality is not reproduced: '.change 5' and "
      "'.change 5.0' are the same parameter string here, and guessing integral would turn a change "
      "filter into a change-and-quantise filter.");
  ADD_CATEGORY(pCategory::MATH);
  INLET_DOC(0, "in",
            "Int or float to compare against the stored value — sent out outlet 0 only when it "
            "differs, or in the '+' / '-' modes when it is greater / less. A non-finite number is "
            "ignored. Also accepts the messages 'set <n>' (replace the stored value without "
            "emitting), 'mode +' / 'mode -' / 'mode' (switch the comparison, or return to the "
            "default) and 'reset' (back to the creation argument and creation mode). All of them "
            "are silent. A list is reduced to its first number, as Max's single-inlet distribution "
            "does; a bang and a symbol are not accepted.",
            "any float");
  OUTLET_DOC(
      0, "out",
      "The number received, when it differs from the stored value. In 'mode +' this carries "
      "1 when the number is greater than the previous one and in 'mode -' it carries -1 when "
      "the number is less; it stays a float outlet in every mode. Silent otherwise, which is "
      "the whole point of the object.",
      "any float");
  OUTLET_DOC(1, "rising", "1 when the stored value was 0 and the input is not 0. Silent otherwise.",
             "1");
  OUTLET_DOC(2, "falling",
             "1 when the stored value was not 0 and the input is 0. Silent otherwise.", "1");
  PARAM_DOC(
      "initial mode", "0",
      "The value the first input is compared against, and optionally Max's mode flag. A bare "
      ".change starts at 0, so its first 0 is swallowed as a repetition and its first non-zero "
      "number comes out; '.change 5' declares that the patch already believes the value to be "
      "5, so loading it fires nothing until the value actually moves. A second argument of "
      "'+' or '-' starts the object in the greater-than or less-than mode, as Max's does. A "
      "token that does not read as a whole finite number is not a value — '.change inf' "
      "therefore starts at 0 — and a token that is neither is ignored. 'reset' returns to "
      "both of these.",
      "a float, optionally followed by + or -");
}

PARM_CLEAR() {
  // Runs on the control thread before the parameter string is re-read, and is
  // the whole of `SetParams("")`: Parameters::Set returns without calling the
  // parse callback for an empty argument, so this has to leave a usable object
  // behind rather than one still holding the previous creation argument.
  args.clear();
  initial = 0.f;
  initialMode = Mode::DIFFERENT;
  stored = 0.f;
  mode = Mode::DIFFERENT;
}

PARM_PARSE() {
  // Runs on the control thread once the creation parameters have been read.
  // Reading the tokens here rather than in the constructor is what lets a saved
  // `.change 5 +` come back holding 5 and comparing upwards.
  //
  // Scanned rather than taken positionally, so that `.change +` — a legal Max
  // object whose only argument is the mode — is not read as an initial value of
  // nothing. The first token that is a whole finite number is the value and the
  // first `+` or `-` is the mode; anything else is ignored, including a second
  // number, since Max documents exactly one.
  initial = 0.f;
  initialMode = Mode::DIFFERENT;
  bool haveValue = false;
  bool haveMode = false;

  for (const std::string& token : args) {
    // Parameters::Set splits on single spaces, so a run of them yields empty
    // tokens.
    if (token.empty()) continue;

    if (!haveValue) {
      float parsed = 0.f;
      // The whole token has to be the number: `5abc` is not a value, and
      // neither is `inf`, which is what keeps the stored value finite without a
      // sanitising substitution that would invent a 0 nobody wrote.
      if (ReadNumericToken(token, parsed)) {
        initial = parsed;
        haveValue = true;
        continue;
      }
    }

    if (!haveMode) {
      Mode parsedMode = Mode::DIFFERENT;
      if (ReadMode(token.c_str(), token.size(), parsedMode)) {
        initialMode = parsedMode;
        haveMode = true;
      }
    }
  }

  stored = initial;
  mode = initialMode;
}

bool gChange::ReadMode(const char* text, std::size_t length, Mode& out) {
  // Max's two flags and nothing else. A `mode` with a word this object does not
  // know leaves the mode alone rather than silently reconfiguring the object,
  // which is the answer .peak gives a malformed `set`.
  if (length != 1) return false;
  if (text[0] == '+') {
    out = Mode::GREATER;
    return true;
  }
  if (text[0] == '-') {
    out = Mode::LESS;
    return true;
  }
  return false;
}

void gChange::Receive(float value, YSE::THREAD thread) {
  // Refused rather than substituted — see "Non-finite input is refused" in the
  // header. Nothing is emitted and the stored value is left alone, which is the
  // only answer under which "this object suppresses repetitions" stays true for
  // every input.
  if (!std::isfinite(value)) return;

  const float previous = stored;

  // Max: "If the stored value is 0 and the input is not 0" / "is not 0 and the
  // input is 0" — both are about the value held *before* this input, so they
  // are computed here and the store happens below.
  const bool rising = ZeroToNonZero(previous, value);
  const bool falling = NonZeroToZero(previous, value);

  bool emit = false;
  float payload = 0.f;
  switch (mode) {
  case Mode::GREATER:
    // Max: "causes change to send a 1 out its left outlet if the received
    // number is greater than the previously received number."
    emit = value > previous;
    payload = 1.f;
    break;
  case Mode::LESS:
    // Max: "causes change to send out a -1 if the received number is less than
    // the previously received number."
    emit = value < previous;
    payload = -1.f;
    break;
  case Mode::DIFFERENT:
  default:
    // Exact, and exactness is the object — see the header on why no tolerance
    // is offered. Both sides are finite here, so there is no NaN corner.
    emit = value != previous;
    payload = value;
    break;
  }

  // "the previously received number", in every mode: the store is
  // unconditional, not a consequence of having emitted. Settled before anything
  // is sent, so nothing reached from an outlet can observe the object
  // half-updated.
  stored = value;

  // Right to left, as .mean, .cartopol and .peak already send: whatever the
  // value on outlet 0 triggers downstream already sees the matching edge
  // reports rather than the previous message's.
  if (falling) outputs[2].SendInt(1, thread);
  if (rising) outputs[1].SendInt(1, thread);
  if (emit) outputs[0].SendFloat(payload, thread);
}

FLOAT_IN(SetFloat) {
  if (inlet != 0) return;
  Receive(value, thread);
}

INT_IN(SetInt) {
  // One numeric type throughout — see "One numeric type" in the header.
  SetFloat(static_cast<float>(value), inlet, thread);
}

LIST_IN(SetList) {
  if (inlet != 0) return;

  // Max: "The word mode by itself returns change to its default mode of sending
  // out received values that differ from the previously received input."
  // MatchWord cannot answer this one — it requires a separator after the word,
  // which is exactly what makes `settle 3` not read as `set 3` — so the bare
  // word is compared here, as .past compares its bare `clear`.
  if (value == "mode") {
    mode = Mode::DIFFERENT;
    return;
  }

  // The family's word, with .counter's / .accum's / .peak's / .past's meaning:
  // back to how the object was created. Both halves, because after a `set` and
  // a `mode` the whole creation state is the only unambiguous thing the word
  // can mean.
  if (value == "reset") {
    stored = initial;
    mode = initialMode;
    return;
  }

  std::size_t argument = 0;
  std::size_t begin = 0;
  std::size_t end = 0;

  // Max: "Replaces the stored value without triggering output." Silent, and
  // that silence is the entire reason the message exists.
  if (MatchWord(value, "set", 3, argument)) {
    float parsed = 0.f;
    // ReadNumericToken is the finiteness gate here: a `set` with no number, a
    // partial one (`set 5abc`) or a non-finite one (`set nan`) keeps the value
    // it had rather than storing a zero nobody sent. That is what makes the
    // stored value finite on every path.
    if (LeadingToken(value, argument, begin, end) &&
        ReadNumericToken(value.c_str() + begin, end - begin, parsed)) {
      stored = parsed;
    }
    return;
  }

  // Max: "The word mode, followed by a +" / "followed by a -". An unknown flag
  // leaves the mode alone.
  if (MatchWord(value, "mode", 4, argument)) {
    Mode parsed = Mode::DIFFERENT;
    if (LeadingToken(value, argument, begin, end) &&
        ReadMode(value.c_str() + begin, end - begin, parsed)) {
      mode = parsed;
    }
    return;
  }

  // Max's single-inlet list distribution: the first element goes to the
  // int / float method and the rest is dropped. Only the leading token is ever
  // examined, and it counts only if the whole of it is a finite number — which
  // is what makes a symbol an ignored message rather than a zero.
  //
  // A message with no leading number and no word this object knows is ignored
  // rather than guessed at, as in .slide, .mean, .accum, .maximum, .peak and
  // .past.
  float number = 0.f;
  if (LeadingToken(value, 0, begin, end) &&
      ReadNumericToken(value.c_str() + begin, end - begin, number)) {
    Receive(number, thread);
  }
}

GUI_VALUE() {
  // The stored value, which is what the next input is compared against and the
  // whole of the object's numeric state.
  return std::to_string(stored);
}
