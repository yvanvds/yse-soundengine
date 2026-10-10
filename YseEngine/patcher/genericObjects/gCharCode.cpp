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
      "The message to convert. It replaces the stored codes and the codes are sent. A bang sends "
      "the stored codes again. An empty message clears them and sends nothing.";

  constexpr char kAtoiAppendDoc[] =
      "Adds the codes of the message to the stored codes and sends nothing. Characters are added "
      "with no space between messages: 'a' then 'b' gives 97 98.";

  constexpr char kAtoiStoreDoc[] =
      "Replaces the stored codes with the codes of the message and sends nothing. An empty "
      "message clears them.";

  constexpr char kAtoiOutletDoc[] =
      "The stored codes as one list, in the order the characters appear: 'hi' leaves as 104 105.";

  constexpr char kItoaInletDoc[] =
      "The character codes to convert: an int, a float (truncated) or a list of them. They "
      "replace the stored text and the text is sent. A bang sends the stored text again. An "
      "empty message clears it and sends nothing.";

  constexpr char kItoaAppendDoc[] =
      "Adds the characters the codes spell to the stored text and sends nothing. Use it to build "
      "a word one character at a time, then bang inlet 0 to send it.";

  constexpr char kItoaStoreDoc[] =
      "Replaces the stored text with the characters the codes spell and sends nothing. An empty "
      "message clears it.";

  constexpr char kItoaOutletDoc[] =
      "The stored text as a list: 104 105 leaves as 'hi'. Always a list, even when the text reads "
      "as a number.";

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

  ADD_CATEGORY(pCategory::STRING);
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

// gCharCode.h explains the shared design: one conversion rule for every kind
// of message, the UTF-8 reading, the three inlets with an empty message as
// the clear, and refusal rather than truncation at 256. No message path
// allocates, locks or blocks, and Calculate() does nothing.
gAtoi::gAtoi() : gCharCodeBase(charCodeDirection::ToCodes) {
  Document("Converts a message into the character codes of its text and holds them: 'hi' gives "
           "104 105. A bang sends the held codes again, and the other two inlets add to them or "
           "replace them without sending.",
           kAtoiInletDoc, kAtoiAppendDoc, kAtoiStoreDoc, kAtoiOutletDoc);
}

gItoa::gItoa() : gCharCodeBase(charCodeDirection::ToText) {
  Document("Converts character codes into the text they spell and holds it: 104 105 gives 'hi'. "
           "A bang sends the held text again, and the other two inlets add to it or replace it "
           "without sending. The inverse of .atoi.",
           kItoaInletDoc, kItoaAppendDoc, kItoaStoreDoc, kItoaOutletDoc);
}
