#include "gSymbol.h"
#include "../../implementations/logImplementation.h"
#include "../math/gExprEval.h"
#include "../pListArgs.h"
#include "../pObjectList.hpp"
#include <cstring>

using namespace YSE::PATCHER;

#define className gSymbolBase

namespace {

  // The separators Parameters::Set and the list outlets use. Hand-rolled rather
  // than std::isspace, which reads locale state another thread may be mutating
  // and is undefined for a negative char.
  inline bool IsSeparator(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
  }

  // The longest list the patcher's own queues carry
  // (patcherImplementation::kValueListCap), so anything that can reach this
  // object through the patcher can also be converted by it without the output
  // buffer growing.
  constexpr std::size_t kValueListCap = 256;

  // Worst case for the join: alternating "x " makes a 256-character list out of
  // 128 tokens, each of which may be followed by a full-length separator. That
  // also covers the widest split, since expanding 256 characters into 256
  // single-character tokens costs 256 characters plus 255 single spaces.
  constexpr std::size_t kOutTextCapacity =
      kValueListCap + (((kValueListCap + 1) / 2) * gSymbolBase::SEPARATOR_CAPACITY) + 1;

  // Max's documented default for the `separator` attribute. See the class
  // documentation for why a space is the identity at the token level here, and
  // why that is left honest rather than papered over with quoting.
  constexpr char kDefaultSeparator = ' ';

  // The symbol a bang collapses to, and the one that expands back into a bang —
  // Max's "the word bang sent as a part of a symbol will be converted to a
  // message".
  constexpr const char* kBangSymbol = "bang";

  constexpr char kSeparatorInletDoc[] =
      "Sets the separator and sends nothing. A list sets its first token, a number its text. An "
      "empty message sets an empty separator. A separator longer than 16 characters is ignored.";

} // namespace

gSymbolBase::gSymbolBase(symbolDirection convert) : pObject(false), direction(convert) {
  // The message to convert. Every kind: Max routes them all through `anything`,
  // and the type of the message is half of what these two objects convert.
  ADD_IN_0;
  REG_BANG_IN(ConvertBang);
  REG_INT_IN(ConvertInt);
  REG_FLOAT_IN(ConvertFloat);
  REG_LIST_IN(ConvertList);

  // The separator, in its own inlet so inlet 0 keeps no reserved words in it.
  // Bang is declined rather than swallowed — see the class documentation.
  ADD_IN_1;
  REG_INT_IN(SetSeparatorInt);
  REG_FLOAT_IN(SetSeparatorFloat);
  REG_LIST_IN(SetSeparatorList);

  // ANY: .tosymbol always sends a list, but .fromsymbol restores whichever type
  // the symbol spells, and a reader should not have to know which object this
  // is to wire to it.
  ADD_OUT_ANY;

  ADD_PARAM(separatorArg);
  REG_PARM_CLEAR;
  REG_PARM_PARSE;

  // The two allocations the message path would otherwise need, taken here on
  // the control thread for the largest separator and the longest result the
  // object will produce.
  separator.reserve(SEPARATOR_CAPACITY);
  outText.reserve(kOutTextCapacity);

  // The default separator; ClearParams() is what a re-parse restores, so run it
  // here rather than repeating it.
  ClearParams();

  ADD_CATEGORY(pCategory::STRING);
}

void gSymbolBase::Document(const char* summary, const char* dataDoc, const char* outDoc,
                           const char* paramDoc) {
  ADD_DESCRIPTION(summary);
  INLET_DOC(0, "in", dataDoc, "");
  INLET_DOC(1, "separator", kSeparatorInletDoc, "one token, at most 16 characters");
  OUTLET_DOC(0, "out", outDoc, "");
  PARAM_DOC("separator", " ", paramDoc, "one token, at most 16 characters");
}

// A re-parse must not leave the previous separator standing: Parameters::Set()
// calls this before parsing and returns early on an empty argument string, so
// this is the only thing that makes `SetParams("")` a real reset — back to
// Max's default of a single space — rather than a no-op.
PARM_CLEAR() {
  separatorArg.clear();
  separator.assign(1, kDefaultSeparator);
}

PARM_PARSE() {
  // Control thread only (Parameters::Set), so unlike the inlet path this can
  // afford a reason. Refused rather than truncated, and the default left in
  // place: the two ends of a round trip have to agree on the separator exactly,
  // and half of one is a different separator.
  if (separatorArg.size() > SEPARATOR_CAPACITY) {
    INTERNAL::LogImpl().emit(E_ERROR, std::string("patcher: ") + Type() +
                                          " separator is longer than 16 characters; ignored");
    return;
  }
  if (separatorArg.empty()) return;
  separator.assign(separatorArg);
}

