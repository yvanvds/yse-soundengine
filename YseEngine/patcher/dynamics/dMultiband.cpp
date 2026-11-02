#include "dMultiband.h"
#include "../pListArgs.h"
#include "../pSelector.h"

#include <algorithm>
#include <cmath>
#include <string_view>

using namespace YSE::PATCHER;
using YSE::DSP::multibandCompressor;

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

  bool Number(std::string_view token, float& out) {
    return !token.empty() && ReadNumericToken(token.data(), token.size(), out);
  }

  // A count from a number: rounded, at least `low`, at most `high`.
  UInt Count(float value, UInt low, UInt high) {
    if (!(value >= static_cast<float>(low))) return low;
    if (value >= static_cast<float>(high)) return high;
    return static_cast<UInt>(std::lround(value));
  }
} // namespace

#define className dMultiband

CONSTRUCT_DSP(), stereo(2) {
  stereo[0].resize(STANDARD_BUFFERSIZE);
  stereo[1].resize(STANDARD_BUFFERSIZE);

  REG_PARM_CLEAR;
  REG_PARM_PARSE;
  ADD_PARAM(settings);

  ShapePorts();

  ADD_DESCRIPTION("A multiband compressor: splits a stereo signal into 2 to 5 frequency bands, "
                  "compresses each band on its own and adds them back together. Reports the "
                  "gain reduction of every band.");
  ADD_CATEGORY(pCategory::DYNAMICS);
  PARAM_DOC("settings", "4",
            "The band count (2-5, default 4), then any of 'threshold <dB>' (-18, -60 to 0), "
            "'ratio <n>' (4, 1 to 20), 'attack <ms>' (10), 'release <ms>' (100), 'makeup <dB>' "
            "(0, -24 to 24), 'drive <dB>' (0) and 'level <dB>' (0, both -96 to 24) for every "
            "band, or for one band with its number after the word ('threshold2 -24'); 'freq1' "
            "to 'freq4 <Hz>' (the crossovers, default 200 1000 5000 for 4 bands, 120 500 2000 "
            "8000 for 5); 'output <dB>' (0); 'detector <0|1>' (0: peak, 1: RMS); 'bypass "
            "<0|1>' (0); 'meters <0|1>' (1) and 'meterrate <ms>' (100).",
            "a number, then words and values");
}

bool dMultiband::ApplyWord(std::string_view word, float value) {
  if (!std::isfinite(value) || word.empty()) return false;
  if (word == "output") {
    fn->output(value);
  } else if (word == "detector") {
    fn->detector(value != 0.f ? DSP::MODULES::DETECT_RMS : DSP::MODULES::DETECT_PEAK);
  } else if (word == "bypass") {
    fn->bypass(value != 0.f);
  } else if (word == "meters") {
    meters = value != 0.f;
  } else if (word == "meterrate") {
    meterRate = std::max(value, 0.f);
  } else {
    // A per-band word, with the band number (from 1) after it, or without
    // for every band.
    const char tail = word.back();
    const bool numbered = tail >= '1' && tail <= '9';
    const std::string_view base = numbered ? word.substr(0, word.size() - 1) : word;
    const UInt bands = fn->bands();
    if (base == "freq") {
      if (!numbered) return false;
      const UInt index = static_cast<UInt>(tail - '1');
      if (index + 1 >= bands) return false;
      fn->crossover(index, value);
      return true;
    }
    UInt from = 0;
    UInt to = bands;
    if (numbered) {
      from = static_cast<UInt>(tail - '1');
      if (from >= bands) return false;
      to = from + 1;
    }
    for (UInt b = from; b < to; ++b) {
      if (base == "threshold") {
        fn->threshold(b, value);
      } else if (base == "ratio") {
        fn->ratio(b, value);
      } else if (base == "attack") {
        fn->attack(b, value);
      } else if (base == "release") {
        fn->release(b, value);
      } else if (base == "makeup") {
        fn->makeup(b, value);
      } else if (base == "drive") {
        fn->drive(b, value);
      } else if (base == "level") {
        fn->level(b, value);
      } else {
        return false;
      }
    }
  }
  return true;
}

