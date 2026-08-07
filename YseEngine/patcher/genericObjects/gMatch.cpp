#include "gMatch.h"
#include "../math/gExprEval.h"
#include "../pListArgs.h"
#include "../pObjectList.hpp"
#include <cmath>
#include <cstddef>
#include <string>

using namespace YSE::PATCHER;

#define className gMatch

namespace {

  // The separators Parameters::Set and the list outlets use. Hand-rolled rather
  // than std::isspace, which reads locale state another thread may be mutating
  // and is undefined for a negative char. Same helper .sel and .route keep.
  bool IsSeparator(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
  }

  // Max's wildcard, spelled exactly as the reference spells it. Case sensitive:
  // `NN` is not `nn`, and the object says so by refusing to match rather than
  // by guessing — see "An argument that is neither a number nor nn".
  const char WILDCARD_WORD[] = "nn";
  constexpr std::size_t WILDCARD_LENGTH = 2;

} // namespace

CONSTRUCT() {
  // One inlet. Max lists int, float, list and anything, plus the two messages;
  // there is no bang, because a message carrying no number cannot advance a
  // sequence.
  ADD_IN_0;
  REG_INT_IN(SetInt);
  REG_FLOAT_IN(SetFloat);
  REG_LIST_IN(SetList);

  // Max: "The numbers received in the inlet are compared with the arguments. If
  // the numbers are the same, and in the same order, they are sent out the
  // outlet as a list." One outlet, always a list, always pattern-length long.
  ADD_OUT_LIST;

  ADD_PARAM(patternArgs);
  // The pattern is compiled out of the argument tokens, so it has to be built
  // after Parameters::Set has read them; the clear callback is what makes
  // SetParams("") return the object to a bare `.match` rather than leaving the
  // previous pattern in place.
  REG_PARM_CLEAR;
  REG_PARM_PARSE;

  // The one allocation the output path would otherwise need. Every value is at
  // most kExprValueTextMax - 1 characters and they are joined by single spaces,
  // so this is an upper bound on any list the object can send.
  result.reserve((std::size_t)kMatchMaxPattern * (std::size_t)kExprValueTextMax);

  ADD_DESCRIPTION(
      "Watches the stream of values arriving at its inlet and sends the whole sequence out as a "
      "list the moment it spells the creation argument — Max's match, 'watches an incoming stream "
      "of ints, floats, symbols, lists, or messages, and outputs the stream after it has met the "
      "specification of its arguments'. This is pattern recognition over time rather than over a "
      "single value, which is the thing the patcher had no way to express: .sel and .split answer "
      "one number at a time and .change and .togedge compare a number with the one before it, so "
      "recognising a played motif, a controller gesture or a trigger sequence meant a chain of "
      "counters and gates whose state was implicit in the wiring. The word 'nn' is Max's wild card "
      "and matches any number, so '.match 1 nn 3' fires on 1, 5, 3 — and what comes out is what "
      "arrived, '1 5 3', not the pattern, which is why the object emits a list rather than a bang: "
      "the wildcard positions carry the information the patch was listening for. The rule that "
      "separates a working matcher from a plausible one is that a mismatch does not throw away the "
      "values that caused it, because those values may be the start of the next candidate: against "
      "'.match 1 2 3' the stream 1 2 1 2 3 matches, since the third value ends one candidate and "
      "begins another at the same moment, and an implementation that walks a cursor forward and "
      "resets it to 0 on a mismatch reports nothing at all there. So the object keeps the last N "
      "values received, N being the pattern length, and after every value asks whether that window "
      "now spells the pattern — every candidate starting position is tested at once rather than "
      "one "
      "chosen in advance, at a bounded cost of at most N comparisons per value with no allocation. "
      "Matches do not overlap: on a match the window is emptied, so '.match 1 1 1' fed five 1s "
      "fires once rather than three times, which is the same erasure Max's 'clear' performs by "
      "hand "
      "and is what makes the outlet a stream of events rather than of every window that happens to "
      "fit. A value that is not a number occupies its position in the stream and matches nothing "
      "there, breaking any candidate it lands in — that covers a symbol (a list token that does "
      "not "
      "read as a number in its entirety, so '5abc' is a symbol and not the number 5) and a "
      "non-finite value, where the patcher's usual 'read a non-finite as 0' substitution is "
      "deliberately not applied because it would let a stray NaN complete a sequence a patch wired "
      "for a real 0; a NaN could never match a literal in any case, and what the rule settles is "
      "that it does not match the wildcard either. Comparison against a literal is exact, as "
      ".sel's "
      "and .change's are, so a computed float may miss a literal it looks equal to and a .round "
      "upstream is the fix; an int is widened to a float and compared as one, since this patcher "
      "has one numeric type and '.match 5' and '.match 5.0' are the same parameter string. An "
      "argument that is neither a number nor 'nn' is kept as an element nothing can match, so the "
      "object goes silent rather than quietly matching a shorter sequence: the pattern's length is "
      "half of what the object means, and turning the uppercase typo '.match 1 NN 3' into '.match "
      "1 "
      "3' would leave a box that fires on a sequence the patch never asked for. A pattern of zero "
      "elements — a bare .match, or a 'set' with no list — has no sequence to detect and stays "
      "silent, rather than matching everything immediately and putting an endless stream of empty "
      "lists on the outlet. Two messages: 'clear' empties the window without touching the pattern "
      "and emits nothing, and 'set <list>' replaces the pattern, taking 'nn' the way the creation "
      "arguments do, and empties the window too, since a partial candidate against the old pattern "
      "means nothing against the new one. 'set' deliberately does not rewrite the creation "
      "argument, so a DumpJSON after one saves the pattern the object was created with — the same "
      "split .peak's 'set' has from its 'initial' argument. There is no bang inlet: Max routes "
      "bang "
      "through 'anything', which performs the same as 'list', and a bang is a list of no values, "
      "so "
      "an inlet that accepted it would only be a way of spelling 'do nothing'. Calculate() does "
      "nothing, an emitting one having no stimulus to report. At most 256 pattern elements are "
      "held.");
  ADD_CATEGORY(pCategory::GENERIC);
  INLET_DOC(
      0, "in",
      "Int, float or list to feed into the sequence. Each number takes the next position in "
      "the stream; a list contributes its tokens left to right, and a token that does not "
      "read as a number in its entirety — like a non-finite value — takes a position that "
      "matches nothing and so breaks any candidate it lands in. The message 'clear' forgets "
      "every value received so far without touching the pattern, and 'set <list>' replaces "
      "the pattern (with 'nn' accepted as the wildcard) and forgets the values too. A bang is "
      "not accepted: a message carrying no number cannot advance a sequence.",
      "any");
  OUTLET_DOC(0, "match",
             "The matched sequence, as a list of exactly as many values as the pattern has "
             "elements, in the order they arrived and in the spelling they arrived in — so the "
             "wildcard positions carry the values that filled them. Sent the moment the last value "
             "of the sequence arrives, and then the object forgets everything, so consecutive "
             "matches never overlap.",
             "list of numbers");
  PARAM_DOC(
      "pattern", "",
      "The sequence to watch for, as a list. A token that reads as a finite number in its "
      "entirety is a literal matched exactly; the word 'nn' is Max's wild card and matches "
      "any number; anything else is kept as an element nothing can match, so a typo makes the "
      "object silent rather than making it match a shorter sequence. With no argument there "
      "is no sequence to detect and the object stays silent. At most 256 elements are held.",
      "any list of numbers and nn");
}

