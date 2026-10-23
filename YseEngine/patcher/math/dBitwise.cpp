#include "dBitwise.h"
#include "../pObjectList.hpp"
#include "../pListArgs.h"
#include "../pSelector.h"

using namespace YSE::PATCHER;

namespace {
  constexpr const char* kSignalIn =
      "The input: a signal, or a number used while no signal is connected.";
  constexpr const char* kModeDoc =
      "0 reads samples as raw 32-bit float patterns, 1 converts them to integers first.";

  // Walks a message's space-separated tokens in place, as dSignalMath's
  // `nearest` parser does, so message handling allocates nothing on
  // whichever thread delivers it.
  struct TokenReader {
    const std::string& text;
    std::size_t pos = 0;

    // The next token as [begin, end); false at the end of the text.
    bool Next(std::size_t& begin, std::size_t& end) {
      while (pos < text.size() && IsSelectorSeparator(text[pos]))
        pos++;
      begin = pos;
      while (pos < text.size() && !IsSelectorSeparator(text[pos]))
        pos++;
      end = pos;
      return end > begin;
    }

    bool NextNumber(float& number) {
      std::size_t begin = 0;
      std::size_t end = 0;
      return Next(begin, end) && ReadNumericToken(text.c_str() + begin, end - begin, number);
    }
  };

  bool IsWord(const std::string& text, std::size_t begin, std::size_t end, const char* word,
              std::size_t length) {
    return end - begin == length && text.compare(begin, length, word) == 0;
  }

  // 0 to `highest`, anything else 0.
  int ModeValue(float number, int highest) {
    const int n = static_cast<int>(number);
    return (n >= 0 && n <= highest && static_cast<float>(n) == number) ? n : 0;
  }

  int ClampToInt(float number) {
    if (number >= 2147483647.f) return 2147483647;
    if (number <= -2147483648.f) return -2147483647 - 1;
    return static_cast<int>(number);
  }
} // namespace

// ─── ~bitand ~bitor ~bitxor ───────────────────────────────────────────────────

#define className dBitwiseBase

dBitwiseBase::dBitwiseBase(DSP::bitwise::OP op) : pObject(true), fn(op) {
  leftFloatIn = 0.f;
  mask = 0;
  mode = 0;

  // in 0: the input (signal, float or int), `bits ...` and `mode n`
  ADD_IN_0;
  REG_SIGNAL_IN;
  REG_FLOAT_IN(SetLeftFloat);
  REG_INT_IN(SetLeftInt);

  // in 1: the mask (int or float) or a second signal
  ADD_IN_1;
  REG_SIGNAL_IN;
  REG_FLOAT_IN(SetMaskFloat);
  REG_INT_IN(SetMaskInt);

  // out 0: the result
  ADD_OUT_BUFFER;

  ADD_PARAM(mask);
  ADD_PARAM(mode);

  ADD_CATEGORY(pCategory::MATH);
}

void dBitwiseBase::Document(const char* summary, const char* outletDoc) {
  ADD_DESCRIPTION(summary);
  INLET_DOC(0, "in",
            "The input: a signal, or a number used while no signal is connected. 'bits' and up "
            "to 32 ones and zeros (most significant first) set the mask; 'mode 0-3' sets the "
            "mode.",
            "any float");
  INLET_DOC(1, "mask",
            "The mask: an int is used as a 32-bit pattern, a float is read as the mode reads "
            "the right operand. A connected signal is used sample by sample instead.",
            "any int or float");
  OUTLET_DOC(0, "out", outletDoc, "any float, never NaN or infinity");
  PARAM_DOC("mask", "0", "32-bit mask used while no signal is connected to the right inlet.",
            "any int");
  PARAM_DOC("mode", "0",
            "0: both operands as raw float bits. 1: both converted to integers. 2: input raw, "
            "mask integer. 3: input integer, mask raw.",
            "0 to 3");
}

