#include "gCharCode.h"
#include "../math/gExprEval.h"
#include "../pCharCodes.h"
#include "../pListArgs.h"
#include "../pObjectList.hpp"

using namespace YSE::PATCHER;

#define className gCharCodeBase

namespace {

  // The separators the patcher's list outlets use. Hand-rolled rather than
  // std::isspace, which reads locale state another thread may be mutating and is
  // undefined for a negative char.
  inline bool IsSeparator(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
  }

  // Max's "a space character (ASCII value 32) is inserted between items" — not a
  // rule of its own here, but the code of the character that is actually between
  // them once the message's whitespace has been normalised.
  constexpr unsigned int kSpaceCode = 32;

  // Widest the contents can get, per direction. Codes are written as decimal
  // with a separator each; characters are written as UTF-8.
  constexpr std::size_t kCodesCapacity = (gCharCodeBase::MAX_CODES * (FORMAT_INT_WIDTH + 1)) + 1;
  constexpr std::size_t kTextCapacity = (gCharCodeBase::MAX_CODES * UTF8_MAX_BYTES) + 1;

  constexpr char kAtoiInletDoc[] =
      "The message to convert. Whatever arrives replaces the stored codes and the result goes "
      "straight out: an int digit by digit (123 gives 49 50 51), a float including its decimal "
      "point, and a list character by character with the code 32 between its items, which is Max's "
      "four separate rules falling out of the one rule this patcher's message model allows. A bang "
      "sends the stored codes again without converting anything. Runs of whitespace collapse to a "
      "single 32 and the ends are trimmed. An empty message stores nothing and sends nothing.";

  constexpr char kAtoiAppendDoc[] =
      "Max's middle inlet: the codes of whatever arrives are added to the stored codes and nothing "
      "is sent. Appending is at the code level, so 'a' then 'b' gives 97 98 — the same as the one "
      "message 'ab', and not the 97 32 98 that the two-token message 'a b' gives. A message that "
      "would take the stored codes past 256 is refused whole, leaving what was already there "
      "untouched.";

  constexpr char kAtoiStoreDoc[] =
      "Max's right inlet: the codes of whatever arrives replace the stored codes and nothing is "
      "sent, so a patch can load the object and send it later with a bang on inlet 0. An empty "
      "message here is Max's clear — replacing the contents with nothing is what clearing them "
      "is, and it needs no reserved word on the inlet that has to carry arbitrary text.";

  constexpr char kAtoiOutletDoc[] =
      "The stored codes, as one list, in the order the characters appear — 'hi' leaves as 104 105. "
      "Max sends them one at a time; they leave together here because nothing in this patcher "
      "accumulates a stream of numbers back into a message, while a list can be taken apart by a "
      ".spray whenever a patch wants them separately. Nothing is sent when there is nothing "
      "stored.";

  constexpr char kItoaInletDoc[] =
      "The character codes to convert. An int, a float (truncated, Max's 'converted to an int') or "
      "a list of them replaces the stored characters and the result goes straight out; a bang "
      "sends the stored characters again without converting anything. A token that is not a "
      "number, or a number that is not a code point — negative, above 1114111, or a surrogate half "
      "— makes the whole message refused, since there is no character to write in its place. An "
      "empty message stores nothing and sends nothing.";

  constexpr char kItoaAppendDoc[] =
      "Max's middle inlet: the characters the arriving codes spell are added to the stored "
      "characters and nothing is sent — how a patch builds up a symbol one character at a time and "
      "sends it with a bang once it is complete. A message that would take the stored characters "
      "past 256 is refused whole, leaving what was already there untouched.";

  constexpr char kItoaStoreDoc[] =
      "Max's right inlet: the characters the arriving codes spell replace the stored characters "
      "and nothing is sent. An empty message here is Max's clear — replacing the contents with "
      "nothing is what clearing them is, and it needs no reserved word on the inlet that has to "
      "carry arbitrary codes.";

