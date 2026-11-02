#include "dComp.h"
#include "../pListArgs.h"
#include "../pSelector.h"

#include <algorithm>
#include <cmath>
#include <string_view>

using namespace YSE::PATCHER;
using YSE::DSP::MODULES::compressorDetector;

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

  // DSP::MODULES::compressor's defaults.
  constexpr float DEFAULT_THRESHOLD = -18.f;
  constexpr float DEFAULT_RATIO = 4.f;
  constexpr float DEFAULT_ATTACK = 10.f;
  constexpr float DEFAULT_RELEASE = 100.f;
} // namespace

#define className dComp

CONSTRUCT_DSP(), stereo(2), key(1) {
  // Size the scratch here, on the control thread: the class sizes its own
  // per-block buffers on the first block it sees, so one silent block (which
  // leaves its gain at 1) keeps that allocation off the audio thread.
  stereo[0].resize(STANDARD_BUFFERSIZE);
  stereo[1].resize(STANDARD_BUFFERSIZE);
  key[0].resize(STANDARD_BUFFERSIZE);
  fn.process(stereo);
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

  // in 2: sidechain
  ADD_IN_2;
  REG_SIGNAL_IN;

  ADD_OUT_BUFFER; // 0: left
  ADD_OUT_BUFFER; // 1: right
  ADD_OUT_FLOAT; // 2: gain reduction

  REG_PARM_CLEAR;
  REG_PARM_PARSE;
  ADD_PARAM(settings);

  ADD_DESCRIPTION("A stereo compressor: when the level goes over the threshold, both channels "
                  "are turned down by the same amount, set by the ratio, attack and release. "
                  "Takes a sidechain signal and reports the gain reduction.");
  ADD_CATEGORY(pCategory::DYNAMICS);
  INLET_DOC(0, "left",
            "The left signal (unconnected, the last number sent here, else 0). Takes "
            "threshold <dB>, ratio <n>, attack <ms>, release <ms>, makeup <dB>, detector <0|1>, "
            "mix <0-1>, bypass <0|1>, meters <0|1>, meterrate <ms> and meterdata.",
            "any float");
  INLET_DOC(1, "right", "The right signal (unconnected, the last number sent here, else 0).",
            "any float");
  INLET_DOC(2, "sidechain",
            "While connected, the level is taken from this signal instead of the left and right "
            "signal. Unconnected, the compressor listens to its own input.",
            "any float");
  OUTLET_DOC(0, "left", "The left signal compressed.", "any float");
  OUTLET_DOC(1, "right", "The right signal compressed.", "any float");
  OUTLET_DOC(2, "reduction",
             "The deepest gain reduction since the last report, in dB, read at the end of each "
             "block: every meterrate ms while meters is on, and after the next block on "
             "meterdata.",
             "0 or less");
  PARAM_DOC("settings", "",
            "Any of 'threshold <dB>' (-18, -60 to 0), 'ratio <n>' (4, 1 to 20), 'attack <ms>' "
            "(10), 'release <ms>' (100), 'makeup <dB>' (0, -24 to 24), 'detector <0|1>' (0: "
            "peak, 1: RMS), 'mix <0-1>' (1, fully compressed), 'bypass <0|1>' (0), 'meters "
            "<0|1>' (1) and 'meterrate <ms>' (100).",
            "words and values");
}

void dComp::Defaults() {
  threshold = DEFAULT_THRESHOLD;
  ratio = DEFAULT_RATIO;
  attack = DEFAULT_ATTACK;
  release = DEFAULT_RELEASE;
  makeup = 0.f;
  mix = 1.f;
  meterRate = DEFAULT_METER_RATE;
  detector = DSP::MODULES::DETECT_PEAK;
  bypass = false;
  meters = true;
  meterRequested = false;
  for (aFlt& c : constants)
    c = 0.f;
}

bool dComp::ApplyWord(std::string_view word, float value) {
  if (!std::isfinite(value)) return false;
  if (word == "threshold") {
    threshold = value;
  } else if (word == "ratio") {
    ratio = value;
  } else if (word == "attack") {
    attack = value;
  } else if (word == "release") {
    release = value;
  } else if (word == "makeup") {
    makeup = value;
  } else if (word == "detector") {
    detector = value != 0.f ? DSP::MODULES::DETECT_RMS : DSP::MODULES::DETECT_PEAK;
  } else if (word == "mix") {
    mix = std::clamp(value, 0.f, 1.f);
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
  // The setters only store (and clamp) the values.
  fn.threshold(threshold.load());
  fn.ratio(ratio.load());
  fn.attack(attack.load());
  fn.release(release.load());
  fn.makeup(makeup.load());
  fn.detector(static_cast<compressorDetector>(detector.load()));
  // Bypass is a fully dry mix: the input comes out unchanged while the
  // detector keeps running, so switching back does not jump.
  fn.impact(bypass.load() ? 0.f : mix.load());

  // Unconnected inlets (#1036): each channel reads its number. The class
  // compresses in place, so it works on a copy.
  stereo[0] = inputs[0].Signal(constants[0].load());
  stereo[1] = inputs[1].Signal(constants[1].load());
  if (inputs[2].IsConstant()) {
    fn.process(stereo);
  } else {
    key[0] = inputs[2].Signal(0.f);
    fn.process(stereo, key);
  }
  outputs[0].SendBuffer(&stereo[0], thread);
  outputs[1].SendBuffer(&stereo[1], thread);

  // The meter: the deepest reduction since the last report.
  deepest = std::min(deepest, static_cast<float>(fn.gainReductionDb()));
  samplesSinceReport += static_cast<double>(stereo[0].getLength());
  const double interval = static_cast<double>(meterRate.load()) * 0.001 * SAMPLERATE;
  const bool due = meters.load() && samplesSinceReport >= interval;
  if (meterRequested.exchange(false) || due) {
    outputs[2].ArmFloat(deepest);
    deepest = 0.f;
    samplesSinceReport = 0.0;
  }
}

#undef className