void gSymbolBase::SetSeparatorText(const char* text, std::size_t length) {
  std::size_t begin = 0;
  while (begin < length && IsSeparator(text[begin]))
    begin++;
  std::size_t end = begin;
  while (end < length && !IsSeparator(text[end]))
    end++;

  const std::size_t size = end - begin;
  // Refused rather than truncated, and silently, since this may be the audio
  // thread. Nothing there is not a malformed request but a real one — Max's
  // `separator` with no arguments, which empties it.
  if (size > SEPARATOR_CAPACITY) return;

  separator.assign(text + begin, size);
}

void gSymbolBase::Join(const char* text, std::size_t length, YSE::THREAD thread) {
  // Refilled immediately before the send rather than kept between them: the
  // send path is synchronous, so a patch looping the outlet back into an inlet
  // re-enters here inside the SendList below. Appends only, into capacity
  // reserved at construction.
  outText.clear();

  std::size_t i = 0;
  bool first = true;
  while (i < length) {
    while (i < length && IsSeparator(text[i]))
      i++;
    if (i >= length) break;

    const std::size_t begin = i;
    while (i < length && !IsSeparator(text[i]))
      i++;

    // Runs of whitespace collapse to one separator, and the ends are trimmed:
    // a stray separator would give the result an empty token that no .route
    // matches, which is the same hazard .prepend trims its stored message for.
    if (!first) outText.append(separator);
    outText.append(text + begin, i - begin);
    first = false;
  }

  // Always a list, whatever arrived: collapsing the type is half of what this
  // object does, and the point of it is that the result can be used as a name.
  outputs[0].SendList(outText, thread);
}

void gSymbolBase::AppendPiece(const char* text, std::size_t length) {
  // Dropped rather than emitted: an empty piece would read back as a doubled
  // separator, and a message with an empty token in it is one no .route
  // matches.
  if (length == 0) return;
  if (!outText.empty()) outText.push_back(' ');
  outText.append(text, length);
}

void gSymbolBase::SplitToken(const char* text, std::size_t length) {
  if (separator.empty()) {
    // Max's "individual characters in a symbol", which is the exact inverse of
    // joining with nothing at all: 1234 comes back as 1 2 3 4.
    for (std::size_t c = 0; c < length; c++)
      AppendPiece(text + c, 1);
    return;
  }

  const std::size_t sepLength = separator.size();
  const char* sep = separator.c_str();
  std::size_t begin = 0;
  std::size_t i = 0;
  while (i + sepLength <= length) {
    if (std::memcmp(text + i, sep, sepLength) == 0) {
      AppendPiece(text + begin, i - begin);
      i += sepLength;
      begin = i;
    } else {
      i++;
    }
  }
  AppendPiece(text + begin, length - begin);
}

void gSymbolBase::Split(const char* text, std::size_t length, YSE::THREAD thread) {
  outText.clear();

  // Whitespace first, then the separator inside each token. Splitting on
  // whitespace as well is not an extra rule but the same one: whitespace is
  // what separates tokens in this patcher, so a message that already has
  // several is already several symbols. It also makes the default separator —
  // a single space — fall out as plain whitespace normalisation rather than
  // needing a case of its own.
  std::size_t i = 0;
  while (i < length) {
    while (i < length && IsSeparator(text[i]))
      i++;
    if (i >= length) break;

    const std::size_t begin = i;
    while (i < length && !IsSeparator(text[i]))
      i++;

    SplitToken(text + begin, i - begin);
  }

  SendExpanded(thread);
}

void gSymbolBase::SendExpanded(YSE::THREAD thread) {
  // A type is a property of the whole message here, so only a result that came
  // out as a single token has one to restore. outText is built with single
  // spaces and no leading or trailing one, so "a single token" is exactly
  // "non-empty and contains no space".
  if (!outText.empty() && outText.find(' ') == std::string::npos) {
    if (outText == kBangSymbol) {
      outputs[0].SendBang(thread);
      return;
    }

    float number = 0.f;
    // Strict, as the rest of the family is: ExprParseFloatList would read
    // `5abc` as 5 and fold `1e999` to 0, and neither answers "is this token a
    // number at all" — which is the question here, since a token that is not
    // one has to stay the symbol it was typed as.
    if (ReadNumericToken(outText.c_str(), outText.size(), number)) {
      // .sel's and .match's test for how a number was spelled, so an int does
      // not come back with a decimal point it never had.
      if (TokenLooksLikeFloat(outText.c_str(), outText.size())) {
        outputs[0].SendFloat(number, thread);
      } else {
        outputs[0].SendInt(ExprToInt(number), thread);
      }
      return;
    }
  }

  outputs[0].SendList(outText, thread);
}