  constexpr char kItoaOutletDoc[] =
      "The stored characters, as a list — 104 105 leaves as 'hi'. Always a list, whatever the "
      "codes spell: a result that happens to read as a number is still the text those codes wrote, "
      "and a patch that wants the number back sends it through a .fromsymbol, which is the object "
      "whose job restoring a type is. Nothing is sent when there is nothing stored.";

} // namespace

gCharCodeBase::gCharCodeBase(charCodeDirection convert) : pObject(false), direction(convert) {
  // The message to convert, and the one inlet that sends. Bang included: it is
  // Max's "trigger the output of the currently stored" value, and the only
  // thing on either object that emits without converting.
  ADD_IN_0;
  REG_BANG_IN(ResendBang);
  REG_INT_IN(ReplaceInt);
  REG_FLOAT_IN(ReplaceFloat);
  REG_LIST_IN(ReplaceList);

  // Max's middle inlet: add to the contents, send nothing. No bang — there is
  // nothing for one to append — so it stays out of GetAcceptedTypes().
  ADD_IN_1;
  REG_INT_IN(AppendInt);
  REG_FLOAT_IN(AppendFloat);
  REG_LIST_IN(AppendList);

  // Max's right inlet: replace the contents, send nothing. An empty message
  // here is Max's `clear`.
  ADD_IN_2;
  REG_INT_IN(StoreInt);
  REG_FLOAT_IN(StoreFloat);
  REG_LIST_IN(StoreList);

  // Always a list, whatever the conversion produced: a one-character result
  // must not leave as a bare int, or a patch would break the moment the word it
  // carries grows a second letter.
  ADD_OUT_LIST;

  // The three allocations the message path would otherwise need, taken here on
  // the control thread for the widest result this direction can produce.
  const std::size_t capacity =
      direction == charCodeDirection::ToCodes ? kCodesCapacity : kTextCapacity;
  store.reserve(capacity);
  scratch.reserve(capacity);
  sendBuffer.reserve(capacity);

  ADD_CATEGORY(pCategory::GENERIC);
}

void gCharCodeBase::Document(const char* summary, const char* dataDoc, const char* appendDoc,
                             const char* storeDoc, const char* outDoc) {
  ADD_DESCRIPTION(summary);
  INLET_DOC(0, "in", dataDoc, "");
  INLET_DOC(1, "append", appendDoc, "at most 256 characters in total");
  INLET_DOC(2, "set", storeDoc, "at most 256 characters in total");
  OUTLET_DOC(0, "out", outDoc, "");
}

bool gCharCodeBase::Build(const char* text, std::size_t length, std::size_t& codes) {
  // Built here rather than straight into the contents so a refusal leaves
  // nothing half-applied: the caller only commits once this has returned true.
  // Appends only, into capacity reserved at construction.
  scratch.clear();
  codes = 0;

  if (direction == charCodeDirection::ToCodes) {
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
      if (!first) {
        if (codes >= MAX_CODES) return false;
        if (!scratch.empty()) scratch.push_back(' ');
        char digits[FORMAT_INT_WIDTH];
        scratch.append(digits, WriteInt((int)kSpaceCode, digits));
        codes++;
      }
      first = false;

      // Bounded by the token's end, so a multi-byte sequence can never be read
      // across the whitespace that ends it.
      std::size_t cursor = begin;
      while (cursor < i) {
        const unsigned int code = NextCodePoint(bytes, i, cursor);
        if (codes >= MAX_CODES) return false;
        if (!scratch.empty()) scratch.push_back(' ');
        char digits[FORMAT_INT_WIDTH];
        scratch.append(digits, WriteInt((int)code, digits));
        codes++;
      }
    }
    return true;
  }

  // .itoa: every token has to be a number, and every number a code point.
  std::size_t i = 0;
  while (i < length) {
    while (i < length && IsSeparator(text[i]))
      i++;
    if (i >= length) break;

    const std::size_t begin = i;
    while (i < length && !IsSeparator(text[i]))
      i++;

    // Strict, as the rest of the family is: ExprParseFloatList would read
    // `5abc` as 5 and fold `1e999` to 0, and neither answers "is this token a
    // number at all" — which is the question here, since a token that is not
    // one names no character.
    float number = 0.f;
    if (!ReadNumericToken(text + begin, i - begin, number)) return false;

    // Max's "a float is converted to an int".
    const int code = ExprToInt(number);
    if (!IsCodePoint(code)) return false;
    if (codes >= MAX_CODES) return false;

    char utf8[UTF8_MAX_BYTES];
    scratch.append(utf8, WriteCodePoint((unsigned int)code, utf8));
    codes++;
  }
  return true;
}