// ─── the pattern ──────────────────────────────────────────────────────────────

gMatch::Element gMatch::ReadElement(const char* text, std::size_t length, float& out) {
  out = 0.f;
  if (length == WILDCARD_LENGTH && text[0] == WILDCARD_WORD[0] && text[1] == WILDCARD_WORD[1]) {
    return Element::WILDCARD;
  }
  // Strict on purpose: ExprParseFloatList would turn the symbol `5abc` into the
  // number 5 and fold `1e999` to a plain 0 that collides with every real zero a
  // patch sends. Both are wrong for deciding what an element *is*.
  if (ReadNumericToken(text, length, out)) return Element::LITERAL;
  return Element::NEVER;
}

void gMatch::ReadPattern(const char* text, std::size_t length, std::size_t offset) {
  int count = 0;
  std::size_t i = offset;
  while (i < length && count < kMatchMaxPattern) {
    while (i < length && IsSeparator(text[i]))
      i++;
    std::size_t end = i;
    while (end < length && !IsSeparator(text[end]))
      end++;
    if (end == i) break;

    float number = 0.f;
    patternKind[count] = ReadElement(text + i, end - i, number);
    patternValue[count] = number;
    count++;
    i = end;
  }

  patternLength = count;
  // A candidate against the old pattern means nothing against the new one, and
  // the window's ring size is the pattern length, so it cannot be carried over
  // even in principle.
  Forget();
}

