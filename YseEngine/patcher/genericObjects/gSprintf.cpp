#include "gSprintf.h"
#include "../../implementations/logImplementation.h"
#include "../math/gExprEval.h"
#include "../pListArgs.h"
#include "../pObjectList.hpp"
#include <cmath>
#include <utility>

using namespace YSE::PATCHER;

#define className gSprintf

namespace {

  // The separators Parameters::Set and the list outlets use. Hand-rolled rather
  // than std::isspace, which reads locale state another thread may be mutating
  // and is undefined for a negative char.
  inline bool IsSeparator(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
  }

  // C's default for %f without a precision.
  constexpr int kDefaultPrecision = 6;

  // Longest single rendered value, before padding: a sign, up to nineteen whole
  // digits, a decimal point and PRECISION_MAX decimals is thirty characters, and
  // the exponent-form fallback below needs kExprValueTextMax.
  constexpr std::size_t kRawMax = 48;

  // Room reserved in the output buffer per slot: the widest field a specifier
  // may ask for (WIDTH_MAX), the longest symbol a %s may hold (TOKEN_CAPACITY)
  // and the longest number the writers below can produce all fit inside this,
  // with slack.
  constexpr std::size_t kSlotOutputMax = 96;

  // Powers of ten for the fixed-point writer, indexed by precision. Two tables
  // rather than a pow() call: the double scales the value, the integer splits
  // the rounded result into its whole and fractional halves, and neither costs
  // a library call on the message path.
  const double kScaleFloat[] = {1e0, 1e1, 1e2, 1e3, 1e4, 1e5, 1e6, 1e7, 1e8, 1e9};
  const U64 kScaleInt[] = {1ULL,      10ULL,      100ULL,      1000ULL,      10000ULL,
                           100000ULL, 1000000ULL, 10000000ULL, 100000000ULL, 1000000000ULL};

  /**
   *  Writes @p value into @p out the way C's ``%f`` would, with @p precision
   *  decimals, and returns how many characters that took. At most ``kRawMax``.
   *
   *  Hand-rolled for the same reason ``WriteInt`` and ``ExprFormatValue`` are:
   *  this runs on whichever thread sent the message, and ``snprintf`` reads
   *  locale state (so the decimal separator would depend on what another thread
   *  has done to it) and is permitted to allocate for a wide field. It also
   *  keeps the promise the class documentation makes — that no user-supplied
   *  text ever reaches a printf-family function.
   *
   *  A magnitude too large for fixed notation to spell in 64 bits falls back to
   *  ``ExprFormatValue``, which switches to an exponent form; a non-finite value
   *  prints as 0, the answer ``ExprProgram::Evaluate`` already gives.
   */
  std::size_t WriteFixed(float value, int precision, char* out) {
    if (precision < 0) precision = kDefaultPrecision;
    if (precision > gSprintf::PRECISION_MAX) precision = gSprintf::PRECISION_MAX;

    double magnitude = value;
    if (!std::isfinite(magnitude)) magnitude = 0.0;
    const bool negative = magnitude < 0.0;
    if (negative) magnitude = -magnitude;

    const double scaled = magnitude * kScaleFloat[precision];
    // Written as a negated comparison so a NaN that slipped through would take
    // the fallback rather than overflow the conversion below.
    if (!(scaled < 9.0e18)) {
      const int written = ExprFormatValue(ExprValue::Float(value), out, kExprValueTextMax);
      return written > 0 ? (std::size_t)written : 0;
    }

    // Round half away from zero, which is what std::llround is defined to do.
    // C's printf rounds a tie to even instead, so the two differ on an exact
    // half — an input a float that came from a patcher computation practically
    // never is. `scaled` is a magnitude and was just bounded below 9e18, so the
    // conversion cannot overflow the signed result. llround rather than
    // (U64)(scaled + 0.5), which double-rounds for values whose sum is not
    // exactly representable; it allocates nothing and reads no locale.
    const U64 rounded = (U64)std::llround(scaled);
    const U64 whole = rounded / kScaleInt[precision];
    const U64 fraction = rounded % kScaleInt[precision];

    char digits[20];
    int n = 0;
    U64 rest = whole;
    do {
      digits[n++] = (char)('0' + (rest % 10));
      rest /= 10;
    } while (rest != 0);

    std::size_t written = 0;
    if (negative) out[written++] = '-';
    while (n > 0)
      out[written++] = digits[--n];

    if (precision > 0) {
      out[written++] = '.';
      U64 divisor = kScaleInt[precision] / 10;
      while (divisor > 0) {
        out[written++] = (char)('0' + ((fraction / divisor) % 10));
        divisor /= 10;
      }
    }
    return written;
  }

