#include "dLimi.h"
#include "../pListArgs.h"
#include "../pSelector.h"

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

#define className dLimi

CONSTRUCT_DSP() {
  REG_PARM_CLEAR;
  REG_PARM_PARSE;
  ADD_PARAM(settings);

  ShapePorts();

  ADD_DESCRIPTION("A brickwall lookahead peak limiter: delays the signal by the lookahead and "
                  "brings the gain down before a peak arrives, so no output sample exceeds the "
                  "threshold. One inlet and outlet per channel; the channels share one gain.");
  ADD_CATEGORY(pCategory::DYNAMICS);
  PARAM_DOC("settings", "1 512",
            "The channel count (1-64, default 1) and the delay-line size in samples (default "
            "512, the longest lookahead), then any of 'threshold <dB>' (0), 'release <ms>' "
            "(100), 'lookahead <samples>' (100), 'preamp <dB>' (0), 'postamp <dB>' (0), 'mode "
            "<0|1>' (1), 'dcblock <0|1>' (0) and 'bypass <0|1>' (0).",
            "numbers, then words and values");
}

bool dLimi::ApplyWord(std::string_view word, float value) {
  if (!std::isfinite(value)) return false;
  if (word == "threshold") {
    threshold = value;
  } else if (word == "release") {
    release = value;
  } else if (word == "lookahead") {
    lookahead = static_cast<int>(Count(value, 0, lookaheadLimiter::MAX_LOOKAHEAD));
  } else if (word == "preamp") {
    preamp = value;
  } else if (word == "postamp") {
    postamp = value;
  } else if (word == "mode") {
    mode = value != 0.f ? lookaheadLimiter::EXPONENTIAL : lookaheadLimiter::LINEAR;
  } else if (word == "dcblock") {
    dcblock = value != 0.f;
  } else if (word == "bypass") {
    bypass = value != 0.f;
  } else {
    return false;
  }
  return true;
}

void dLimi::ShapePorts() {
  threshold = lookaheadLimiter::DEFAULT_THRESHOLD;
  release = lookaheadLimiter::DEFAULT_RELEASE;
  preamp = 0.f;
  postamp = 0.f;
  lookahead = static_cast<int>(lookaheadLimiter::DEFAULT_LOOKAHEAD);
  mode = lookaheadLimiter::EXPONENTIAL;
  dcblock = false;
  bypass = false;
  clearRequested = false;
  for (aFlt& c : constants)
    c = 0.f;

  // Leading numbers: the channel count and the delay-line size. Then word
  // and value pairs.
  UInt channels = 1;
  UInt size = lookaheadLimiter::DEFAULT_MAX_LOOKAHEAD;
  std::size_t at = 0;
  for (int position = 0; at < settings.size() && position < 2; ++position, ++at) {
    float number = 0.f;
    if (!ReadNumericToken(settings[at], number)) break;
    if (position == 0)
      channels = Count(number, 1, lookaheadLimiter::MAX_CHANNELS);
    else
      size = Count(number, 1, lookaheadLimiter::MAX_LOOKAHEAD);
  }
  for (; at + 1 < settings.size(); at += 2) {
    float number = 0.f;
    if (ReadNumericToken(settings[at + 1], number)) ApplyWord(settings[at], number);
  }

  // Rebuilt rather than resized: the pins and the limiter have to agree.
  // Every caller runs before the object is wired or published.
  fn = std::make_unique<lookaheadLimiter>(channels, size);
  ins.assign(channels, nullptr);
  inputs.clear();
  outputs.clear();
  for (UInt i = 0; i < channels; i++) {
    if (i == 0) {
      ADD_IN_0;
    } else {
      inputs.emplace_back(this, false, static_cast<int>(i));
    }
    REG_SIGNAL_IN;
    REG_FLOAT_IN(SetFloat);
    REG_INT_IN(SetInt);
    ADD_OUT_BUFFER;

    const std::string n = std::to_string(i + 1);
    inputs.back().SetDoc(
        "channel " + n,
        "The signal of channel " + n + " (unconnected, the last number sent here, else 0)." +
            (i == 0 ? std::string(" Takes the settings as messages: threshold <dB>, release "
                                  "<ms>, lookahead <samples>, preamp <dB>, postamp <dB>, "
                                  "mode <0|1>, dcblock <0|1>, bypass <0|1>, and clear.")
                    : std::string()),
        "any float");
    outputs.back().SetDoc("channel " + n,
                          "Channel " + n +
                              " limited to the threshold (then the postamp), lookahead samples "
                              "late.",
                          "any float");
  }
}

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
  if (inlet >= 0 && inlet < static_cast<int>(constants.size()))
    constants[static_cast<std::size_t>(inlet)] = value;
}

INT_IN(SetInt) {
  SetFloat(static_cast<float>(value), inlet, thread);
}

MESSAGES() {
  (void)value;
  std::size_t next = 0;
  const std::string_view word = Token(message, 0, next);
  if (word == "clear") {
    clearRequested = true;
    return;
  }
  float number = 0.f;
  if (Number(Token(message, next, next), number)) ApplyWord(word, number);
}

CALC() {
  // The DSP class only rebuilds its derived state when a value changes.
  fn->threshold(threshold.load());
  fn->release(release.load());
  fn->preamp(preamp.load());
  fn->postamp(postamp.load());
  fn->mode(static_cast<lookaheadLimiter::MODE>(mode.load()));
  fn->lookahead(static_cast<UInt>(lookahead.load()));
  fn->dcBlock(dcblock.load());
  fn->bypass(bypass.load());
  if (clearRequested.exchange(false)) fn->clear();

  // Unconnected inlets (#1036): each channel reads its number.
  const UInt count = fn->channels();
  for (UInt c = 0; c < count; c++)
    ins[c] = &inputs[c].Signal(constants[c].load());
  (*fn)(ins.data(), count);
  for (UInt c = 0; c < count; c++)
    outputs[c].SendBuffer(&fn->output(c), thread);
}

#undef className