PARM_CLEAR() {
  // Runs on the control thread before the parameter string is re-read, and is
  // the whole of `SetParams("")`: Parameters::Set returns without calling the
  // parse callback for an empty argument, so this has to leave a usable object
  // behind rather than one still watching for the previous sequence.
  patternArgs.clear();
  patternLength = 0;
  Forget();
}

PARM_PARSE() {
  int count = 0;
  for (const std::string& token : patternArgs) {
    if (count >= kMatchMaxPattern) break;
    // Parameters::Set splits on single spaces, so a run of them yields empty
    // tokens. An empty element could never be matched by anything and would
    // only lengthen the sequence by a position nothing can fill.
    if (token.empty()) continue;

    float number = 0.f;
    patternKind[count] = ReadElement(token.c_str(), token.size(), number);
    patternValue[count] = number;
    count++;
  }

  patternLength = count;
  Forget();
}

bool gMatch::ElementIsWildcard(int index) const {
  if (index < 0 || index >= patternLength) return false;
  return patternKind[index] == Element::WILDCARD;
}

bool gMatch::ElementIsLiteral(int index) const {
  if (index < 0 || index >= patternLength) return false;
  return patternKind[index] == Element::LITERAL;
}

float gMatch::ElementValue(int index) const {
  if (!ElementIsLiteral(index)) return 0.f;
  return patternValue[index];
}

// ─── the window ───────────────────────────────────────────────────────────────

void gMatch::Forget() {
  head = 0;
  filled = 0;
}

const gMatch::Slot& gMatch::Recent(int back) const {
  // `head` is the next slot to write, so the newest value sits one before it.
  int index = head - back;
  if (index < 0) index += patternLength;
  return window[index];
}

bool gMatch::Accepts(int index, const Slot& slot) const {
  switch (patternKind[index]) {
  case Element::WILDCARD:
    // Max: "a wild card that will match any number" — a number, and so not a
    // symbol and not a NaN.
    return slot.number;
  case Element::LITERAL:
    // Exact, and exactness is the object; an int is widened and compared as a
    // float, this patcher having one numeric type. A non-number never gets
    // here on its value, which is what keeps a NaN from matching by accident.
    return slot.number && slot.value.AsFloat() == patternValue[index];
  default:
    // Element::NEVER — an argument the object cannot look for.
    return false;
  }
}

bool gMatch::WindowMatches() const {
  // Oldest first: with the ring full, `head` is both the next slot to write and
  // the oldest value in it.
  for (int i = 0; i < patternLength; i++) {
    int index = head + i;
    if (index >= patternLength) index -= patternLength;
    if (!Accepts(i, window[index])) return false;
  }
  return true;
}

void gMatch::Emit(YSE::THREAD thread) {
  result.clear();
  for (int i = 0; i < patternLength; i++) {
    if (i != 0) result += ' ';
    int index = head + i;
    if (index >= patternLength) index -= patternLength;
    char text[kExprValueTextMax];
    // No locale, no allocation, and an int stays spelled as an int — which is
    // why the list `1 2 3` comes back as `1 2 3` rather than as `1. 2. 3.`.
    const int length = ExprFormatValue(window[index].value, text, kExprValueTextMax);
    result.append(text, (std::size_t)length);
  }

  // Forgotten before the send, twice over. Matches must not overlap, so the
  // values just reported cannot also begin the next sequence; and the send path
  // is synchronous with no queue in between, so a patch looping the outlet back
  // into the inlet re-enters here inside the SendList below and has to find the
  // object already describing the sequence being reported.
  Forget();
  outputs[0].SendList(result, thread);
}

