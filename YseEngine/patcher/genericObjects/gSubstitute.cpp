#include "gSubstitute.h"
#include "../../implementations/logImplementation.h"
#include "../math/gExprEval.h"
#include "../pListArgs.h"
#include "../pObjectList.hpp"

using namespace YSE::PATCHER;

#define className gSubstitute

namespace {

  // The separators Parameters::Set and the list outlets use. Hand-rolled rather
  // than std::isspace, which reads locale state another thread may be mutating
  // and is undefined for a negative char.
  inline bool IsSeparator(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
  }

  // The longest list the patcher's own queues carry
  // (patcherImplementation::kValueListCap), so anything that can reach this
  // object through the patcher can also be rewritten by it without the output
  // buffer growing.
  constexpr std::size_t kValueListCap = 256;

  // Worst case: alternating "x " makes a 256-character list out of 128
  // single-character elements, and every one of them may become a
  // TOKEN_CAPACITY-character replacement plus its separator.
  constexpr std::size_t kOutTextCapacity =
      (((kValueListCap + 1) / 2) * (gSubstitute::TOKEN_CAPACITY + 1)) + 1;

  // Max's default replacement — "the second number or symbol specifies the
  // replacement for the match", default 0. Used when a match is given without
  // one, because an empty replacement would leave an empty token in the message.
  constexpr const char* kDefaultReplacement = "0";

  // First whitespace-delimited word of the `length` characters at `text`.
  // Returns false when there is none, which is what an empty or all-whitespace
  // message is.
  bool FirstToken(const char* text, std::size_t length, std::size_t& begin, std::size_t& size) {
    std::size_t start = 0;
    while (start < length && IsSeparator(text[start]))
      start++;
    std::size_t end = start;
    while (end < length && !IsSeparator(text[end]))
      end++;
    if (end == start) return false;
    begin = start;
    size = end - start;
    return true;
  }

} // namespace

