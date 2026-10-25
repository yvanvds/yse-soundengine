#include "dFffb.h"
#include "../pListArgs.h"
#include "../pSelector.h"

#include <cctype>
#include <cmath>
#include <string_view>

using namespace YSE::PATCHER;

namespace {
  // The token starting at or after `from` in `text`, walked in place so
  // nothing allocates on whichever thread delivers the message. `next` is
  // set to where the token ends. Empty when there is none.
  std::string_view Token(const std::string& text, std::size_t from, std::size_t& next) {
    const char* chars = text.c_str();
    const std::size_t size = text.size();
    std::size_t begin = from;
    while (begin < size && IsSelectorSeparator(chars[begin]))
      begin++;
    std::size_t end = begin;
    while (end < size && !IsSelectorSeparator(chars[end]))
      end++;
    next = end;
    return std::string_view(chars + begin, end - begin);
  }

  bool ReadNumber(std::string_view token, float& out) {
    return !token.empty() && ReadNumericToken(token.data(), token.size(), out) &&
           std::isfinite(out);
  }

  // A filter index: a whole number from 0.
  bool ReadIndex(std::string_view token, unsigned int& out) {
    float number = 0.f;
    if (!ReadNumber(token, number) || number < 0.f || number != std::floor(number) ||
        number >= static_cast<float>(YSE::DSP::filterBank::MAX_FILTERS))
      return false;
    out = static_cast<unsigned int>(number);
    return true;
  }

  // `word` equals the lower-case `name`, regardless of case.
  bool Is(std::string_view word, std::string_view name) {
    if (word.size() != name.size()) return false;
    for (std::size_t i = 0; i < word.size(); ++i) {
      if (std::tolower(static_cast<unsigned char>(word[i])) != name[i]) return false;
    }
    return true;
  }
} // namespace

#define className dFffb

CONSTRUCT_DSP() {
  REG_PARM_CLEAR;
  REG_PARM_PARSE;
  ADD_PARAM(arguments);

  ShapePorts();

  ADD_DESCRIPTION("Bank of bandpass filters in parallel on one input, each with its own center "
                  "frequency, Q, gain and outlet: vocoders, spectral shaping, resonator banks.");
  ADD_CATEGORY(pCategory::FILTER);
  PARAM_DOC("arguments", "4 100 2 10 1",
            "The number of filters (1-128, also the number of outlets), the first filter's "
            "frequency in Hz, the ratio between successive frequencies or H for a harmonic "
            "series, the Q and the gain of every filter ('~fffb 8 200 H 20 0.5'). Later "
            "messages do not change what is saved.",
            "a count, then frequency, ratio, Q and gain");
}

void dFffb::ShapePorts() {
  // Rebuilt rather than resized: the outlet count comes from the arguments.
  // Every caller runs before the object is wired or published.
  float values[5] = {static_cast<float>(DSP::filterBank::DEFAULT_FILTERS),
                     DSP::filterBank::DEFAULT_FREQUENCY, DSP::filterBank::DEFAULT_RATIO,
                     DSP::filterBank::DEFAULT_Q, DSP::filterBank::DEFAULT_GAIN};
  bool harmonic = false;
  unsigned int position = 0;
  for (const std::string& token : arguments) {
    if (position >= 5) break;
    float number = 0.f;
    // The third argument may be H instead of a ratio.
    if (position == 2 && Is(token, "h")) {
      harmonic = true;
      position++;
      continue;
    }
    if (!ReadNumber(token, number)) continue;
    values[position++] = number;
  }

  const float count = std::round(values[0]);
  fn = DSP::filterBank(count < 1.f ? 1u : static_cast<unsigned int>(std::fmin(count, MAX)));
  if (harmonic)
    fn.harmonic(values[1]);
  else
    fn.frequencyRatio(values[1], values[2]);
  fn.qAll(values[3]);
  fn.gainAll(values[4]);
  for (unsigned int i = 0; i < MAX; ++i) {
    freq[i].store(fn.frequency(i), std::memory_order_relaxed);
    quality[i].store(fn.q(i), std::memory_order_relaxed);
    amp[i].store(fn.gain(i), std::memory_order_relaxed);
  }
  applied = generation.load(std::memory_order_relaxed);
  clearRequested = false;

  inputs.clear();
  outputs.clear();
  ADD_IN_0;
  REG_SIGNAL_IN;
  INLET_DOC(0, "in",
            "The signal every filter reads (unconnected reads 0), and the messages, indexes from "
            "0: 'freq <index> <hz>...', 'q <index> <q>...' and 'gain <index> <gain>...' set "
            "filters from the index on; 'freqall', 'qall' and 'gainall' set every filter; "
            "'freqratio <first> <ratio>' spreads the frequencies as first * ratio^i, "
            "'freqratio <first> H' as a harmonic series; a list '<index> <hz> [<q>]' sets one "
            "filter; 'clear' sets every filter's state to 0.",
            "");
  for (unsigned int i = 0; i < fn.size(); ++i) {
    ADD_OUT_BUFFER;
    const std::string n = std::to_string(i);
    outputs.back().SetDoc("filter " + n,
                          "The input through filter " + n +
                              ", a bandpass with its gain at its center frequency.",
                          "");
  }
}