BANG_IN(ConvertBang) {
  // Registered on inlet 0 alone, so there is nothing to guard against here.
  (void)inlet;

  if (direction == symbolDirection::To) {
    // Max: bang "converts to symbol output" — the symbol `bang`, which is what
    // a .fromsymbol on the other end turns back into a bang.
    outText.assign(kBangSymbol);
    outputs[0].SendList(outText, thread);
    return;
  }

  // Max: "The message bang will simply pass through to the output."
  outputs[0].SendBang(thread);
}

INT_IN(ConvertInt) {
  (void)inlet;

  // Max: "Any integer will simply pass through to the output." Nothing to
  // expand — an int is already the thing a symbol would have been expanded to.
  if (direction == symbolDirection::From) {
    outputs[0].SendInt(value, thread);
    return;
  }

  // ExprFormatValue is the patcher's number-to-text writer: no allocation, no
  // locale (so the separator is always '.', which is what the reader on the
  // other end expects) and no exception.
  char text[kExprValueTextMax];
  const int length = ExprFormatValue(ExprValue::Int(value), text, kExprValueTextMax);
  outText.assign(text, length > 0 ? (std::size_t)length : 0);
  outputs[0].SendList(outText, thread);
}

FLOAT_IN(ConvertFloat) {
  (void)inlet;

  if (direction == symbolDirection::From) {
    outputs[0].SendFloat(value, thread);
    return;
  }

  // A float keeps its decimal point, so it still reads as a float when a
  // .fromsymbol on the other end restores the type.
  char text[kExprValueTextMax];
  const int length = ExprFormatValue(ExprValue::Float(value), text, kExprValueTextMax);
  outText.assign(text, length > 0 ? (std::size_t)length : 0);
  outputs[0].SendList(outText, thread);
}

LIST_IN(ConvertList) {
  (void)inlet;

  if (direction == symbolDirection::To) {
    Join(value.c_str(), value.size(), thread);
    return;
  }
  Split(value.c_str(), value.size(), thread);
}

LIST_IN(SetSeparatorList) {
  (void)inlet;
  SetSeparatorText(value.c_str(), value.size());
}

INT_IN(SetSeparatorInt) {
  (void)inlet;
  char text[kExprValueTextMax];
  const int length = ExprFormatValue(ExprValue::Int(value), text, kExprValueTextMax);
  SetSeparatorText(text, length > 0 ? (std::size_t)length : 0);
}

FLOAT_IN(SetSeparatorFloat) {
  (void)inlet;
  char text[kExprValueTextMax];
  const int length = ExprFormatValue(ExprValue::Float(value), text, kExprValueTextMax);
  SetSeparatorText(text, length > 0 ? (std::size_t)length : 0);
}

#undef className

// ─── the two directions ───────────────────────────────────────────────────────

// gSymbol.h explains the shared design: a symbol is a one-token list, the
// default separator is a space (the identity at the token level), and no
// quotes are added around a result. Both directions are one bounded walk into
// a buffer reserved at construction for the longest list the patcher's queues
// carry, so no message path allocates, locks or blocks, and Calculate() does
// nothing.
gToSymbol::gToSymbol() : gSymbolBase(symbolDirection::To) {
  Document("Joins the tokens of a message into one symbol, with the separator between them: with "
           "separator /, 'voice 3 freq' becomes voice/3/freq. A number or a bang becomes the "
           "text that spells it.",
           "The message to convert. A list has its tokens joined with the separator. A bang "
           "becomes the word bang, and a number becomes its text.",
           "The result, always as a list. With a separator that is not a space, it is one token.",
           "The separator placed between tokens. A single space when not given, which only "
           "evens out the spacing.");
}

gFromSymbol::gFromSymbol() : gSymbolBase(symbolDirection::From) {
  Document("Splits a symbol on its separator into a message: with separator /, voice/3/freq "
           "becomes 'voice 3 freq'. A result that is one number or the word bang leaves as an int, "
           "a float or a bang. The inverse of .tosymbol.",
           "The message to split. A bang, an int or a float passes through as it is.",
           "The split message. A one-token result leaves as an int, a float or a bang when it "
           "reads as one, and as a list otherwise.",
           "The separator to split on. A single space when not given, which only evens out the "
           "spacing. Empty splits the symbol into single characters.");
}