void gCharCodeBase::Accept(const char* text, std::size_t length, bool replace, bool emit,
                           YSE::THREAD thread) {
  std::size_t codes = 0;
  // Refused whole rather than truncated, and silently, since this may be the
  // audio thread. Half a name is a different name rather than a shorter one,
  // and a patch assembling one a piece at a time could not tell that a piece
  // had gone missing.
  if (!Build(text, length, codes)) return;

  if (replace) {
    store.assign(scratch);
    count = codes;
  } else {
    if (count + codes > MAX_CODES) return;
    // At the code level, with no separator invented between one message and the
    // next: Max's middle inlet adds characters, not items.
    if (direction == charCodeDirection::ToCodes && !store.empty() && !scratch.empty())
      store.push_back(' ');
    store.append(scratch);
    count += codes;
  }

  if (emit) Send(thread);
}

void gCharCodeBase::AcceptValue(bool isInt, int intValue, float floatValue, bool replace, bool emit,
                                YSE::THREAD thread) {
  // ExprFormatValue is the patcher's number-to-text writer: no allocation, no
  // locale (so the decimal separator is always '.', which is what the reader on
  // the other end expects) and no exception.
  char text[kExprValueTextMax];
  const int length = ExprFormatValue(
      isInt ? ExprValue::Int(intValue) : ExprValue::Float(floatValue), text, kExprValueTextMax);
  Accept(text, length > 0 ? (std::size_t)length : 0, replace, emit, thread);
}

void gCharCodeBase::Send(YSE::THREAD thread) {
  // Nothing stored: inert rather than a source of empty messages — the
  // .prepend / .sprintf / .spell rule that makes an object safe to drop into a
  // working patch.
  if (store.empty()) return;

  // A copy, into capacity reserved at construction: the send path is
  // synchronous, so a patch looping the outlet back into inlet 1 would
  // otherwise append to the very string being handed to the targets that have
  // not been reached yet.
  sendBuffer.assign(store);
  outputs[0].SendList(sendBuffer, thread);
}

BANG_IN(ResendBang) {
  // Registered on inlet 0 alone, so there is nothing to guard against here.
  (void)inlet;
  Send(thread);
}

INT_IN(ReplaceInt) {
  (void)inlet;
  AcceptValue(true, value, 0.f, true, true, thread);
}

FLOAT_IN(ReplaceFloat) {
  (void)inlet;
  AcceptValue(false, 0, value, true, true, thread);
}

LIST_IN(ReplaceList) {
  (void)inlet;
  Accept(value.c_str(), value.size(), true, true, thread);
}

INT_IN(AppendInt) {
  (void)inlet;
  AcceptValue(true, value, 0.f, false, false, thread);
}

FLOAT_IN(AppendFloat) {
  (void)inlet;
  AcceptValue(false, 0, value, false, false, thread);
}

LIST_IN(AppendList) {
  (void)inlet;
  Accept(value.c_str(), value.size(), false, false, thread);
}

INT_IN(StoreInt) {
  (void)inlet;
  AcceptValue(true, value, 0.f, true, false, thread);
}

FLOAT_IN(StoreFloat) {
  (void)inlet;
  AcceptValue(false, 0, value, true, false, thread);
}

LIST_IN(StoreList) {
  (void)inlet;
  Accept(value.c_str(), value.size(), true, false, thread);
}