  constexpr char kInletDocHot[] =
      "Hot. A bang, an int, a float or a list formats the message and sends it — Max's 'any of the "
      "above messages in the left inlet will format the message and send it out'. When the format "
      "has changeable arguments this is also the first one's inlet, so an int here both stores and "
      "fires. A list is spread one token per changeable argument from here rightwards, and tokens "
      "past the last one are dropped.";

  constexpr char kInletDocCold[] =
      "Cold. Stores the value for this changeable argument and sends nothing; the message is "
      "formatted when inlet 0 next receives. An int, a float or a list is accepted whatever the "
      "conversion is and converted to what it needs — a number reaching a %s is stored as the text "
      "that spells it, and a token that does not read as a number is refused by a %ld or a %f, "
      "leaving the previous value rather than a zero the patch never sent. A list is spread one "
      "token per changeable argument from here rightwards.";

  constexpr char kOutletDoc[] =
      "The formatted message, as text — Max's 'the message specified by the typed-in argument(s) "
      "is "
      "formatted and sent out with substitutions made for the changeable arguments'. Fires once "
      "per "
      "message into inlet 0, and never at all while no format has been given.";

} // namespace

CONSTRUCT() {
  // Every port is built by ShapePorts(), because how many inlets there are is a
  // property of the format. The clear callback is what makes SetParams("")
  // return the object to its no-argument shape rather than leaving the previous
  // format's inlets standing.
  REG_PARM_CLEAR;
  REG_PARM_PARSE;

  ADD_PARAM(creationArgs);

  // The unconfigured object: one inlet, one outlet, no format — and so nothing
  // sent. Also the shape ClearParams() restores.
  ShapePorts();

  ADD_DESCRIPTION(
      "Formats a message out of words and numbers — Max's sprintf, which 'uses the common "
      "C-language printf function inside Max' to 'combine symbols, organize lists of numbers, or "
      "format messages or menu items'. The creation arguments are a format: literal text with "
      "changeable arguments in it, %s for a symbol, %ld for an int, %f for a float and %c for an "
      "int rendered as a character, and 'the number of inlets is determined by the number of "
      "changeable arguments, with each inlet corresponding to a changeable argument, in order'. "
      "This is the message-construction object the others could not stand in for: .prepend and "
      ".append reach the ends of a message and .substitute swaps one word for another, but all "
      "three work in whole tokens, so building /voice/3/freq out of the number 3 meant a round "
      "trip out to the host application. The format is parsed once, on the control thread, into "
      "literal runs and typed slots, and every argument is rendered by the patcher's own writers: "
      "no user-supplied text ever reaches a printf-family function, which would otherwise let a "
      "patch file decide how many arguments to read off the stack. The accepted grammar is %, "
      "optional - and 0 flags, an optional field width, an optional .precision, optional l/h "
      "length modifiers (read and ignored, since this patcher has one integer type and one "
      "floating-point one) and one of d i f F s c; %% is a literal per cent sign. Anything else is "
      "not a specifier: it is reported at error level and copied into the output as the text it "
      "was typed as, so a typo shows up in the log and in the message rather than silently eating "
      "an inlet. Inlet 0 is hot — a bang, an int, a float or a list formats and sends, which is "
      "Max's 'any of the above messages in the left inlet will format the message and send it "
      "out' — and every other inlet is cold and only stores. Inlet 0 keeps no reserved words in "
      "it, the .prepend and .substitute discipline, which matters here because the values this "
      "object formats are often symbols. A list is spread one token per slot from the receiving "
      "inlet rightwards, Max's 'each item in the list is treated as if it had been received in a "
      "separate inlet, up to the number of inlets'. Every slot inlet accepts all three types "
      "rather than only the one its conversion names, since a patch feeding a counter into a %s "
      "obviously means the digits: a number reaching a %s is stored as the text that spells it, "
      "and a token that does not read as a number is refused by a %ld or a %f so the previous "
      "value stands rather than a zero nobody sent. An unset slot follows Max exactly, including "
      "its asymmetry: a %ld or a %f renders 0, a %s or a %c renders nothing. A .sprintf with no "
      "format at all sends nothing, the .prepend rule that makes an unconfigured object safe to "
      "drop into a working patch, and a format with no changeable arguments is a constant message "
      "generator. Max's symout flag is recognised and consumed rather than read as part of the "
      "format — 'the word symout itself is not included in the output' — but it switches nothing, "
      "because this patcher carries every message as one piece of text and so has no symbol/list "
      "distinction to make. A symbol slot holds at most 64 characters and refuses a longer token "
      "rather than truncating it, field width is capped at 64, precision at 9 and the format at 32 "
      "changeable arguments. Calculate() does nothing, and no message path allocates, locks or "
      "blocks: formatting is a bounded walk of the compiled pieces into a buffer reserved before "
      "the object was published for the literal text plus the widest thing every slot can render.");
  ADD_CATEGORY(pCategory::GENERIC);

  PARAM_DOC("format", "",
            "Max's argument list: an optional leading 'symout' followed by the format itself, "
            "which is literal text with changeable arguments in it. A changeable argument is %, "
            "optional - and 0 flags, an optional field width (at most 64), an optional .precision "
            "(at most 9 — decimals for %f, characters for %s), optional l/h length modifiers, and "
            "one of d i f F s c; %% is a literal per cent sign. One inlet is built per changeable "
            "argument, up to 32 of them, and anything else after a % is reported and copied "
            "through as text. With no argument at all the object sends nothing.",
            "printf-style format, at most 32 changeable arguments");
}