void gMatch::Push(const ExprValue& value, bool number, YSE::THREAD thread) {
  // Nothing to detect, so nothing worth remembering — and the ring's size *is*
  // the pattern length, so there would be nowhere to put it.
  if (patternLength <= 0) return;

  window[head] = Slot{value, number};
  head++;
  if (head >= patternLength) head = 0;
  if (filled < patternLength) filled++;

  if (filled < patternLength) return;
  if (!WindowMatches()) return;
  Emit(thread);
}

int gMatch::Progress() const {
  if (patternLength <= 0) return 0;
  // A full-length prefix match would have emitted and emptied the window, so
  // the answer is always strictly less than the pattern length.
  const int most = filled < patternLength ? filled : patternLength - 1;
  for (int k = most; k > 0; k--) {
    bool ok = true;
    for (int j = 0; j < k; j++) {
      // Element j of the pattern against the (k - j)-th value back from the
      // newest, so element k-1 lands on the newest value.
      if (!Accepts(j, Recent(k - j))) {
        ok = false;
        break;
      }
    }
    if (ok) return k;
  }
  return 0;
}

// ─── the inlet ────────────────────────────────────────────────────────────────

INT_IN(SetInt) {
  if (inlet != 0) return;
  Push(ExprValue::Int(value), true, thread);
}

FLOAT_IN(SetFloat) {
  if (inlet != 0) return;
  // A non-finite value takes its position in the stream and matches nothing
  // there — not even the wildcard. Reading it as 0, the patcher's usual
  // substitution, would let a stray NaN complete a sequence a patch wired for a
  // real zero.
  Push(ExprValue::Float(value), std::isfinite(value), thread);
}

LIST_IN(SetList) {
  if (inlet != 0) return;

  // Max: "Causes match to forget all numbers it has received up to that time."
  // The pattern is untouched, and nothing is emitted.
  if (value == "clear") {
    Forget();
    return;
  }

  // Max: "The word set, followed by a list of numbers, specifies a new series
  // of numbers match will look for." MatchWord requires the separator, so
  // `settle 1 2` is not read as `set tle 1 2`.
  std::size_t argument = 0;
  if (MatchWord(value, "set", 3, argument)) {
    ReadPattern(value.c_str(), value.size(), argument);
    return;
  }
  // The bare word is `set` with an empty list, which is a pattern of no
  // elements: there is then no sequence to detect and the object stays silent
  // until another `set` gives it one. The literal reading, and the same shape a
  // bare `.match` has.
  if (value == "set") {
    patternLength = 0;
    Forget();
    return;
  }

  // Max's `anything`, which "performs the same as list": every token takes a
  // position in the stream, left to right. Bounded by the message length and
  // allocation-free — the tokens are read in place rather than split out.
  std::size_t i = 0;
  while (i < value.size()) {
    while (i < value.size() && IsSeparator(value[i]))
      i++;
    std::size_t end = i;
    while (end < value.size() && !IsSeparator(value[end]))
      end++;
    if (end == i) break;

    const std::size_t length = end - i;
    float number = 0.f;
    if (ReadNumericToken(value.c_str() + i, length, number)) {
      // Spelled as an int or as a float, which is what decides how it is
      // written back out; the shared test lives next to the reader that agreed
      // the token is a number.
      if (TokenLooksLikeFloat(value.c_str() + i, length)) {
        Push(ExprValue::Float(number), true, thread);
      } else {
        Push(ExprValue::Int(ExprToInt(number)), true, thread);
      }
    } else {
      // A symbol — including a non-finite one, which ReadNumericToken refuses.
      // It takes its position and matches nothing there.
      Push(ExprValue(), false, thread);
    }
    i = end;
  }
}

GUI_VALUE() {
  // How far into the pattern the stream is, which is the only thing about this
  // object an editor could usefully show and the only thing a patch can observe
  // without waiting for a whole sequence.
  return std::to_string(Progress());
}