FLOAT_IN(SetLeftFloat) {
  leftFloatIn = value;
}

INT_IN(SetLeftInt) {
  leftFloatIn = static_cast<float>(value);
}

FLOAT_IN(SetMaskFloat) {
  mask = DSP::FloatToBits(value, DSP::BitRightIsInteger(DSP::BitModeFromInt(mode)));
}

INT_IN(SetMaskInt) {
  mask = value;
}

MESSAGES() {
  (void)value;
  TokenReader reader{message};
  std::size_t begin = 0;
  std::size_t end = 0;
  if (!reader.Next(begin, end)) return;

  if (IsWord(message, begin, end, "mode", 4)) {
    float number = 0.f;
    if (reader.NextNumber(number)) mode = ModeValue(number, 3);
    return;
  }

  if (IsWord(message, begin, end, "bits", 4)) {
    // Up to 32 bits, most significant first; a token that is not a number
    // drops the whole message.
    U32 bits = 0;
    int count = 0;
    while (count < 32 && reader.Next(begin, end)) {
      float number = 0.f;
      if (!ReadNumericToken(message.c_str() + begin, end - begin, number)) return;
      bits = (bits << 1) | (number != 0.f ? 1u : 0u);
      count++;
    }
    if (count > 0) mask = static_cast<int>(bits);
  }
}

CALC() {
  fn.setMode(DSP::BitModeFromInt(mode)).setMask(mask);
  // Unconnected inlets read as their numbers; a connected signal wins (#1036).
  DSP::buffer& in = inputs[0].Signal(leftFloatIn);
  DSP::buffer& out = inputs[1].IsConstant() ? fn(in) : fn(in, inputs[1].Signal(0.f));
  outputs[0].SendBuffer(&out, thread);
}

#undef className

dBitAnd::dBitAnd() : dBitwiseBase(DSP::bitwise::OP::AND) {
  Document("Bitwise and of the input with a mask or a second signal: a bit stays 1 only where "
           "both have a 1. Clearing low mantissa bits is a coarse bitcrusher. Samples are 32-bit "
           "floats, read as raw bit patterns or converted to integers (see mode).",
           "The bitwise and of the input and the mask.");
}

dBitOr::dBitOr() : dBitwiseBase(DSP::bitwise::OP::OR) {
  Document("Bitwise or of the input with a mask or a second signal: a bit is 1 where either has "
           "a 1. Samples are 32-bit floats, read as raw bit patterns or converted to integers "
           "(see mode). A result that would be NaN or infinity gives 0.",
           "The bitwise or of the input and the mask.");
}

dBitXor::dBitXor() : dBitwiseBase(DSP::bitwise::OP::XOR) {
  Document("Bitwise exclusive-or of the input with a mask or a second signal: a bit is 1 where "
           "exactly one has a 1. Samples are 32-bit floats, read as raw bit patterns or "
           "converted to integers (see mode). A result that would be NaN or infinity gives 0.",
           "The bitwise exclusive-or of the input and the mask.");
}

// ─── ~bitsafe ─────────────────────────────────────────────────────────────────

dBitSafe::dBitSafe() {
  Document("Replaces NaN and infinite samples with 0 and passes every other sample through. Put "
           "it at the end of a feedback chain so one bad sample cannot stay in the loop.",
           kSignalIn, "any float", "The input with NaN and infinity replaced by 0.",
           "any finite float");
}

// ─── ~bitnot ──────────────────────────────────────────────────────────────────

#define className dBitNot