void dMultiband::ShapePorts() {
  meterRate = DEFAULT_METER_RATE;
  meters = true;
  meterRequested = false;
  samplesSinceReport = 0.0;
  deepest.fill(0.f);
  for (aFlt& c : constants)
    c = 0.f;

  // A leading number: the band count. Then word and value pairs.
  UInt bands = multibandCompressor::DEFAULT_BANDS;
  std::size_t at = 0;
  float number = 0.f;
  if (!settings.empty() && ReadNumericToken(settings[0], number)) {
    bands = Count(number, multibandCompressor::MIN_BANDS, multibandCompressor::MAX_BANDS);
    at = 1;
  }

  // Rebuilt rather than resized: the pins and the DSP class have to agree.
  // Every caller runs before the object is wired or published.
  fn = std::make_unique<multibandCompressor>(bands, 2);
  for (; at + 1 < settings.size(); at += 2) {
    if (ReadNumericToken(settings[at + 1], number)) ApplyWord(settings[at], number);
  }

  inputs.clear();
  outputs.clear();

  // in 0: left; the settings as messages
  ADD_IN_0;
  REG_SIGNAL_IN;
  REG_FLOAT_IN(SetFloat);
  REG_INT_IN(SetInt);
  inputs.back().SetDoc("left",
                       "The left signal (unconnected, the last number sent here, else 0). Takes "
                       "the settings as messages: threshold, ratio, attack, release, makeup, "
                       "drive and level <value> (every band, or one with its number after the "
                       "word: threshold2 <dB>), freq1-freq4 <Hz>, output <dB>, detector <0|1>, "
                       "bypass <0|1>, meters <0|1>, meterrate <ms>, and meterdata.",
                       "any float");

  // in 1: right
  ADD_IN_1;
  REG_SIGNAL_IN;
  REG_FLOAT_IN(SetFloat);
  REG_INT_IN(SetInt);
  inputs.back().SetDoc(
      "right", "The right signal (unconnected, the last number sent here, else 0).", "any float");

  ADD_OUT_BUFFER; // 0: left
  outputs.back().SetDoc("left", "The left signal compressed, its bands added back together.",
                        "any float");
  ADD_OUT_BUFFER; // 1: right
  outputs.back().SetDoc("right", "The right signal compressed, its bands added back together.",
                        "any float");
  for (UInt b = 0; b < bands; ++b) {
    ADD_OUT_FLOAT; // 2 + b: band b's gain reduction
    const std::string n = std::to_string(b + 1);
    outputs.back().SetDoc("band " + n,
                          "The deepest gain reduction of band " + n +
                              " since the last report, in dB, read at the end of each block: "
                              "every meterrate ms while meters is on, and after the next block "
                              "on meterdata.",
                          "0 or less");
  }
}

// Control thread: a list parameter rebuilds the object, so these run on one
// no other thread has seen.
PARM_CLEAR() {
  settings.clear();
  ShapePorts();
}

PARM_PARSE() {
  ShapePorts();
}

// A number into an inlet is that channel's level while it is unconnected.
FLOAT_IN(SetFloat) {
  (void)thread;
  if (inlet == 0 || inlet == 1) constants[static_cast<std::size_t>(inlet)] = value;
}

INT_IN(SetInt) {
  SetFloat(static_cast<float>(value), inlet, thread);
}

// The DSP class's setters store atomics, so a message applies them from
// whichever thread delivers it.
MESSAGES() {
  (void)value;
  std::size_t next = 0;
  const std::string_view word = Token(message, 0, next);
  if (word == "meterdata") {
    meterRequested = true;
    return;
  }
  float number = 0.f;
  if (Number(Token(message, next, next), number)) ApplyWord(word, number);
}

CALC() {
  // Unconnected inlets (#1036): each channel reads its number. The class
  // works in place, so it works on a copy.
  stereo[0] = inputs[0].Signal(constants[0].load());
  stereo[1] = inputs[1].Signal(constants[1].load());
  fn->process(stereo);
  outputs[0].SendBuffer(&stereo[0], thread);
  outputs[1].SendBuffer(&stereo[1], thread);

  // The meters: each band's deepest reduction since the last report.
  const UInt bands = fn->bands();
  for (UInt b = 0; b < bands; ++b)
    deepest[b] = std::min(deepest[b], static_cast<float>(fn->gainReduction(b)));
  samplesSinceReport += static_cast<double>(stereo[0].getLength());
  const double interval = static_cast<double>(meterRate.load()) * 0.001 * SAMPLERATE;
  const bool due = meters.load() && samplesSinceReport >= interval;
  if (meterRequested.exchange(false) || due) {
    for (UInt b = 0; b < bands; ++b) {
      outputs[2 + b].ArmFloat(deepest[b]);
      deepest[b] = 0.f;
    }
    samplesSinceReport = 0.0;
  }
}

#undef className