void dFffb::Publish() {
  generation.fetch_add(1, std::memory_order_release);
}

PARM_CLEAR() {
  arguments.clear();
  ShapePorts();
}

PARM_PARSE() {
  ShapePorts();
}

MESSAGES() {
  (void)value;
  const unsigned int count = fn.size();
  std::size_t next = 0;
  const std::string_view word = Token(message, 0, next);
  float number = 0.f;

  if (Is(word, "clear")) {
    clearRequested.store(true, std::memory_order_release);
    return;
  }

  // One setting from an index on, one value per filter.
  std::array<std::atomic<float>, MAX>* from = nullptr;
  if (Is(word, "freq"))
    from = &freq;
  else if (Is(word, "q"))
    from = &quality;
  else if (Is(word, "gain"))
    from = &amp;
  if (from != nullptr) {
    unsigned int index = 0;
    if (!ReadIndex(Token(message, next, next), index)) return;
    bool changed = false;
    while (index < count && ReadNumber(Token(message, next, next), number)) {
      (*from)[index++].store(number, std::memory_order_relaxed);
      changed = true;
    }
    if (changed) Publish();
    return;
  }

  // One setting for every filter.
  if (Is(word, "freqall"))
    from = &freq;
  else if (Is(word, "qall"))
    from = &quality;
  else if (Is(word, "gainall"))
    from = &amp;
  if (from != nullptr) {
    if (!ReadNumber(Token(message, next, next), number)) return;
    for (unsigned int i = 0; i < count; ++i)
      (*from)[i].store(number, std::memory_order_relaxed);
    Publish();
    return;
  }

  if (Is(word, "freqratio")) {
    float first = 0.f;
    if (!ReadNumber(Token(message, next, next), first)) return;
    const std::string_view spread = Token(message, next, next);
    if (Is(spread, "h")) {
      for (unsigned int i = 0; i < count; ++i)
        freq[i].store(DSP::filterBank::HarmonicFrequency(first, i), std::memory_order_relaxed);
    } else {
      if (!ReadNumber(spread, number) || number <= 0.f) return;
      for (unsigned int i = 0; i < count; ++i)
        freq[i].store(DSP::filterBank::RatioFrequency(first, number, i), std::memory_order_relaxed);
    }
    Publish();
    return;
  }

  // A list: index, frequency, optionally the Q.
  unsigned int index = 0;
  if (!ReadIndex(word, index) || index >= count) return;
  if (!ReadNumber(Token(message, next, next), number)) return;
  freq[index].store(number, std::memory_order_relaxed);
  if (ReadNumber(Token(message, next, next), number))
    quality[index].store(number, std::memory_order_relaxed);
  Publish();
}

CALC() {
  if (clearRequested.exchange(false, std::memory_order_acquire)) fn.clear();

  const std::uint32_t g = generation.load(std::memory_order_acquire);
  if (g != applied) {
    applied = g;
    for (unsigned int i = 0; i < fn.size(); ++i) {
      fn.frequency(i, freq[i].load(std::memory_order_relaxed));
      fn.q(i, quality[i].load(std::memory_order_relaxed));
      fn.gain(i, amp[i].load(std::memory_order_relaxed));
    }
  }

  // Unconnected input (#1036): reads 0.
  fn(inputs[0].Signal(0.f));
  for (unsigned int i = 0; i < fn.size(); ++i)
    outputs[i].SendBuffer(&fn.output(i), thread);
}

#undef className