void gSprintf::ShapePorts() {
  // Rebuilt rather than patched: the inlet *count* comes from the format, so the
  // compiled pieces, the slots and the ports all have to agree. Safe because
  // every caller runs before the object is wired or published — the constructor,
  // and the two parameter callbacks, which
  // patcherImplementation::CreateObjectUnlocked runs before AssignGraphIds. A
  // *live* SetParams never reaches here on a published object: registering the
  // callbacks makes ParamsNeedRebuild() true, so #234 replaces the object.

  // Max: "If the first argument is the word symout ... the word symout itself is
  // not included in the output of sprintf." Consumed rather than obeyed — see
  // the class documentation on why there is nothing here for it to switch.
  symOut = false;
  std::size_t first = 0;
  if (!creationArgs.empty() && creationArgs[0] == "symout") {
    symOut = true;
    first = 1;
  }

  // Parameters::Set splits on every single space, so joining the tokens back
  // with one space each reproduces the argument string exactly, runs of spaces
  // included.
  format.clear();
  for (std::size_t i = first; i < creationArgs.size(); i++) {
    if (i > first) format.push_back(' ');
    format.append(creationArgs[i]);
  }

  pieces.clear();
  slots.clear();

  bool reportedOverflow = false;
  std::size_t runBegin = 0;
  std::size_t i = 0;
  const std::size_t length = format.size();

  while (i < length) {
    if (format[i] != '%') {
      i++;
      continue;
    }

    Slot slot;
    std::size_t next = i;
    const SpecResult result = ReadSpec(i, slot, next);

    if (result == SpecResult::ESCAPE) {
      // "%%" is one literal per cent sign: end the run *on* the first '%' and
      // resume after the second, so the output carries one of them.
      pieces.push_back({runBegin, (i + 1) - runBegin, -1});
      i += 2;
      runBegin = i;
      continue;
    }

    if (result == SpecResult::INVALID) {
      // The control thread, before the object is published, so this can be said
      // rather than merely made observable. Left as literal text on purpose: a
      // typo that silently ate an inlet would move every argument after it.
      INTERNAL::LogImpl().emit(E_ERROR, std::string("patcher: ") + Type() +
                                            " does not understand the format specifier in \"" +
                                            format.substr(i) + "\"; copied through as text");
      i++;
      continue;
    }

    if (slots.size() >= MAX_SLOTS) {
      if (!reportedOverflow) {
        INTERNAL::LogImpl().emit(E_ERROR, std::string("patcher: ") + Type() +
                                              " takes at most 32 changeable arguments; the rest "
                                              "are copied through as text");
        reportedOverflow = true;
      }
      i++;
      continue;
    }

    slots.push_back(std::move(slot));
    pieces.push_back({runBegin, i - runBegin, (int)slots.size() - 1});
    i = next;
    runBegin = next;
  }

  if (runBegin < length) {
    pieces.push_back({runBegin, length - runBegin, -1});
  }

  // Reserved after the vector has stopped growing, so no reallocation can move
  // these strings and lose it. This is the one allocation a %s store would
  // otherwise need.
  for (Slot& slot : slots)
    slot.text.reserve(TOKEN_CAPACITY);

  inputs.clear();
  outputs.clear();

  // One inlet per changeable argument, and always at least one: a format with no
  // changeable arguments in it is still a message a bang can send.
  const std::size_t inletCount = slots.empty() ? 1 : slots.size();
  for (std::size_t pin = 0; pin < inletCount; pin++) {
    if (pin == 0) {
      // Inlet 0 is the object's active one, as it is everywhere else in the
      // patcher, and the only one a bang means anything on.
      ADD_IN_0;
      REG_BANG_IN(SetBang);
    } else {
      inputs.emplace_back(this, false, (int)pin);
    }
    REG_INT_IN(SetInt);
    REG_FLOAT_IN(SetFloat);
    REG_LIST_IN(SetList);
    inputs.back().SetDoc(InletLabel((int)pin), pin == 0 ? kInletDocHot : kInletDocCold,
                         pin < slots.size() ? KindSpelling(slots[pin].kind) : "any");
  }

  // ANY rather than LIST: the object always sends text, but a reader should not
  // have to know that to wire to it.
  ADD_OUT_ANY;
  outputs.back().SetDoc("out", kOutletDoc, "any");

  // Every allocation the message path would otherwise need, taken here for the
  // longest message this format can produce.
  outText.clear();
  outText.reserve(format.size() + (slots.size() * kSlotOutputMax) + 1);
}

