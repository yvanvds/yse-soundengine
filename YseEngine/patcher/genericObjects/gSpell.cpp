#include "gSpell.h"
#include "../../implementations/logImplementation.h"
#include "../math/gExprEval.h"
#include "../pCharCodes.h"
#include "../pListArgs.h"
#include "../pObjectList.hpp"

using namespace YSE::PATCHER;

#define className gSpell

namespace {

  // The separators the patcher's list outlets use. Hand-rolled rather than
  // std::isspace, which reads locale state another thread may be mutating and is
  // undefined for a negative char.
  inline bool IsSeparator(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
  }

  // Max's "a space character (32) is sent out between items in the list" — not a
  // rule of its own here, but the code of the character that is actually between
  // them once the message's whitespace has been normalised.
  constexpr unsigned int kSpaceCode = 32;

  // The UTF-8 decoder this object reads a character with lives in
  // ../pCharCodes.h. It was written here and moved there when .atoi (#493)
  // needed it: the two objects spell the same message, so they have to agree
  // about what a character is byte for byte, and a second copy of the decoder
  // is exactly the kind of thing that drifts.

  constexpr char kInletDoc[] =
      "The message to spell: an int digit by digit (123 gives 49 50 51), a float including its "
      "decimal point, a list character by character with 32 between its items.";

  constexpr char kOutletDoc[] =
      "The codes as one list, in the order the characters appear: 'hi' leaves as 104 105. Nothing "
      "is sent when there is nothing to spell, or when the result would pass 256 codes.";

} // namespace

CONSTRUCT() {
  // The message to spell. Every kind but a bang: Max's spell documents int,
  // symbol, list and anything, all of which are one thing here, and no bang —
  // so no bang handler is registered and inlet::GetAcceptedTypes() keeps
  // reporting the real contract.
  ADD_IN_0;
  REG_INT_IN(SpellInt);
  REG_FLOAT_IN(SpellFloat);
  REG_LIST_IN(SpellList);

  // Always a list of numbers, whatever arrived, and deliberately so: a
  // one-character message must not leave as a bare int, or a patch would break
  // the moment the word it spells grows a second letter.
  ADD_OUT_LIST;

  ADD_PARAM(minimumSize);
  ADD_PARAM(fillCode);
  // The clear callback is what makes a re-parse a real reset: Parameters::Set
  // writes only the arguments it is given, so without it `.spell 8 45` followed
  // by SetParams("4") would keep the fill of 45 that the new argument list does
  // not mention.
  REG_PARM_CLEAR;
  REG_PARM_PARSE;

  // The one allocation a spelling would otherwise need, taken here on the
  // control thread: MAX_CODES codes, each at its widest, plus a separator each.
  outText.reserve((MAX_CODES * (FORMAT_INT_WIDTH + 1)) + 1);

  // gSpell.h explains the design: one conversion rule for every kind of
  // message, the UTF-8 reading shared with .atoi through pCharCodes.h, the
  // codes leaving as one list, and refusal rather than truncation at 256. A
  // spelling is one walk of the bytes into the buffer reserved above, so no
  // message path allocates, locks or blocks, and Calculate() does nothing.
  ADD_DESCRIPTION("Spells a message out as the character codes of its text, sent as one list: 'hi' "
                  "gives 104 105. Optional arguments pad a short result to a minimum length with "
                  "a fill code.");
  ADD_CATEGORY(pCategory::STRING);

  INLET_DOC(0, "in", kInletDoc, "any");
  OUTLET_DOC(0, "out", kOutletDoc, "0-1114111 per code, at most 256 codes");

  PARAM_DOC("size", "0",
            "The minimum number of codes to send. A shorter result is padded with the fill code. "
            "0 means no minimum. Clamped to 256.",
            "0-256");
  PARAM_DOC("fill", "32",
            "The code a short result is padded with: 32 (a space) by default, 48 for the "
            "character '0'. A negative code, or one above 1114111, is logged and the default "
            "kept.",
            "0-1114111");
}

// Runs on the control thread before the argument string is re-read, and is the
// whole of SetParams(""): Parameters::Set returns without calling the parse
// callback for an empty argument string, and it writes only the arguments it is
// actually given, so this is what keeps a shorter argument list from inheriting
// the previous one's values.
PARM_CLEAR() {
  minimumSize = 0;
  fillCode = DEFAULT_FILL;
}

