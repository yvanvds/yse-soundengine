#include "dScale.h"
#include "../pListArgs.h"
#include "../pSelector.h"
#include <string>

using namespace YSE::PATCHER;

#define className dScale

namespace {
  using MODE = YSE::DSP::scale::MODE;

  // Walks a message's space-separated tokens in place, so message handling
  // allocates nothing on whichever thread delivers it.
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
  };

  bool IsWord(const std::string& text, std::size_t begin, std::size_t end, const char* word) {
    const std::size_t length = std::char_traits<char>::length(word);
    return end - begin == length && text.compare(begin, length, word) == 0;
  }

  // MSP's classic values; anything else is the modern default.
  MODE ModeFromInt(int value) {
    switch (value) {
    case static_cast<int>(MODE::CLASSIC):
      return MODE::CLASSIC;
    case static_cast<int>(MODE::MODERN_INVERSE):
      return MODE::MODERN_INVERSE;
    default:
      return MODE::MODERN;
    }
  }
} // namespace

CONSTRUCT_DSP() {
  input = 0.f;
  inLow = DSP::scale::DEFAULT_IN_LOW;
  inHigh = DSP::scale::DEFAULT_IN_HIGH;
  outLow = DSP::scale::DEFAULT_OUT_LOW;
  outHigh = DSP::scale::DEFAULT_OUT_HIGH;
  exponent = DSP::scale::DEFAULT_EXPONENT;
  classic = static_cast<int>(DSP::scale::DEFAULT_MODE);

  // in 0: the signal (or a number) and `classic ...`
  ADD_IN_0;
  REG_SIGNAL_IN;
  REG_FLOAT_IN(SetFloat);
  REG_INT_IN(SetInt);

  // in 1-5: inLow, inHigh, outLow, outHigh, exponent; numbers or signals
  ADD_IN_1;
  REG_SIGNAL_IN;
  REG_FLOAT_IN(SetFloat);
  REG_INT_IN(SetInt);

  ADD_IN_2;
  REG_SIGNAL_IN;
  REG_FLOAT_IN(SetFloat);
  REG_INT_IN(SetInt);

  ADD_IN_3;
  REG_SIGNAL_IN;
  REG_FLOAT_IN(SetFloat);
  REG_INT_IN(SetInt);

  ADD_IN_4;
  REG_SIGNAL_IN;
  REG_FLOAT_IN(SetFloat);
  REG_INT_IN(SetInt);

  ADD_IN_5;
  REG_SIGNAL_IN;
  REG_FLOAT_IN(SetFloat);
  REG_INT_IN(SetInt);

  // out 0: the mapped signal
  ADD_OUT_BUFFER;

  ADD_PARAM(inLow);
  ADD_PARAM(inHigh);
  ADD_PARAM(outLow);
  ADD_PARAM(outHigh);
  ADD_PARAM(exponent);
  ADD_PARAM(classic);

  ADD_DESCRIPTION("Maps a signal from the input range onto the output range, as a straight line "
                  "or bent into a curve by an exponent: control-voltage style range mapping at "
                  "audio rate. Samples outside the input range map past the output range.");
  ADD_CATEGORY(pCategory::MATH);
  INLET_DOC(0, "in",
            "The signal to map, or a number used while no signal is connected (unconnected "
            "reads 0). 'classic 0', 'classic 1' or 'classic 2' sets the formula.",
            "any float");
  INLET_DOC(1, "inLow",
            "The input value that maps to outLow. A number or a signal read per sample "
            "(unconnected, the last number sent here or the creation argument).",
            "any float");
  INLET_DOC(2, "inHigh",
            "The input value that maps to outHigh. A number or a signal read per sample "
            "(unconnected, the last number sent here or the creation argument).",
            "any float");
  INLET_DOC(3, "outLow",
            "The output for inLow. A number or a signal read per sample (unconnected, the last "
            "number sent here or the creation argument).",
            "any float");
  INLET_DOC(4, "outHigh",
            "The output for inHigh. A number or a signal read per sample (unconnected, the "
            "last number sent here or the creation argument).",
            "any float");
  INLET_DOC(5, "exponent",
            "The curve: 1 is a straight line, above 1 starts slowly and ends fast, below 1 the "
            "opposite. A number or a signal read per sample (unconnected, the last number sent "
            "here or the creation argument).",
            "above 0");
  OUTLET_DOC(0, "out",
             "The mapped signal. A result that is not a finite number gives 0; a collapsed "
             "input range gives outLow.",
             "any float");
  PARAM_DOC("inLow", "0",
            "The input value that maps to outLow ('~scale -1 1 200 800'). A number in its inlet "
            "changes it, but what is saved stays the argument.",
            "any float");
  PARAM_DOC("inHigh", "1",
            "The input value that maps to outHigh. A number in its inlet changes it, but what "
            "is saved stays the argument.",
            "any float");
  PARAM_DOC("outLow", "0",
            "The output for inLow. A number in its inlet changes it, but what is saved stays "
            "the argument.",
            "any float");
  PARAM_DOC("outHigh", "1",
            "The output for inHigh. A number in its inlet changes it, but what is saved stays "
            "the argument.",
            "any float");
  PARAM_DOC("exponent", "1",
            "The curve ('~scale 0 1 0 1 2'): a power of the normalized input, or a base in "
            "classic 1. A number in its inlet changes it, but what is saved stays the argument.",
            "above 0");
  PARAM_DOC("classic", "0",
            "The formula: 0 the power curve; 1 the legacy linedrive formula; 2 the power curve "
            "with the exponent inverted when outLow is above outHigh. A 'classic' message "
            "changes it, but what is saved stays the argument.",
            "0 to 2");
}

// The DSP class ignores a setting that is not finite.
void dScale::Store(float value, int inlet) {
  switch (inlet) {
  case 0:
    input = value;
    break;
  case 1:
    inLow = value;
    break;
  case 2:
    inHigh = value;
    break;
  case 3:
    outLow = value;
    break;
  case 4:
    outHigh = value;
    break;
  case 5:
    exponent = value;
    break;
  default:
    break;
  }
}

FLOAT_IN(SetFloat) {
  (void)thread;
  Store(value, inlet);
}

INT_IN(SetInt) {
  (void)thread;
  Store(static_cast<float>(value), inlet);
}

MESSAGES() {
  (void)value;
  TokenReader reader{message};
  std::size_t begin = 0;
  std::size_t end = 0;
  if (!reader.Next(begin, end) || !IsWord(message, begin, end, "classic")) return;
  if (!reader.Next(begin, end)) return;
  float number = 0.f;
  if (ReadNumericToken(message.c_str() + begin, end - begin, number) && number >= 0.f &&
      number <= 2.f && number == static_cast<float>(static_cast<int>(number)))
    classic = static_cast<int>(number);
}

CALC() {
  fn.mode(ModeFromInt(classic.load()));
  // Unconnected inlets (#1036): each reads its number.
  DSP::buffer& out = fn(inputs[0].Signal(input.load()), inputs[1].Signal(inLow.load()),
                        inputs[2].Signal(inHigh.load()), inputs[3].Signal(outLow.load()),
                        inputs[4].Signal(outHigh.load()), inputs[5].Signal(exponent.load()));
  outputs[0].SendBuffer(&out, thread);
}

#undef className