const char* gSprintf::KindSpelling(Kind kind) {
  switch (kind) {
  case Kind::INT:
    return "%ld";
  case Kind::FLOAT:
    return "%f";
  case Kind::STRING:
    return "%s";
  case Kind::CHAR:
    return "%c";
  }
  return "any";
}

gSprintf::SpecResult gSprintf::ReadSpec(std::size_t pos, Slot& slot, std::size_t& next) const {
  const std::size_t length = format.size();
  std::size_t p = pos + 1;
  if (p >= length) return SpecResult::INVALID;
  if (format[p] == '%') return SpecResult::ESCAPE;

  // Flags. Only the two that mean something for a message: '-' left-aligns in
  // the field and '0' pads a number with zeros. C's '+', ' ' and '#' are about
  // C's own spelling of numbers rather than about the message.
  bool leftAlign = false;
  bool zeroPad = false;
  while (p < length && (format[p] == '-' || format[p] == '0')) {
    if (format[p] == '-') {
      leftAlign = true;
    } else {
      zeroPad = true;
    }
    p++;
  }

  int width = 0;
  int widthDigits = 0;
  while (p < length && format[p] >= '0' && format[p] <= '9') {
    if (widthDigits < 3) width = (width * 10) + (format[p] - '0');
    widthDigits++;
    p++;
  }
  // Refused rather than clamped: a width the object silently shrank would spell
  // the message differently from the way the patch asked for it, and the buffer
  // this bounds is reserved before the object is published.
  if (widthDigits > 3 || width > WIDTH_MAX) return SpecResult::INVALID;

  int precision = -1;
  if (p < length && format[p] == '.') {
    p++;
    precision = 0;
    int digits = 0;
    while (p < length && format[p] >= '0' && format[p] <= '9') {
      if (digits < 2) precision = (precision * 10) + (format[p] - '0');
      digits++;
      p++;
    }
    if (digits > 2 || precision > PRECISION_MAX) return SpecResult::INVALID;
  }

  // Length modifiers: read and thrown away. This patcher has one integer type
  // and one floating-point one, so %ld, %d and %lld all mean the same thing —
  // but Max's own documentation spells an int argument %ld, so refusing them
  // would refuse every format copied over from a Max patch.
  while (p < length && (format[p] == 'l' || format[p] == 'h'))
    p++;
  if (p >= length) return SpecResult::INVALID;

  Kind kind = Kind::INT;
  switch (format[p]) {
  case 'd':
  case 'i':
    kind = Kind::INT;
    break;
  case 'f':
  case 'F':
    kind = Kind::FLOAT;
    break;
  case 's':
    kind = Kind::STRING;
    break;
  case 'c':
    kind = Kind::CHAR;
    break;
  default:
    return SpecResult::INVALID;
  }

  slot.kind = kind;
  slot.leftAlign = leftAlign;
  slot.zeroPad = zeroPad;
  slot.width = width;
  slot.precision = precision;
  next = p + 1;
  return SpecResult::OK;
}