PARM_PARSE() {
  // Control thread only (Parameters::Set), so unlike the inlet path this can
  // afford a reason for what it does.
  if (minimumSize < 0) {
    INTERNAL::LogImpl().emit(E_ERROR, std::string("patcher: ") + Type() +
                                          " minimum size cannot be negative; no minimum used");
    minimumSize = 0;
  } else if ((std::size_t)minimumSize > MAX_CODES) {
    INTERNAL::LogImpl().emit(E_ERROR, std::string("patcher: ") + Type() +
                                          " sends at most 256 codes; minimum size clamped to 256");
    minimumSize = (int)MAX_CODES;
  }

  // Max reads a negative fill as the character '0', because it cannot tell an
  // absent argument from a zero. This one can, so a negative fill names no
  // character at all and is refused rather than quietly turned into 48 — which
  // would be a trap for anyone who wrote -1 meaning something else.
  if (fillCode < 0) {
    INTERNAL::LogImpl().emit(E_ERROR,
                             std::string("patcher: ") + Type() +
                                 " fill is a character code, so it cannot be negative; write the "
                                 "code you want (48 for '0'). Default of 32 kept");
    fillCode = DEFAULT_FILL;
  } else if (fillCode > MAX_CODE_POINT) {
    INTERNAL::LogImpl().emit(E_ERROR, std::string("patcher: ") + Type() +
                                          " fill is above the highest code point; default of 32 "
                                          "kept");
    fillCode = DEFAULT_FILL;
  }
}

bool gSpell::AppendCode(unsigned int code, std::size_t& count) {
  // Refused rather than truncated: the caller abandons the whole spelling on a
  // false, because half a spelling is a different word rather than a shorter one
  // and nothing downstream could tell the two apart.
  if (count >= MAX_CODES) return false;

  char digits[FORMAT_INT_WIDTH];
  const std::size_t written = WriteInt((int)code, digits);
  if (!outText.empty()) outText.push_back(' ');
  outText.append(digits, written);
  count++;
  return true;
}

void gSpell::Spell(const char* text, std::size_t length, YSE::THREAD thread) {
  // Filled immediately before the send rather than kept between them: the send
  // path is synchronous, so a patch looping the outlet back into an inlet
  // re-enters this function inside the SendList below. Appends only, into
  // capacity reserved at construction.
  outText.clear();
  std::size_t count = 0;

  // Signed char is undefined territory for the bit tests the decoder does, and
  // the codes it produces are values rather than characters.
  const unsigned char* bytes = reinterpret_cast<const unsigned char*>(text);

  std::size_t i = 0;
  bool first = true;
  while (i < length) {
    while (i < length && IsSeparator(text[i]))
      i++;
    if (i >= length) break;

    const std::size_t begin = i;
    while (i < length && !IsSeparator(text[i]))
      i++;

    // Runs of whitespace collapse to one code and the ends are trimmed, which
    // is both Max's spacing for a list and the whitespace discipline the rest
    // of the family keeps.
    if (!first && !AppendCode(kSpaceCode, count)) {
      outText.clear();
      return;
    }
    first = false;

    // Bounded by the token's end, so a multi-byte sequence can never be read
    // across the whitespace that ends it.
    std::size_t cursor = begin;
    while (cursor < i) {
      if (!AppendCode(NextCodePoint(bytes, i, cursor), count)) {
        outText.clear();
        return;
      }
    }
  }

  // Max: "any input that doesn't 'spell' to the minimum length is followed by
  // enough fill characters". minimumSize is clamped to MAX_CODES when it is
  // read, so this loop cannot be the one that overruns.
  while (count < (std::size_t)minimumSize) {
    if (!AppendCode((unsigned int)fillCode, count)) {
      outText.clear();
      return;
    }
  }

  // Nothing to spell and no minimum to pad to: inert rather than a source of
  // empty messages — the .prepend / .sprintf / .combine rule that makes an
  // object safe to drop into a working patch.
  if (outText.empty()) return;

  outputs[0].SendList(outText, thread);
}

INT_IN(SpellInt) {
  (void)inlet;
  // Max: "the ASCII value of each of the digits of the number". The digits are
  // the number's text, and ExprFormatValue is the patcher's writer for it: no
  // allocation, no locale, no exception.
  char text[kExprValueTextMax];
  const int length = ExprFormatValue(ExprValue::Int(value), text, kExprValueTextMax);
  Spell(text, length > 0 ? (std::size_t)length : 0, thread);
}

FLOAT_IN(SpellFloat) {
  (void)inlet;
  // A float keeps its decimal point, so the point is spelled too (46) — the
  // same rule, applied to the text a float actually has.
  char text[kExprValueTextMax];
  const int length = ExprFormatValue(ExprValue::Float(value), text, kExprValueTextMax);
  Spell(text, length > 0 ? (std::size_t)length : 0, thread);
}

LIST_IN(SpellList) {
  (void)inlet;
  Spell(value.c_str(), value.size(), thread);
}

#undef className