#undef className

// ─── the two directions ───────────────────────────────────────────────────────

gAtoi::gAtoi() : gCharCodeBase(charCodeDirection::ToCodes) {
  Document(
      "Converts the characters of a message into their character codes, and holds them — Max's "
      "atoi, 'convert characters to integers'. Max documents four rules, one per atom type: an int "
      "is converted digit by digit, a symbol character by character, a list with a space (32) "
      "between its items, and anything with all its items converted one character at a time. They "
      "collapse into a single rule here, and not by cutting a corner: a message in this patcher is "
      "text, with no atom types at all, so .atoi sends the code of every character of the message "
      "as it stands and each of Max's four rules falls out of that — a number's digits are its "
      "text, and a list gets its 32 because a space is the character that is actually between its "
      "items. Runs of whitespace collapse to one space and the ends are trimmed. The encoding is "
      "the one .spell states and shares with this object, character for character: bytes forming a "
      "well-formed UTF-8 sequence give the one code point they spell, so the two bytes of an "
      "accented letter give one code and not two, and any byte that is not part of a well-formed "
      "sequence spells its own value 0-255 with decoding resuming at the next byte, so the object "
      "never fails, never drops a byte and never guesses. What .spell does not have is the state: "
      "this object holds what it converted, so a bang sends it again, inlet 1 adds to it a piece "
      "at a time without sending, and inlet 2 loads it silently — Max's three inlets. An empty "
      "message on inlet 2 is Max's clear, spelled as an empty message rather than a reserved word "
      "because inlet 0 has to be able to carry the word 'clear' as text like any other. At most "
      "256 codes; a message that would take it past that is refused whole rather than truncated, "
      "leaving what was already stored untouched, because half a spelling is a different word "
      "rather than a shorter one. Pair it with .itoa, which converts codes back into the "
      "characters they spell and returns exactly the message that went in. Calculate() does "
      "nothing, and no message path allocates, locks or blocks.",
      kAtoiInletDoc, kAtoiAppendDoc, kAtoiStoreDoc, kAtoiOutletDoc);
}

gItoa::gItoa() : gCharCodeBase(charCodeDirection::ToText) {
  Document(
      "Converts character codes into the characters they spell, and holds them — Max's itoa, "
      "'convert a stream or list of up to 256 integer character codes into a symbol'. The inverse "
      "of .atoi, and the direction nothing in the message-construction family could go before: "
      ".spell and .atoi leave the text model and hand a patch numbers, and until now nothing "
      "brought it back, so codes computed inside a patch could never become a name, a path or a "
      "word again. A list of codes gives the text they spell, an int or a float gives the one "
      "character it names (a float truncated, Max's 'converted to an int'), and the result leaves "
      "as a list. Always a list, whatever the codes spell: a result that happens to read as a "
      "number is still the text those codes wrote, and a patch that wants a type restored sends it "
      "through a .fromsymbol, whose job that is. The codes are written as UTF-8 by the exact "
      "inverse of the decoder .atoi and .spell read with, which is what makes the pair a real "
      "round trip — .atoi after .itoa gives back the codes that went in, and .itoa after .atoi "
      "gives back the message. A token that is not a number, or a number that is not a code point "
      "— negative, above 1114111, or a surrogate half — makes the whole message refused rather "
      "than replaced with something arbitrary, since a substituted character would come back from "
      "an .atoi as a different code and break that guarantee. Max's three inlets are kept: inlet 0 "
      "converts and sends, a bang sends what is held again, inlet 1 adds characters without "
      "sending — which is how a patch builds a symbol one character at a time and sends it once it "
      "is complete — and inlet 2 loads it silently. An empty message on inlet 2 is Max's clear. At "
      "most 256 characters; a message that would take it past that is refused whole, leaving what "
      "was already stored untouched. Calculate() does nothing, and no message path allocates, "
      "locks or blocks.",
      kItoaInletDoc, kItoaAppendDoc, kItoaStoreDoc, kItoaOutletDoc);
}