PARM_CLEAR() {
  // Runs on the control thread before the parameter string is re-read, and is
  // the whole of `SetParams("")`: Parameters::Set returns without calling the
  // parse callback for an empty argument, so this has to leave the no-argument
  // object behind rather than one still wearing the previous format's inlets.
  creationArgs.clear();
  ShapePorts();
}

PARM_PARSE() {
  ShapePorts();
}

void gSprintf::StoreInt(std::size_t index, int value) {
  if (index >= slots.size()) return;
  Slot& slot = slots[index];

  switch (slot.kind) {
  case Kind::INT:
  case Kind::CHAR:
    // Max: "A %c argument will convert the int to its ASCII character
    // equivalent" — the same store, read differently when it is rendered.
    slot.intValue = value;
    break;
  case Kind::FLOAT:
    slot.floatValue = (float)value;
    break;
  case Kind::STRING: {
    // WriteInt rather than std::to_string: no allocation into a string the slot
    // already has room for, and no locale.
    char text[FORMAT_INT_WIDTH];
    const std::size_t written = WriteInt(value, text);
    slot.text.assign(text, written);
    break;
  }
  }
  slot.hasValue = true;
}

void gSprintf::StoreFloat(std::size_t index, float value) {
  if (index >= slots.size()) return;
  Slot& slot = slots[index];

  switch (slot.kind) {
  case Kind::INT:
  case Kind::CHAR:
    // Truncated through the range-checked conversion rather than cast: a cast of
    // a NaN or of something outside the int range is undefined.
    slot.intValue = ExprToInt(value);
    break;
  case Kind::FLOAT:
    slot.floatValue = value;
    break;
  case Kind::STRING: {
    char text[kExprValueTextMax];
    const int written = ExprFormatValue(ExprValue::Float(value), text, kExprValueTextMax);
    slot.text.assign(text, written > 0 ? (std::size_t)written : 0);
    break;
  }
  }
  slot.hasValue = true;
}

void gSprintf::StoreToken(std::size_t index, const char* text, std::size_t length) {
  if (index >= slots.size() || length == 0) return;
  Slot& slot = slots[index];

  float number = 0.f;
  // Strict on purpose, as the rest of the family is: ExprParseFloatList would
  // read `5abc` as 5 and fold `1e999` to 0, and neither answers "is this token a
  // number at all".
  const bool numeric = ReadNumericToken(text, length, number);

  switch (slot.kind) {
  case Kind::INT:
    // Refused rather than zeroed: a symbol arriving at a number's inlet is a
    // patching mistake, and leaving the previous value standing keeps the rest
    // of the message readable while it is found.
    if (!numeric) return;
    slot.intValue = ExprToInt(number);
    break;
  case Kind::FLOAT:
    if (!numeric) return;
    slot.floatValue = number;
    break;
  case Kind::CHAR:
    // A number is Max's own reading — the int as its character code. A symbol
    // has no code to convert, so its first character is the obvious one.
    slot.intValue = numeric ? ExprToInt(number) : (int)(unsigned char)text[0];
    break;
  case Kind::STRING:
    // Refused rather than truncated, and silently, since this may be the audio
    // thread. Half a symbol is a different symbol.
    if (length > TOKEN_CAPACITY) return;
    slot.text.assign(text, length);
    break;
  }
  slot.hasValue = true;
}