CONSTRUCT() {
  // The message to rewrite. Every kind, since Max routes them all through
  // `anything` and each has elements to match on — a bang's being the symbol
  // `bang`.
  ADD_IN_0;
  REG_BANG_IN(SetBang);
  REG_INT_IN(SetInt);
  REG_FLOAT_IN(SetFloat);
  REG_LIST_IN(SetList);

  // The match and the replacement, an inlet each: both are single tokens, so an
  // int and a float mean something on either, and a patch can re-aim one
  // without restating the other. Bang is declined rather than swallowed — see
  // the class documentation.
  ADD_IN_1;
  REG_INT_IN(SetMatchInt);
  REG_FLOAT_IN(SetMatchFloat);
  REG_LIST_IN(SetMatchList);

  ADD_IN_2;
  REG_INT_IN(SetReplacementInt);
  REG_FLOAT_IN(SetReplacementFloat);
  REG_LIST_IN(SetReplacementList);

  // Both ANY: a rewritten scalar leaves as the type its replacement reads as,
  // and an unmatched message leaves as the type it arrived as.
  ADD_OUT_ANY;
  ADD_OUT_ANY;

  ADD_PARAM(matchArg);
  ADD_PARAM(replacementArg);
  ADD_PARAM(modeArg);
  REG_PARM_CLEAR;
  REG_PARM_PARSE;

  // Every allocation the message path would otherwise need, taken here on the
  // control thread for the largest message the object will accept.
  match.text.reserve(TOKEN_CAPACITY);
  replacement.text.reserve(TOKEN_CAPACITY);
  outText.reserve(kOutTextCapacity);

  // A bare .substitute is a wire; ClearParams() is what a re-parse restores it
  // to, so run it here rather than repeating the defaults.
  ClearParams();

  ADD_DESCRIPTION(
      "Replaces elements of a message — Max's substitute, which 'matches messages to its own "
      "arguments; whenever it finds a match, will make the appropriate substitution'. Two "
      "arguments, a match and a replacement: every element of an incoming message equal to the "
      "match leaves as the replacement, everything else leaves untouched, and a third argument of "
      "any kind restricts the edit to the first match in the message. This is the edit the patcher "
      "could not otherwise make: .prepend and .append put words on the ends of a message and "
      ".route, .routepass and .sel branch on the word it starts with, but nothing could change a "
      "word in the middle of one without a round trip out to the host application. Exactly one "
      "outlet fires. A message something was substituted in leaves the left outlet; one that "
      "matched nothing leaves the rightmost outlet unchanged and in its own type, which is Max's "
      "'if no substitution occurred ... the original input message is passed out the rightmost "
      "outlet' and makes the outlet a report of whether the edit happened — so substitutions chain "
      "the way .routepass matches do, each reject outlet feeding the next inlet. A bare "
      ".substitute matches nothing and is a wire, the .prepend rule that makes it safe to drop an "
      "unconfigured object into a working patch; Max's default of 0 for both arguments is declined "
      "for the reason .routepass declines to invent a default selector, since it would route "
      "zeroes out the other outlet than everything else. A match given without a replacement does "
      "take Max's 0, because an empty replacement would leave an empty token no .route matches. "
      "The matcher is .sel's and .routepass's: a match is a number or a symbol, classified once, "
      "and the two never match each other — a number matches by value with an int widened to a "
      "float, a symbol matches by exact text, and a bang matches a match spelled 'bang'. A list is "
      "walked element by element, which is the one place this differs from .routepass: "
      "substituting "
      "is not routing, so a match anywhere in the message counts. The comparison is exact, so a "
      "computed float may miss a match it looks equal to and a NaN matches nothing. A replaced "
      "element is written as the text of the replacement; when a scalar matched, the replacement "
      "leaves as the type it reads as, so .substitute 5 7 turns the int 5 into the int 7 rather "
      "than into the list that spells it. The match and the replacement each have an inlet, "
      "leaving inlet 0 with no reserved words in it — which matters more here than anywhere, since "
      "the messages this object edits are the ones that begin with a word. Match and replacement "
      "are at most 64 characters, refused rather than truncated past that. Calculate() does "
      "nothing, and no message path allocates, locks or blocks: the rewrite is a bounded walk into "
      "a buffer reserved at construction for the worst case a 256-character list can grow into, "
      "and a message that matched nothing is forwarded by reference rather than copied through "
      "it.");
  ADD_CATEGORY(pCategory::GENERIC);

  INLET_DOC(0, "in",
            "Bang, int, float or list to rewrite. Every element equal to the match is replaced; if "
            "any was, the result leaves the left outlet, and if none was, the message leaves the "
            "rightmost outlet unchanged and in the type it arrived as.",
            "any");
  INLET_DOC(1, "match",
            "Replaces the element being matched, without emitting anything. The first word of a "
            "list, or an int or a float as the number it spells. An empty message clears the match "
            "and makes the object a pass-through; a word longer than 64 characters is refused and "
            "the previous match kept.",
            "any single number or symbol, at most 64 characters");
  INLET_DOC(2, "replacement",
            "Replaces what a matched element is rewritten as, without emitting anything. The first "
            "word of a list, or an int or a float as the number it spells. An empty message is "
            "ignored — there is no empty replacement — and a word longer than 64 characters is "
            "refused and the previous replacement kept.",
            "any single number or symbol, at most 64 characters");

  OUTLET_DOC(0, "out",
             "The message with every matched element replaced, as a list — or, when the whole "
             "message was one matched element, the replacement in the type it reads as. Fires only "
             "when at least one substitution was made.",
             "any");
  OUTLET_DOC(1, "rest",
             "The message, unchanged and in its own type, when nothing in it matched. Chain this "
             "into the next .substitute to carry on rewriting: nothing was consumed, so the next "
             "object sees exactly what this one saw.",
             "any");

  PARAM_DOC("match", "",
            "The element to look for. A token that reads as a finite number matches by value, with "
            "an int widened to a float; anything else is a symbol and matches by exact text. With "
            "no argument the object matches nothing and passes everything out the rightmost "
            "outlet. At most 64 characters.",
            "any single number or symbol");
  PARAM_DOC("replacement", "0",
            "What a matched element is replaced by, written into the message exactly as it is "
            "spelled here. Defaults to Max's 0 when a match is given without one, since an empty "
            "replacement would leave an empty token in the message. At most 64 characters.",
            "any single number or symbol");
  PARAM_DOC("mode", "",
            "Max's 'replace first message only' mode: any third argument, of any kind, restricts "
            "the edit to the first matching element of a list rather than every one of them. With "
            "no third argument every match is replaced.",
            "any single number or symbol");
}

