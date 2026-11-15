#include "dPitchshift.h"
#include "../pListArgs.h"
#include "../pSelector.h"

#include <cmath>
#include <string_view>

using namespace YSE::PATCHER;
using YSE::DSP::pitchShifter;

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
    return !token.empty() && ReadNumericToken(token.data(), token.size(), out) &&
           std::isfinite(out);
  }

  // A quality by name or number (0-3). False when it is neither.
  bool Quality(std::string_view token, int& out) {
    static constexpr std::string_view names[] = {"basic", "good", "better", "best"};
    for (int q = 0; q < 4; ++q) {
      if (token == names[q]) {
        out = q;
        return true;
      }
    }
    float number = 0.f;
    if (!Number(token, number)) return false;
    const long q = std::lround(number);
    if (q < pitchShifter::BASIC || q > pitchShifter::BEST) return false;
    out = static_cast<int>(q);
    return true;
  }
} // namespace

#define className dPitchshift

CONSTRUCT_DSP() {
  REG_PARM_CLEAR;
  REG_PARM_PARSE;
  ADD_PARAM(settings);

  ShapePorts();

  ADD_DESCRIPTION("Shifts the pitch of one or more channels by a ratio or in cents, keeping "
                  "their duration: a low-latency granular dual delay line, or a phase vocoder "
                  "for steadier tone at more latency. The last outlet reports the latency.");
  ADD_CATEGORY(pCategory::DELAY);
  PARAM_DOC("settings", "1",
            "The channel count (1-16, default 1), then any of 'pitchshift <ratio>' (1), "
            "'pitchshiftcent <cents>', 'quality <basic|good|better|best>' (basic), "
            "'constantlatency <0|1>' (0), 'enabled <0|1>' (1), 'reportlatency <0|1>' (0) and "
            "'usecents <0|1>' (0).",
            "a number, then words and values");
}

bool dPitchshift::ApplyWord(std::string_view word, std::string_view value) {
  if (word == "quality") {
    int q = 0;
    if (!Quality(value, q)) return false;
    quality = q;
    return true;
  }
  float number = 0.f;
  if (!Number(value, number)) return false;
  if (word == "pitchshift") {
    ratio = number;
  } else if (word == "pitchshiftcent") {
    ratio = pitchShifter::centsToRatio(number);
  } else if (word == "constantlatency") {
    constantLatency = number != 0.f;
  } else if (word == "enabled") {
    enabled = number != 0.f;
  } else if (word == "usecents") {
    useCents = number != 0.f;
  } else if (word == "reportlatency" || word == "getlatency") {
    reportLatency = number != 0.f;
    if (number != 0.f) reportOnce = true;
  } else {
    return false;
  }
  return true;
}

void dPitchshift::ShapePorts() {
  ratio = 1.f;
  quality = pitchShifter::BASIC;
  constantLatency = false;
  enabled = true;
  useCents = false;
  reportLatency = false;
  reportOnce = false;
  reported = -1;
  for (aFlt& c : constants)
    c = 0.f;

  // A leading number: the channel count. Then word and value pairs.
  UInt channels = 1;
  std::size_t at = 0;
  float number = 0.f;
  if (!settings.empty() && ReadNumericToken(settings[0], number)) {
    if (number >= static_cast<float>(MAX_CHANNELS))
      channels = MAX_CHANNELS;
    else if (number >= 1.f)
      channels = static_cast<UInt>(std::lround(number));
    at = 1;
  }
  for (; at + 1 < settings.size(); at += 2)
    ApplyWord(settings[at], settings[at + 1]);

  // Rebuilt rather than resized: the pins and the shifters have to agree.
  // Every caller runs before the object is wired or published.
  fn.clear();
  for (UInt i = 0; i < channels; i++)
    fn.push_back(
        std::make_unique<pitchShifter>(static_cast<pitchShifter::QUALITY>(quality.load())));
  inputs.clear();
  outputs.clear();
  for (UInt i = 0; i <= channels; i++) {
    if (i == 0) {
      ADD_IN_0;
    } else {
      inputs.emplace_back(this, false, static_cast<int>(i));
    }
    REG_SIGNAL_IN;
    REG_FLOAT_IN(SetFloat);
    REG_INT_IN(SetInt);
    if (i < channels) {
      const std::string n = std::to_string(i + 1);
      inputs.back().SetDoc("channel " + n,
                           "The signal of channel " + n +
                               " (unconnected, the last number sent here, else 0). Takes the "
                               "attribute messages, as every inlet does.",
                           "any float");
      ADD_OUT_BUFFER;
      outputs.back().SetDoc("channel " + n,
                            "Channel " + n + " shifted, the latency in samples late.", "any float");
    }
  }
  inputs.back().SetDoc(
      "shift",
      "The shift, a ratio (2 an octave up) or, with usecents 1, cents (100 a semitone up): a "
      "number or a signal read per sample. Takes pitchshift <ratio>, pitchshiftcent <cents>, "
      "quality <basic|good|better|best>, constantlatency, enabled, reportlatency and usecents "
      "<0|1>, and getlatency.",
      "0.25 to 4, or -2400 to 2400 cents");
  ADD_OUT_INT;
  outputs.back().SetDoc("latency",
                        "The latency in samples: when reportlatency is turned on, whenever it "
                        "changes while it is on, and on getlatency.",
                        "0 or more");
}

PARM_CLEAR() {
  settings.clear();
  ShapePorts();
}

PARM_PARSE() {
  ShapePorts();
}

// A number into a channel's inlet is its level while it is unconnected; into
// the last inlet it is the shift.
FLOAT_IN(SetFloat) {
  (void)thread;
  if (inlet < 0) return;
  const auto at = static_cast<std::size_t>(inlet);
  if (at == fn.size()) {
    if (std::isfinite(value)) ratio = useCents.load() ? pitchShifter::centsToRatio(value) : value;
  } else if (at < constants.size()) {
    constants[at] = value;
  }
}

INT_IN(SetInt) {
  SetFloat(static_cast<float>(value), inlet, thread);
}

MESSAGES() {
  (void)value;
  std::size_t next = 0;
  const std::string_view word = Token(message, 0, next);
  const std::string_view arg = Token(message, next, next);
  if (word == "getlatency" && arg.empty()) {
    reportOnce = true;
    return;
  }
  ApplyWord(word, arg);
}

CALC() {
  const auto count = static_cast<UInt>(fn.size());
  const auto q = static_cast<pitchShifter::QUALITY>(quality.load());
  inlet& shift = inputs[count];
  const bool signal = !shift.IsConstant();
  for (UInt c = 0; c < count; c++) {
    pitchShifter& p = *fn[c];
    p.quality(q)
        .constantLatency(constantLatency.load())
        .enabled(enabled.load())
        .usesCents(useCents.load());
    // Unconnected inlets (#1036): each channel reads its number; a
    // connected shift signal wins over the number.
    DSP::buffer& in = inputs[c].Signal(constants[c].load());
    if (signal) {
      p(in, shift.Signal(1.f));
    } else {
      p.ratio(ratio.load());
      p(in);
    }
  }
  // Right outlet first, as Max fires outlets right to left. The latency is
  // armed, sent after the traversal (#1037).
  const int latency = count > 0 ? static_cast<int>(fn[0]->latency()) : 0;
  const bool once = reportOnce.exchange(false);
  if (once || (reportLatency.load() && latency != reported)) {
    outputs[count].ArmInt(latency);
    reported = latency;
  }
  for (UInt c = count; c > 0; c--)
    outputs[c - 1].SendBuffer(&fn[c - 1]->output(), thread);
}

#undef className
