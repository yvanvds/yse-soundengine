#include "gSpell.h"
#include "../../implementations/logImplementation.h"
#include "../math/gExprEval.h"
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

  /**
   *  Reads the code point starting at @p cursor and advances @p cursor past it,
   *  stopping at @p end.
   *
   *  A well-formed UTF-8 sequence yields the one code point it spells. Anything
   *  that is not one — a stray continuation byte, a sequence truncated by @p end,
   *  an overlong encoding, a surrogate half, or a value above U+10FFFF — yields
   *  the *lead byte's own value*, 0-255, and the cursor advances by one so
   *  decoding resumes at the next byte.
   *
   *  That fallback is deliberate and documented on the class: a patcher message
   *  is bytes, nothing upstream promises an encoding, and an object that threw
   *  away what it could not decode would silently lose characters from a patch
   *  fed Latin-1 or raw binary. No allocation, no locale, no failure path.
   */
  inline unsigned int NextCodePoint(const unsigned char* bytes, std::size_t end,
                                    std::size_t& cursor) {
    const unsigned int lead = bytes[cursor];
    if (lead < 0x80u) {
      cursor++;
      return lead;
    }

    // How many continuation bytes the lead announces, and the bits it carries
    // itself. A continuation byte in lead position, and the 5- and 6-byte forms
    // UTF-8 has not permitted since 2003, fall through as their own value.
    std::size_t extra = 0;
    unsigned int code = 0;
    if ((lead & 0xE0u) == 0xC0u) {
      extra = 1;
      code = lead & 0x1Fu;
    } else if ((lead & 0xF0u) == 0xE0u) {
      extra = 2;
      code = lead & 0x0Fu;
    } else if ((lead & 0xF8u) == 0xF0u) {
      extra = 3;
      code = lead & 0x07u;
    } else {
      cursor++;
      return lead;
    }

    if (cursor + extra >= end) {
      cursor++;
      return lead;
    }
    for (std::size_t k = 1; k <= extra; k++) {
      if ((bytes[cursor + k] & 0xC0u) != 0x80u) {
        cursor++;
        return lead;
      }
    }
    for (std::size_t k = 1; k <= extra; k++)
      code = (code << 6) | (bytes[cursor + k] & 0x3Fu);

    // Overlong encodings spell a code point that had a shorter form, surrogates
    // are not characters, and nothing above U+10FFFF exists. Each of the three is
    // a byte sequence that is *not* well-formed UTF-8, so each takes the same
    // route as any other malformed byte.
    static constexpr unsigned int kSmallest[4] = {0u, 0x80u, 0x800u, 0x10000u};
    if (code < kSmallest[extra] || code > 0x10FFFFu || (code >= 0xD800u && code <= 0xDFFFu)) {
      cursor++;
      return lead;
    }

    cursor += extra + 1;
    return code;
  }

  constexpr char kInletDoc[] =
      "The message to spell. Whatever arrives is spelled out as the character codes of its text: "
      "an "
      "int digit by digit (123 spells 49 50 51), a float including its decimal point, and a list "
      "character by character with the code 32 between its items, which is Max's four separate "
      "rules falling out of the one rule this patcher's message model allows. Runs of whitespace "
      "collapse to a single 32 and the ends are trimmed. A bang is declined: Max's spell has none, "
      "and there is nothing for it to spell.";

  constexpr char kOutletDoc[] =
      "The codes, as one list, in the order the characters appear — 'hi' leaves as 104 105. Max "
      "sends them one at a time; they leave together here because nothing in this patcher "
      "accumulates a stream of numbers back into a message, so one-at-a-time codes could not be "
      "put back together, while a list can be taken apart by a .spray whenever a patch wants them "
      "separately. Nothing is sent when there is nothing to spell and "
      "no minimum size to pad to, and nothing is sent at all when the input would spell past 256 "
      "codes — refused rather than truncated.";

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

  ADD_DESCRIPTION(
      "Spells whatever arrives out as the character codes of its text — Max's spell, 'convert "
      "input to UTF-8 (Unicode) codes', so text can be processed numerically inside a patch. Max "
      "documents four rules, one per atom type: an int is spelled digit by digit, a symbol "
      "character by character, a list with a space (32) between its items, and anything with all "
      "its int and symbol items converted one character at a time. They collapse into a single "
      "rule here, and not by cutting a corner: a message in this patcher is text, with no atom "
      "types at all, so .spell sends the code of every character of the message as it stands and "
      "each of Max's four rules falls out of that — a number's digits are its text, written by "
      "ExprFormatValue the way a patch author would have typed it, and a list gets its 32 because "
      "a space is the character that is actually between its items. Runs of whitespace collapse to "
      "one space and the ends are trimmed, so what is spelled is the tokens with a single 32 "
      "between them, Max's own spacing, rather than however much incidental whitespace the sender "
      "left in. The encoding is stated rather than assumed, because a patcher message is bytes and "
      "nothing upstream promises otherwise: bytes that form a well-formed UTF-8 sequence are "
      "decoded to the one code point they spell, so the two bytes of an accented letter give one "
      "code and not two, and any byte that is not part of a well-formed sequence — a stray "
      "continuation byte, a truncated sequence, an overlong encoding, a surrogate half — spells "
      "its own value 0-255 with decoding resuming at the next byte, so the object never fails, "
      "never drops a byte and never guesses. Max's two creation arguments are kept: size is the "
      "minimum number of codes, padded to with the fill code, and fill is that code, 32 (a space) "
      "by default. They are creation arguments rather than reserved words on inlet 0 — the "
      ".prepend discipline, which matters here because this object exists to carry arbitrary text "
      "and a reserved word would swallow the very messages it is asked to spell. Max's 'use a "
      "negative number to get 0 as the fill character' is not reproduced: it exists only because "
      "Max cannot tell an absent argument from a zero, whereas arguments are read as tokens here, "
      "so .spell 8 48 says 'pad with the character 0' directly and a negative fill is reported and "
      "ignored instead of silently becoming 48. The codes leave together as one list rather than "
      "one at a time, since nothing in this patcher accumulates a stream of numbers back into a "
      "message. At most 256 codes; longer input is refused rather than truncated, because half a "
      "spelling is not a shorter word but a different one. Calculate() does nothing, and no "
      "message path allocates, locks or blocks: a spelling is one walk of the bytes into a buffer "
      "reserved at construction for every code at full width.");
  ADD_CATEGORY(pCategory::GENERIC);

  INLET_DOC(0, "in", kInletDoc, "any");
  OUTLET_DOC(0, "out", kOutletDoc, "0-1114111 per code, at most 256 codes");

  PARAM_DOC("size", "0",
            "Max's size argument: the minimum number of codes to send. A message that spells to "
            "fewer is followed by enough fill codes to reach it, which is how a patch gets "
            "fixed-width records out of variable-length text. 0, the default, means no minimum. "
            "Clamped to 256, the most codes the object will send at all.",
            "0-256");
  PARAM_DOC("fill", "32",
            "Max's character argument: the code short output is padded with, 32 (a space) by "
            "default. Taken literally, so 0 is the code 0 and .spell 8 48 pads with the character "
            "'0' — Max's 'use any negative number' escape is not reproduced, because it only "
            "exists to work around Max being unable to tell an absent argument from a zero. A "
            "negative code, or one above 1114111, is reported and the default kept.",
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