// A re-parse must not leave half of the previous configuration standing:
// Parameters::Set() calls this before parsing and returns early on an empty
// argument string, so this is the only thing that makes `SetParams("")` a real
// reset — back to the wire — rather than a no-op that leaves the object
// rewriting on its old pair.
PARM_CLEAR() {
  matchArg.clear();
  replacementArg.clear();
  modeArg.clear();

  ClassifyToken(match, "", 0);
  ClassifyToken(replacement, kDefaultReplacement, 1);
  firstOnly = false;
}

PARM_PARSE() {
  // Control thread only (Parameters::Set), so unlike the inlet path this can
  // afford a reason. Refused rather than truncated, and the object left
  // matching nothing rather than matching half a symbol.
  if (matchArg.size() > TOKEN_CAPACITY) {
    INTERNAL::LogImpl().emit(E_ERROR, std::string("patcher: ") + Type() +
                                          " match is longer than 64 characters; ignored");
    matchArg.clear();
  }
  if (replacementArg.size() > TOKEN_CAPACITY) {
    INTERNAL::LogImpl().emit(E_ERROR, std::string("patcher: ") + Type() +
                                          " replacement is longer than 64 characters; ignored");
    replacementArg.clear();
  }

  ClassifyToken(match, matchArg.c_str(), matchArg.size());
  if (replacementArg.empty()) {
    ClassifyToken(replacement, kDefaultReplacement, 1);
  } else {
    ClassifyToken(replacement, replacementArg.c_str(), replacementArg.size());
  }

  // Max: "Any third number or symbol sets the 'replace first message only'
  // mode." What it says, rather than what it spells — the token's value is
  // never read.
  firstOnly = !modeArg.empty();
}

void gSubstitute::ClassifyToken(Token& token, const char* text, std::size_t length) {
  token.text.assign(text, length);

  float number = 0.f;
  // Strict on purpose, as the rest of the family is: ExprParseFloatList would
  // read `5abc` as 5 and fold `1e999` to 0, and neither answers "is this token a
  // number at all".
  if (length > 0 && ReadNumericToken(text, length, number)) {
    token.numeric = true;
    token.value = number;
    // Which of SendInt / SendFloat a scalar substitution uses, so a replacement
    // typed as `7` does not come back as `7.`.
    token.isFloat = TokenLooksLikeFloat(text, length);
  } else {
    token.numeric = false;
    token.value = 0.f;
    token.isFloat = false;
  }
}

void gSubstitute::StoreToken(Token& token, const char* text, std::size_t length, bool emptyClears) {
  std::size_t begin = 0;
  std::size_t size = 0;
  if (!FirstToken(text, length, begin, size)) {
    // Nothing but whitespace. For the match that is a real request — it returns
    // the object to being a wire; for the replacement there is nothing to ask
    // for, since an empty replacement would leave an empty token behind.
    if (emptyClears) ClassifyToken(token, "", 0);
    return;
  }

  // Refused rather than truncated, and silently, since this may be the audio
  // thread.
  if (size > TOKEN_CAPACITY) return;

  ClassifyToken(token, text + begin, size);
}

bool gSubstitute::TokenMatches(const char* text, std::size_t length) const {
  if (match.text.empty()) return false;

  if (match.numeric) {
    float number = 0.f;
    if (!ReadNumericToken(text, length, number)) return false;
    // Exact, the matcher `.sel` and `.routepass` already apply. A NaN on either
    // side compares unequal, which is what leaves it unmatched.
    return number == match.value;
  }

  if (match.text.size() != length) return false;
  // Compared against the character range in place: a substr here would allocate
  // on whichever thread the message arrived on.
  return match.text.compare(0, length, text, length) == 0;
}

bool gSubstitute::NumberMatches(float value) const {
  if (match.text.empty() || !match.numeric) return false;
  // Exact, and compared as a number rather than as the text that spells it: an
  // int and a float that are equal must match the same match, and routing the
  // scalar path through ExprFormatValue first would make that depend on how the
  // formatter happens to spell a denormal.
  return match.value == value;
}