CONSTRUCT_DSP() {
  inputFloat = 0.f;
  mode = 0;

  // in 0: the input (signal, float or int) and `mode <0|1>`
  ADD_IN_0;
  REG_SIGNAL_IN;
  REG_FLOAT_IN(SetInputFloat);
  REG_INT_IN(SetInputInt);

  // out 0: the inverted input
  ADD_OUT_BUFFER;

  ADD_PARAM(mode);

  ADD_DESCRIPTION("Inverts every bit of the input. Samples are 32-bit floats: in mode 0 the raw "
                  "bit pattern is flipped (a result that would be NaN or infinity gives 0), in "
                  "mode 1 the sample is converted to an integer n first and gives -n - 1.");
  ADD_CATEGORY(pCategory::MATH);
  INLET_DOC(0, "in",
            "The input: a signal, or a number used while no signal is connected. 'mode "
            "0' or 'mode 1' sets the mode.",
            "any float");
  OUTLET_DOC(0, "out", "The input with every bit inverted.", "any float, never NaN or infinity");
  PARAM_DOC("mode", "0", kModeDoc, "0 or 1");
}

FLOAT_IN(SetInputFloat) {
  inputFloat = value;
}

INT_IN(SetInputInt) {
  inputFloat = static_cast<float>(value);
}

MESSAGES() {
  (void)value;
  TokenReader reader{message};
  std::size_t begin = 0;
  std::size_t end = 0;
  if (!reader.Next(begin, end) || !IsWord(message, begin, end, "mode", 4)) return;
  float number = 0.f;
  if (reader.NextNumber(number)) mode = ModeValue(number, 1);
}

CALC() {
  fn.setMode(DSP::BitModeFromInt(mode));
  // An unconnected inlet reads as its number; a connected signal wins (#1036).
  outputs[0].SendBuffer(&fn(inputs[0].Signal(inputFloat)), thread);
}

#undef className

// ─── ~bitshift ────────────────────────────────────────────────────────────────

#define className dBitShift

CONSTRUCT_DSP() {
  inputFloat = 0.f;
  shift = 0;
  mode = 0;

  // in 0: the input (signal, float or int), `shift <n>` and `mode <0|1>`
  ADD_IN_0;
  REG_SIGNAL_IN;
  REG_FLOAT_IN(SetInputFloat);
  REG_INT_IN(SetInputInt);

  // out 0: the shifted input
  ADD_OUT_BUFFER;

  ADD_PARAM(shift);
  ADD_PARAM(mode);

  ADD_DESCRIPTION("Shifts the bits of the input: positive shifts left, negative right. Samples are "
                  "32-bit floats: mode 0 shifts the raw bit pattern (a NaN or infinite result "
                  "gives 0), mode 1 converts to an integer first, so each bit left doubles it and "
                  "each bit right halves it.");
  ADD_CATEGORY(pCategory::MATH);
  INLET_DOC(0, "in",
            "The input: a signal, or a number used while no signal is connected. 'shift n' sets "
            "the shift, 'mode 0' or 'mode 1' the mode.",
            "any float");
  OUTLET_DOC(0, "out", "The input with its bits shifted.", "any float, never NaN or infinity");
  PARAM_DOC("shift", "0", "Bits to shift: positive moves left, negative moves right.",
            "-32 to 32 (beyond empties the word)");
  PARAM_DOC("mode", "0", kModeDoc, "0 or 1");
}

FLOAT_IN(SetInputFloat) {
  inputFloat = value;
}

INT_IN(SetInputInt) {
  inputFloat = static_cast<float>(value);
}

MESSAGES() {
  (void)value;
  TokenReader reader{message};
  std::size_t begin = 0;
  std::size_t end = 0;
  if (!reader.Next(begin, end)) return;
  const bool isShift = IsWord(message, begin, end, "shift", 5);
  const bool isMode = IsWord(message, begin, end, "mode", 4);
  float number = 0.f;
  if ((!isShift && !isMode) || !reader.NextNumber(number)) return;
  if (isShift)
    shift = ClampToInt(number);
  else
    mode = ModeValue(number, 1);
}

CALC() {
  fn.setMode(DSP::BitModeFromInt(mode)).setShift(shift);
  // An unconnected inlet reads as its number; a connected signal wins (#1036).
  outputs[0].SendBuffer(&fn(inputs[0].Signal(inputFloat)), thread);
}

#undef className
