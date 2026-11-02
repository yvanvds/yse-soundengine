#include "dPong.h"
#include "../pListArgs.h"
#include "../pSelector.h"
#include <string>

using namespace YSE::PATCHER;

#define className dPong

namespace {
  using MODE = YSE::DSP::fold::MODE;

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

    bool NextNumber(float& number) {
      std::size_t begin = 0;
      std::size_t end = 0;
      return Next(begin, end) && ReadNumericToken(text.c_str() + begin, end - begin, number);
    }
  };

  bool IsWord(const std::string& text, std::size_t begin, std::size_t end, const char* word) {
    const std::size_t length = std::char_traits<char>::length(word);
    return end - begin == length && text.compare(begin, length, word) == 0;
  }

  // MSP's folding-mode numbering; anything else folds, the default.
  MODE ModeFromInt(int value) {
    switch (value) {
    case static_cast<int>(MODE::WRAP):
      return MODE::WRAP;
    case static_cast<int>(MODE::CLIP):
      return MODE::CLIP;
    default:
      return MODE::FOLD;
    }
  }
} // namespace

CONSTRUCT_DSP() {
  input = 0.f;
  mode = static_cast<int>(DSP::fold::DEFAULT_MODE);
  low = DSP::fold::DEFAULT_LOW;
  high = DSP::fold::DEFAULT_HIGH;

  // in 0: the signal (or a number), `mode ...` and `range ...`
  ADD_IN_0;
  REG_SIGNAL_IN;
  REG_FLOAT_IN(SetInput);
  REG_INT_IN(SetInputInt);

  // in 1: the lower limit, a number or a signal
  ADD_IN_1;
  REG_SIGNAL_IN;
  REG_FLOAT_IN(SetLow);
  REG_INT_IN(SetLowInt);

  // in 2: the upper limit, a number or a signal
  ADD_IN_2;
  REG_SIGNAL_IN;
  REG_FLOAT_IN(SetHigh);
  REG_INT_IN(SetHighInt);

  // out 0: the signal kept in range
  ADD_OUT_BUFFER;

  ADD_PARAM(mode);
  ADD_PARAM(low);
  ADD_PARAM(high);

  ADD_DESCRIPTION("Keeps a signal inside the range low to high by folding it back from the "
                  "limits, the default, by wrapping it around to the other side, or by clipping "
                  "it: wavefolding, phase wrapping and limiting.");
  ADD_CATEGORY(pCategory::MATH);
  INLET_DOC(0, "in",
            "The signal to keep in range, or a number used while no signal is connected "
            "(unconnected reads 0). 'mode fold', 'mode wrap' or 'mode clip' (or 'mode 0' to "
            "'mode 2') sets the mode; 'range' with two numbers sets both limits.",
            "any float");
  INLET_DOC(1, "low",
            "The lower limit. A number or a signal read per sample (unconnected, the last "
            "number sent here or the creation argument).",
            "any float");
  INLET_DOC(2, "high",
            "The upper limit. A number or a signal read per sample (unconnected, the last "
            "number sent here or the creation argument). Limits given high to low are swapped.",
            "any float");
  OUTLET_DOC(0, "out",
             "The signal: unchanged inside the range; outside it, reflected back from the limits "
             "(fold), carried around to the other side with the upper limit itself giving the "
             "lower one (wrap), or pinned to the nearest limit (clip).",
             "low to high");
  PARAM_DOC("mode", "0",
            "0 folds, 1 wraps, 2 clips ('~pong 1 -1 1'). A 'mode' message changes it, but what "
            "is saved stays the argument.",
            "0 to 2");
  PARAM_DOC("low", "0",
            "The lower limit ('~pong 0 -0.5 0.5'). A number in the middle inlet or a 'range' "
            "message changes it, but what is saved stays the argument.",
            "any float");
  PARAM_DOC("high", "1",
            "The upper limit. A number in the right inlet or a 'range' message changes it, but "
            "what is saved stays the argument.",
            "any float");
}

FLOAT_IN(SetInput) {
  (void)inlet;
  (void)thread;
  input = value;
}

INT_IN(SetInputInt) {
  (void)inlet;
  (void)thread;
  input = static_cast<float>(value);
}

// The DSP class ignores a limit that is not finite.
FLOAT_IN(SetLow) {
  (void)inlet;
  (void)thread;
  low = value;
}

INT_IN(SetLowInt) {
  (void)inlet;
  (void)thread;
  low = static_cast<float>(value);
}

FLOAT_IN(SetHigh) {
  (void)inlet;
  (void)thread;
  high = value;
}

INT_IN(SetHighInt) {
  (void)inlet;
  (void)thread;
  high = static_cast<float>(value);
}

MESSAGES() {
  (void)value;
  TokenReader reader{message};
  std::size_t begin = 0;
  std::size_t end = 0;
  if (!reader.Next(begin, end)) return;

  if (IsWord(message, begin, end, "mode")) {
    if (!reader.Next(begin, end)) return;
    float number = 0.f;
    if (IsWord(message, begin, end, "fold"))
      mode = static_cast<int>(MODE::FOLD);
    else if (IsWord(message, begin, end, "wrap"))
      mode = static_cast<int>(MODE::WRAP);
    else if (IsWord(message, begin, end, "clip"))
      mode = static_cast<int>(MODE::CLIP);
    else if (ReadNumericToken(message.c_str() + begin, end - begin, number) && number >= 0.f &&
             number <= 2.f && number == static_cast<float>(static_cast<int>(number)))
      mode = static_cast<int>(number);
    return;
  }

  if (IsWord(message, begin, end, "range")) {
    float lowValue = 0.f;
    float highValue = 0.f;
    if (reader.NextNumber(lowValue) && reader.NextNumber(highValue)) {
      low = lowValue;
      high = highValue;
    }
  }
}

CALC() {
  fn.mode(ModeFromInt(mode.load()));
  // Unconnected inlets (#1036): each reads its number.
  DSP::buffer& out = fn(inputs[0].Signal(input.load()), inputs[1].Signal(low.load()),
                        inputs[2].Signal(high.load()));
  outputs[0].SendBuffer(&out, thread);
}

#undef className