void gSubstitute::SendReplacement(YSE::THREAD thread) {
  // The scalar case: the whole message was the matched element, so what leaves
  // is the replacement in the type it reads as — an int stays an int rather
  // than becoming the one-token list that spells it.
  if (replacement.numeric) {
    if (replacement.isFloat) {
      outputs[0].SendFloat(replacement.value, thread);
    } else {
      outputs[0].SendInt((int)replacement.value, thread);
    }
    return;
  }
  outputs[0].SendList(replacement.text, thread);
}

BANG_IN(SetBang) {
  if (inlet != 0) return;

  // Max: "The bang message matches a 'bang' symbol in the arguments" — .sel's
  // and .routepass's rule, so all three agree on what a bang equals.
  if (TokenMatches("bang", 4)) {
    SendReplacement(thread);
    return;
  }
  outputs[1].SendBang(thread);
}

INT_IN(SetInt) {
  if (inlet != 0) return;

  // Widened and compared as a float: this patcher has one numeric type, so
  // `.substitute 5 x` has to answer the int 5 and the float 5.0 alike.
  if (NumberMatches((float)value)) {
    SendReplacement(thread);
    return;
  }
  outputs[1].SendInt(value, thread);
}

FLOAT_IN(SetFloat) {
  if (inlet != 0) return;

  if (NumberMatches(value)) {
    SendReplacement(thread);
    return;
  }
  outputs[1].SendFloat(value, thread);
}

LIST_IN(SetList) {
  if (inlet != 0) return;

  // With nothing to match there is nothing to walk, and the message is handed
  // on by reference rather than copied through the rewrite buffer.
  if (match.text.empty()) {
    outputs[1].SendList(value, thread);
    return;
  }

  // Rebuilt in a buffer reserved at construction. Filled here rather than kept
  // between sends: the send path is synchronous, so a patch looping the outlet
  // back into an inlet re-enters this function inside the SendList below.
  outText.clear();
  bool replaced = false;
  bool wroteToken = false;

  std::size_t i = 0;
  const std::size_t size = value.size();
  while (i < size) {
    while (i < size && IsSeparator(value[i]))
      i++;
    if (i >= size) break;
    const std::size_t begin = i;
    while (i < size && !IsSeparator(value[i]))
      i++;
    const std::size_t length = i - begin;

    // Single spaces between elements, which is how the patcher spells a list
    // everywhere else; a run of separators in the input therefore collapses.
    if (wroteToken) outText.push_back(' ');
    wroteToken = true;

    // Max's third argument stops after the first match — everything past it is
    // copied through untouched.
    const bool allowed = !firstOnly || !replaced;
    if (allowed && TokenMatches(value.c_str() + begin, length)) {
      outText.append(replacement.text);
      replaced = true;
    } else {
      outText.append(value.c_str() + begin, length);
    }
  }

  if (!replaced) {
    // Including a message with no elements at all: nothing matched, so it
    // leaves the rightmost outlet exactly as it arrived, spelling included.
    outputs[1].SendList(value, thread);
    return;
  }
  outputs[0].SendList(outText, thread);
}

LIST_IN(SetMatchList) {
  StoreToken(match, value.c_str(), value.size(), true);
}

INT_IN(SetMatchInt) {
  // ExprFormatValue is the patcher's number-to-text writer: no allocation, no
  // locale (so the separator is always '.', which is what the reader on the
  // other end expects) and no exception.
  char text[kExprValueTextMax];
  const int length = ExprFormatValue(ExprValue::Int(value), text, kExprValueTextMax);
  StoreToken(match, text, (std::size_t)length, true);
}

FLOAT_IN(SetMatchFloat) {
  char text[kExprValueTextMax];
  const int length = ExprFormatValue(ExprValue::Float(value), text, kExprValueTextMax);
  StoreToken(match, text, (std::size_t)length, true);
}

LIST_IN(SetReplacementList) {
  StoreToken(replacement, value.c_str(), value.size(), false);
}

INT_IN(SetReplacementInt) {
  char text[kExprValueTextMax];
  const int length = ExprFormatValue(ExprValue::Int(value), text, kExprValueTextMax);
  StoreToken(replacement, text, (std::size_t)length, false);
}

FLOAT_IN(SetReplacementFloat) {
  char text[kExprValueTextMax];
  const int length = ExprFormatValue(ExprValue::Float(value), text, kExprValueTextMax);
  StoreToken(replacement, text, (std::size_t)length, false);
}

#undef className
