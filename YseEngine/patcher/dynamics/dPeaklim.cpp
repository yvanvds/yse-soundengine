#include "dPeaklim.h"
#include "../pListArgs.h"
#include "../pSelector.h"

#include <algorithm>
#include <cmath>
#include <string_view>

using namespace YSE::PATCHER;
using YSE::DSP::lookaheadLimiter;

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
} // namespace

#define className dPeaklim

CONSTRUCT_DSP(), fn(2, LOOKAHEAD) {
  fn.lookahead(LOOKAHEAD);
  Defaults();

  // in 0: left; the settings as messages
  ADD_IN_0;
  REG_SIGNAL_IN;
  REG_FLOAT_IN(SetFloat);
  REG_INT_IN(SetInt);

  // in 1: right
  ADD_IN_1;
  REG_SIGNAL_IN;
  REG_FLOAT_IN(SetFloat);
  REG_INT_IN(SetInt);

  ADD_OUT_BUFFER; // 0: left
  ADD_OUT_BUFFER; // 1: right
  ADD_OUT_FLOAT; // 2: gain reduction

  REG_PARM_CLEAR;
  REG_PARM_PARSE;
  ADD_PARAM(settings);

  ADD_DESCRIPTION("A stereo brickwall peak limiter for a master bus: both channels share one "
                  "gain, brought down 64 samples before a peak arrives, so no output sample "
                  "exceeds the threshold. Reports the gain reduction.");
  ADD_CATEGORY(pCategory::DYNAMICS);
  INLET_DOC(0, "left",
            "The left signal (unconnected, the last number sent here, else 0). Takes "
            "threshold <dB>, ingain <dB>, outgain <dB>, mode <0|1>, bypass <0|1>, meters <0|1>, "
            "meterrate <ms> and meterdata.",
            "any float");
  INLET_DOC(1, "right", "The right signal (unconnected, the last number sent here, else 0).",
            "any float");
  OUTLET_DOC(0, "left", "The left signal limited, 64 samples late.", "any float");
  OUTLET_DOC(1, "right", "The right signal limited, 64 samples late.", "any float");
  OUTLET_DOC(2, "reduction",
             "The deepest gain reduction since the last report, in dB: every meterrate ms "
             "while meters is on, and after the next block on meterdata.",
             "0 or less");
  PARAM_DOC("settings", "",
            "Any of 'threshold <dB>' (0), 'ingain <dB>' (0), 'outgain <dB>' (0), 'mode <0|1>' "
            "(1, the release curve: 0 linear, 1 exponential), 'bypass <0|1>' (0), 'meters "
            "<0|1>' (1) and 'meterrate <ms>' (100).",
            "words and values");
}

void dPeaklim::Defaults() {
  threshold = lookaheadLimiter::DEFAULT_THRESHOLD;
  ingain = 0.f;
  outgain = 0.f;
  meterRate = DEFAULT_METER_RATE;
  mode = lookaheadLimiter::EXPONENTIAL;
  bypass = false;
  meters = true;
  meterRequested = false;
  for (aFlt& c : constants)
    c = 0.f;
}

bool dPeaklim::ApplyWord(std::string_view word, float value) {
  if (!std::isfinite(value)) return false;
  if (word == "threshold") {
    threshold = value;
  } else if (word == "ingain") {
    ingain = value;
  } else if (word == "outgain") {
    outgain = value;
  } else if (word == "mode") {
    mode = value != 0.f ? lookaheadLimiter::EXPONENTIAL : lookaheadLimiter::LINEAR;
  } else if (word == "bypass") {
    bypass = value != 0.f;
  } else if (word == "meters") {
    meters = value != 0.f;
  } else if (word == "meterrate") {
    meterRate = std::max(value, 0.f);
  } else {
    return false;
  }
  return true;
}

// Control thread: a list parameter rebuilds the object, so these run on one
// no other thread has seen.
PARM_CLEAR() {
  settings.clear();
  Defaults();
}

PARM_PARSE() {
  Defaults();
  for (std::size_t at = 0; at + 1 < settings.size(); at += 2) {
    float number = 0.f;
    if (ReadNumericToken(settings[at + 1], number)) ApplyWord(settings[at], number);
  }
}

// A number into an inlet is that channel's level while it is unconnected.
FLOAT_IN(SetFloat) {
  (void)thread;
  if (inlet == 0 || inlet == 1) constants[static_cast<std::size_t>(inlet)] = value;
}

INT_IN(SetInt) {
  SetFloat(static_cast<float>(value), inlet, thread);
}

MESSAGES() {
  (void)value;
  std::size_t next = 0;
  const std::string_view word = Token(message, 0, next);
  if (word == "meterdata") {
    meterRequested = true;
    return;
  }
  const std::string_view token = Token(message, next, next);
  float number = 0.f;
  if (!token.empty() && ReadNumericToken(token.data(), token.size(), number))
    ApplyWord(word, number);
}

CALC() {
  // The DSP class only rebuilds its derived state when a value changes.
  fn.threshold(threshold.load());
  fn.preamp(ingain.load());
  fn.postamp(outgain.load());
  fn.mode(static_cast<lookaheadLimiter::MODE>(mode.load()));
  fn.bypass(bypass.load());

  // Unconnected inlets (#1036): each channel reads its number.
  DSP::buffer* ins[2] = {&inputs[0].Signal(constants[0].load()),
                         &inputs[1].Signal(constants[1].load())};
  fn(ins, 2);
  outputs[0].SendBuffer(&fn.output(0), thread);
  outputs[1].SendBuffer(&fn.output(1), thread);

  // The meter: the deepest reduction since the last report.
  deepest = std::min(deepest, fn.gainReduction());
  samplesSinceReport += static_cast<double>(fn.output(0).getLength());
  const double interval = static_cast<double>(meterRate.load()) * 0.001 * SAMPLERATE;
  const bool due = meters.load() && samplesSinceReport >= interval;
  if (meterRequested.exchange(false) || due) {
    outputs[2].ArmFloat(deepest);
    deepest = 0.f;
    samplesSinceReport = 0.0;
  }
}

#undef className
