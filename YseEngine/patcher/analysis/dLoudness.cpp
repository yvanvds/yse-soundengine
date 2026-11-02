#include "dLoudness.h"
#include "../pListArgs.h"
#include "../pSelector.h"

#include <cmath>
#include <string_view>

using namespace YSE::PATCHER;
using YSE::DSP::loudnessMeter;

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

  // A peak channel as Max's int takes it: 0 for all; NaN is rejected, a
  // negative number is 0.
  bool ReadChannel(float value, int& out) {
    if (std::isnan(value)) return false;
    const auto most = static_cast<float>(loudnessMeter::MAX_CHANNELS);
    if (value >= most) {
      out = static_cast<int>(loudnessMeter::MAX_CHANNELS);
    } else if (value < 0.f) {
      out = 0;
    } else {
      out = static_cast<int>(value); // Max converts a float to an int
    }
    return true;
  }
} // namespace

#define className dLoudness

CONSTRUCT_DSP() {
  REG_PARM_CLEAR;
  REG_PARM_PARSE;
  ADD_PARAM(arguments);

  ShapePorts();

  // out 0 - 5: momentary, short-term, integrated, loudness range, sample
  // peak, true peak; sent after the block (issue #1037)
  ADD_OUT_FLOAT;
  ADD_OUT_FLOAT;
  ADD_OUT_FLOAT;
  ADD_OUT_FLOAT;
  ADD_OUT_FLOAT;
  ADD_OUT_FLOAT;

  ADD_DESCRIPTION("Measures loudness after EBU R 128 and ITU-R BS.1770: the momentary (400 "
                  "ms), short-term (3 s) and gated integrated loudness of one or more signals "
                  "in LUFS, their loudness range in LU, and their sample peak and true peak, "
                  "on a bang or every interval.");
  ADD_CATEGORY(pCategory::ANALYSIS);
  OUTLET_DOC(0, "momentary",
             "The momentary loudness in LUFS: the K-weighted level of the last 400 ms. -200 "
             "for silence.",
             "-200 or more LUFS");
  OUTLET_DOC(1, "short-term",
             "The short-term loudness in LUFS: the K-weighted level of the last 3 s. -200 for "
             "silence.",
             "-200 or more LUFS");
  OUTLET_DOC(2, "integrated",
             "The integrated loudness in LUFS since the start or the last reset, gated at -70 "
             "LUFS and 10 LU below the mean. -200 while nothing passes the gates.",
             "-200 or more LUFS");
  OUTLET_DOC(3, "loudness range",
             "The loudness range in LU since the start or the last reset (EBU Tech 3342): the "
             "spread of the short-term loudness between its 10th and 95th percentiles. 0 until "
             "3 s have been measured.",
             "0 or more LU");
  OUTLET_DOC(4, "sample peak",
             "The largest sample magnitude since the start or the last reset in dBFS, of the "
             "chosen channel or of all channels. -200 for none.",
             "-200 or more dBFS");
  OUTLET_DOC(5, "true peak",
             "The true peak since the start or the last reset in dBTP, 4 times oversampled "
             "(ITU-R BS.1770), of the chosen channel or of all channels. -200 for none.",
             "-200 or more dBTP");
  PARAM_DOC("loudness", "1 0",
            "The number of signal inlets, measured together (1-64), then the reporting "
            "interval in milliseconds, 0 to report only on a bang ('~loudness 2 100'). Later "
            "numbers and messages do not change what is saved.",
            "a channel count, then an interval in ms");
}