void gSprintf::Distribute(const std::string& text, std::size_t first) {
  // Max: "Each item in the list is treated as if it had been received in a
  // separate inlet, up to the number of inlets." Tokens past the last slot have
  // nowhere to go and are dropped. Walked in place — a substr per token would
  // allocate on whichever thread the message arrived on.
  const std::size_t size = text.size();
  std::size_t i = 0;
  std::size_t index = first;

  while (i < size && index < slots.size()) {
    while (i < size && IsSeparator(text[i]))
      i++;
    if (i >= size) break;

    const std::size_t begin = i;
    while (i < size && !IsSeparator(text[i]))
      i++;

    StoreToken(index, text.c_str() + begin, i - begin);
    index++;
  }
}

void gSprintf::RenderSlot(const Slot& slot) {
  char raw[kRawMax];
  const char* body = raw;
  std::size_t length = 0;
  bool numeric = false;

  switch (slot.kind) {
  case Kind::INT:
    // Max: "If no value has been received for a changeable number argument (%ld
    // or %f), 0 will be substituted for that argument."
    length = WriteInt(slot.hasValue ? slot.intValue : 0, raw);
    numeric = true;
    break;
  case Kind::FLOAT:
    length = WriteFixed(slot.hasValue ? slot.floatValue : 0.f, slot.precision, raw);
    numeric = true;
    break;
  case Kind::CHAR:
    // Max: "If no value has been received for a %s or %c argument, that argument
    // will be left blank." A code outside the single-byte range has no character
    // to render, and 0 would end the text for anything reading it as C string.
    if (slot.hasValue && slot.intValue > 0 && slot.intValue < 256) {
      raw[0] = (char)slot.intValue;
      length = 1;
    }
    break;
  case Kind::STRING:
    if (slot.hasValue) {
      body = slot.text.c_str();
      length = slot.text.size();
      // C's reading of a precision on %s: at most that many characters.
      if (slot.precision >= 0 && length > (std::size_t)slot.precision) {
        length = (std::size_t)slot.precision;
      }
    }
    break;
  }

  const std::size_t width = slot.width > 0 ? (std::size_t)slot.width : 0;
  if (length >= width) {
    outText.append(body, length);
    return;
  }

  const std::size_t pad = width - length;
  if (slot.leftAlign) {
    outText.append(body, length);
    outText.append(pad, ' ');
    return;
  }
  if (slot.zeroPad && numeric) {
    // The sign stays in front of the zeros — "-005", not "00-5" — which is C's
    // rule and the only spelling that reads back as the number it is.
    std::size_t at = 0;
    if (length > 0 && (body[0] == '-' || body[0] == '+')) {
      outText.push_back(body[0]);
      at = 1;
    }
    outText.append(pad, '0');
    outText.append(body + at, length - at);
    return;
  }
  outText.append(pad, ' ');
  outText.append(body, length);
}

void gSprintf::Emit(YSE::THREAD thread) {
  // A .sprintf nobody has configured is inert rather than a source of empty
  // messages — the .prepend rule that makes it safe to drop an unconfigured
  // object into a working patch.
  if (format.empty()) return;

  // Filled here rather than kept between sends: the send path is synchronous, so
  // a patch looping the outlet back into an inlet re-enters this function inside
  // the SendList below. Appends only, into capacity reserved by ShapePorts().
  outText.clear();
  for (const Piece& piece : pieces) {
    if (piece.length > 0) outText.append(format.c_str() + piece.begin, piece.length);
    if (piece.slot >= 0) RenderSlot(slots[(std::size_t)piece.slot]);
  }

  outputs[0].SendList(outText, thread);
}

BANG_IN(SetBang) {
  // Max: "In left inlet: Formats the message using the values currently stored."
  // Registered on inlet 0 alone, so there is nothing to guard against here.
  (void)inlet;
  Emit(thread);
}

INT_IN(SetInt) {
  // The store settles before the send, so a patch that loops the outlet back
  // into an inlet finds the message it is being handed.
  StoreInt((std::size_t)inlet, value);
  // Max: "Any of the above messages in the left inlet will format the message
  // and send it out." Every other inlet is cold.
  if (inlet == 0) Emit(thread);
}

FLOAT_IN(SetFloat) {
  StoreFloat((std::size_t)inlet, value);
  if (inlet == 0) Emit(thread);
}

LIST_IN(SetList) {
  Distribute(value, (std::size_t)inlet);
  if (inlet == 0) Emit(thread);
}

#undef className