void dLoudness::ShapePorts() {
  // The first number is the channel count, the second the interval.
  int count = 1;
  float ms = 0.f;
  int numbers = 0;
  for (const std::string& token : arguments) {
    float number = 0.f;
    if (token.empty() || !ReadNumericToken(token, number) || std::isnan(number)) continue;
    if (numbers == 0) {
      const auto most = static_cast<float>(loudnessMeter::MAX_CHANNELS);
      count = number >= most ? static_cast<int>(loudnessMeter::MAX_CHANNELS)
                             : (number < 1.f ? 1 : static_cast<int>(number));
    } else if (numbers == 1) {
      ms = number;
    }
    numbers++;
  }
  if (fn.channels() != static_cast<UInt>(count)) fn = loudnessMeter(static_cast<UInt>(count));
  interval = ms;
  channel = 0;

  // Rebuilt rather than resized, as ~tapout: the inlets follow the
  // arguments.
  inputs.clear();
  for (int i = 0; i < count; i++) {
    if (i == 0) {
      // in 0: a bang reports, a number picks the peak channel and reports;
      // 'channel <n>', 'interval <ms>', 'reset'
      ADD_IN_0;
      REG_SIGNAL_IN;
      REG_BANG_IN(Report);
      REG_FLOAT_IN(SetChannel);
      REG_INT_IN(SetChannelInt);
      INLET_DOC(0, count == 1 ? "signal" : "signal 1",
                "A signal to measure; unconnected it reads as silence. A bang reports; a "
                "number picks the channel whose peaks are reported (0 the highest of all) "
                "and reports. Also takes 'channel <n>', 'interval <ms>' (0 stops "
                "automatic reports) and 'reset', which starts the integrated loudness and "
                "the loudness range and the peaks over.",
                "any float");
    } else {
      inputs.emplace_back(this, false, i);
      REG_SIGNAL_IN;
      const std::string n = std::to_string(i + 1);
      inputs.back().SetDoc("signal " + n,
                           "Channel " + n +
                               " of the signal to measure; unconnected it reads as silence.",
                           "any float");
    }
  }
}

PARM_CLEAR() {
  arguments.clear();
  ShapePorts();
}

PARM_PARSE() {
  ShapePorts();
}

BANG_IN(Report) {
  (void)inlet;
  (void)thread;
  report = true;
}

FLOAT_IN(SetChannel) {
  (void)inlet;
  (void)thread;
  int parsed = 0;
  if (!ReadChannel(value, parsed)) return;
  channel = parsed;
  report = true;
}

INT_IN(SetChannelInt) {
  (void)inlet;
  (void)thread;
  channel = value < 0 ? 0 : value;
  report = true;
}

MESSAGES() {
  (void)value;
  // 'reset', 'channel <n>' and 'interval <ms>'; a missing or malformed
  // number is ignored.
  std::size_t next = 0;
  const std::string_view word = Token(message, 0, next);
  if (word == "reset") {
    resetRequested = true;
    return;
  }
  const bool isInterval = word == "interval";
  if (!isInterval && word != "channel") return;
  const std::string_view token = Token(message, next, next);
  float number = 0.f;
  if (!ReadNumericToken(token.data(), token.size(), number) || std::isnan(number)) return;
  if (isInterval) {
    interval = number;
    return;
  }
  int parsed = 0;
  if (ReadChannel(number, parsed)) channel = parsed;
}

CALC() {
  // Never Send* floats from CALC(): the patcher sends these after the
  // traversal. A report gives the values up to the end of the last block,
  // then a reset that came with it starts over.
  const auto arm = [this]() {
    const auto pick = static_cast<UInt>(channel.load());
    outputs[5].ArmFloat(fn.truePeak(pick));
    outputs[4].ArmFloat(fn.peak(pick));
    outputs[3].ArmFloat(fn.loudnessRange());
    outputs[2].ArmFloat(fn.integrated());
    outputs[1].ArmFloat(fn.shortTerm());
    outputs[0].ArmFloat(fn.momentary());
  };
  if (report.exchange(false)) arm();
  if (resetRequested.exchange(false)) fn.reset();
  fn.interval(interval.load());
  // Unconnected inlets read as silence (#1036).
  const std::size_t count = inputs.size();
  for (std::size_t i = 0; i < count; ++i)
    ins[i] = &inputs[i].Signal(0.f);
  if (fn(ins.data(), static_cast<UInt>(count)) > 0) arm();
  (void)thread;
}

#undef className
