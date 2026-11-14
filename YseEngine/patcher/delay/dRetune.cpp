#include "dRetune.h"
#include "../pListArgs.h"
#include "../pSelector.h"

#include <cmath>
#include <string_view>

using namespace YSE::PATCHER;
using YSE::DSP::pitchShifter;
using YSE::DSP::tuning;

namespace {
  // The words ~retune takes: its attributes and commands. Anything else in
  // the settings is a value.
  constexpr std::string_view WORDS[] = {"correction_ambience_threshold",
                                        "correction_amount",
                                        "correction_bypass",
                                        "correction_threshold",
                                        "enablednotes",
                                        "notebase",
                                        "notelist",
                                        "pitchdetection",
                                        "quality",
                                        "reportlatency",
                                        "retune",
                                        "use_16bit",
                                        "windowsize",
                                        "defaulttuning",
                                        "flush",
                                        "getlatency",
                                        "list"};

  bool IsWord(std::string_view token) {
    for (const std::string_view w : WORDS)
      if (token == w) return true;
    return false;
  }

  bool Number(std::string_view token, float& out) {
    return !token.empty() && ReadNumericToken(token.data(), token.size(), out) &&
           std::isfinite(out);
  }

  // A Scala pitch: cents, or a ratio `a/b`. False when it is neither.
  bool Pitch(std::string_view token, float& cents) {
    const std::size_t slash = token.find('/');
    if (slash == std::string_view::npos) return Number(token, cents);
    float a = 0.f;
    float b = 0.f;
    if (!Number(token.substr(0, slash), a) || !Number(token.substr(slash + 1), b)) return false;
    if (!(a > 0.f) || !(b > 0.f)) return false;
    cents = tuning::ratioToCents(a / b);
    return true;
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

  // Up to `capacity` tokens of `text`, walked in place so nothing allocates
  // on whichever thread delivers the message. The number found.
  std::size_t Tokens(const std::string& text, std::string_view* out, std::size_t capacity) {
    const char* chars = text.c_str();
    const std::size_t size = text.size();
    std::size_t found = 0;
    std::size_t at = 0;
    while (found < capacity) {
      while (at < size && IsSelectorSeparator(chars[at]))
        at++;
      if (at == size) break;
      const std::size_t begin = at;
      while (at < size && !IsSelectorSeparator(chars[at]))
        at++;
      out[found++] = std::string_view(chars + begin, at - begin);
    }
    return found;
  }
} // namespace

#define className dRetune

CONSTRUCT_DSP() {
  REG_PARM_CLEAR;
  REG_PARM_PARSE;
  ADD_PARAM(settings);

  ADD_IN_0;
  REG_SIGNAL_IN;
  REG_FLOAT_IN(SetFloat);
  REG_INT_IN(SetInt);
  REG_LIST_IN(SetList);
  ADD_IN_1;
  REG_SIGNAL_IN;
  REG_FLOAT_IN(SetFloat);
  REG_INT_IN(SetInt);
  ADD_IN_2;
  REG_SIGNAL_IN;
  REG_FLOAT_IN(SetFloat);
  REG_INT_IN(SetInt);
  ADD_OUT_BUFFER;
  ADD_OUT_BUFFER;
  ADD_OUT_INT;

  Defaults(440.f);

  ADD_DESCRIPTION("Detects the pitch of a voice or instrument and corrects it toward the nearest "
                  "enabled note of a scale, or toward a pitch you give it: autotune. The scale "
                  "is a note list in cents or ratios on a base note, with any subset enabled.");
  ADD_CATEGORY(pCategory::DELAY);
  INLET_DOC(0, "signal",
            "The signal to correct (unconnected, the last number sent here, else 0). A list of "
            "MIDI notes and velocities enables or disables the scale degrees they fall on. Every "
            "inlet takes the attribute messages.",
            "any float");
  INLET_DOC(1, "lock",
            "A pitch in Hz to correct toward instead of the scale, a signal or a number; 0 "
            "follows the scale.",
            "0 or a frequency in Hz");
  INLET_DOC(2, "detune", "A detune in cents added to the correction, a signal or a number.",
            "cents");
  OUTLET_DOC(0, "signal", "The corrected signal, the latency in samples late.", "any float");
  OUTLET_DOC(1, "pitch",
             "With pitchdetection 1, the detected pitch in Hz per sample (0 when there is "
             "none); else zeros.",
             "0 or a frequency in Hz");
  OUTLET_DOC(2, "latency",
             "The latency in samples: when reportlatency is turned on, whenever it changes while "
             "it is on, and on getlatency.",
             "0 or more");
  PARAM_DOC("settings", "440",
            "The standard pitch (the A in Hz, default 440), then any of 'correction_amount "
            "<0-100>' (100), 'correction_threshold <cents>' (0), "
            "'correction_ambience_threshold <0-1>' (0.3), 'correction_bypass <percent>' (100), "
            "'notelist <cents or a/b>...' (chromatic), 'notebase <0-11>' (0), 'enablednotes "
            "all|<degree>...' (all), 'quality <basic|good|better|best>' (basic), 'retune <0|1>' "
            "(1), 'pitchdetection <0|1>' (0), 'windowsize <samples>' (256), 'reportlatency "
            "<0|1>' (0) and 'use_16bit <0|1>' (ignored).",
            "a number, then words and values");
}

bool dRetune::BeginEdit() {
  if (writing.test_and_set(std::memory_order_acquire)) {
    dropped.fetch_add(1, std::memory_order_relaxed);
    return false;
  }
  return true;
}

void dRetune::Publish() {
  slots[back] = edited;
  // Publish the slot and take the one CALC() left in the middle, read or not.
  back = middle.exchange(back | FRESH, std::memory_order_acq_rel) & ~FRESH;
  writing.clear(std::memory_order_release);
}

void dRetune::Defaults(Flt standardPitch) {
  level = 0.f;
  lockHz = 0.f;
  detuneCents = 0.f;
  amount = 100.f;
  threshold = 0.f;
  ambience = DSP::retune::DEFAULT_AMBIENCE;
  bypass = 100.f;
  quality = pitchShifter::BASIC;
  windowSize = 0;
  retuning = true;
  detection = false;
  reportLatency = false;
  reportOnce = false;
  reported = -1;
  if (!BeginEdit()) return;
  edited = tuning();
  edited.standardPitch(standardPitch);
  Publish();
}

bool dRetune::ApplyWord(std::string_view word, const std::string_view* values, std::size_t count) {
  // The notes: edited under the senders' flag and published.
  if (word == "notelist" || word == "enablednotes" || word == "notebase" ||
      word == "defaulttuning" || word == "flush" || word == "list") {
    float number = 0.f;
    if (word == "notebase" && (count == 0 || !Number(values[0], number))) return false;
    if (!BeginEdit()) return false;
    if (word == "notelist") {
      std::array<Flt, tuning::MAX_NOTES> cents{};
      UInt n = 0;
      for (std::size_t i = 0; i < count && n < cents.size(); ++i) {
        float c = 0.f;
        if (Pitch(values[i], c)) cents[n++] = c;
      }
      edited.notes(cents.data(), n);
    } else if (word == "enablednotes") {
      if (count == 1 && values[0] == "all") {
        edited.enableAll();
      } else {
        U64 mask = 0;
        for (std::size_t i = 0; i < count; ++i) {
          float d = 0.f;
          if (!Number(values[i], d)) continue;
          const long degree = std::lround(d);
          if (degree >= 0 && degree < static_cast<long>(tuning::MAX_NOTES))
            mask |= U64(1) << static_cast<unsigned int>(degree);
        }
        edited.enabled(mask);
      }
    } else if (word == "notebase") {
      edited.base(static_cast<Int>(std::lround(number)));
    } else if (word == "defaulttuning") {
      edited.chromatic();
    } else if (word == "flush") {
      edited.enableAll();
    } else {
      // Note and velocity pairs; a note without one is enabled.
      for (std::size_t i = 0; i < count; i += 2) {
        float note = 0.f;
        float velocity = 1.f;
        if (!Number(values[i], note)) continue;
        if (i + 1 < count && !Number(values[i + 1], velocity)) velocity = 1.f;
        edited.midiNote(static_cast<Int>(std::lround(note)),
                        static_cast<Int>(std::lround(velocity)));
      }
    }
    Publish();
    return true;
  }

  if (word == "getlatency" && count == 0) {
    reportOnce = true;
    return true;
  }
  if (count == 0) return false;
  if (word == "quality") {
    int q = 0;
    if (!Quality(values[0], q)) return false;
    quality = q;
    return true;
  }
  float number = 0.f;
  if (!Number(values[0], number)) return false;
  if (word == "correction_amount") {
    amount = number;
  } else if (word == "correction_threshold") {
    threshold = number;
  } else if (word == "correction_ambience_threshold") {
    ambience = number;
  } else if (word == "correction_bypass") {
    bypass = number;
  } else if (word == "retune") {
    retuning = number != 0.f;
  } else if (word == "pitchdetection") {
    detection = number != 0.f;
  } else if (word == "windowsize") {
    windowSize = number > 0.f ? static_cast<UInt>(std::lround(number)) : 0u;
  } else if (word == "reportlatency" || word == "getlatency") {
    reportLatency = number != 0.f;
    if (number != 0.f) reportOnce = true;
  } else if (word == "use_16bit") {
    // Ztx's 16-bit processing: YSE has no such path; accepted for patches
    // that set it.
  } else {
    return false;
  }
  return true;
}

PARM_CLEAR() {
  settings.clear();
  Defaults(440.f);
}

// Control thread: the settings are split once into views of their tokens.
PARM_PARSE() {
  std::size_t at = 0;
  float standard = 440.f;
  if (!settings.empty() && Number(settings[0], standard)) {
    if (!(standard > 0.f)) standard = 440.f;
    at = 1;
  }
  Defaults(standard);
  std::vector<std::string_view> tokens(settings.begin(), settings.end());
  while (at < tokens.size()) {
    if (!IsWord(tokens[at])) {
      at++;
      continue;
    }
    std::size_t end = at + 1;
    while (end < tokens.size() && !IsWord(tokens[end]))
      end++;
    ApplyWord(tokens[at], tokens.data() + at + 1, end - at - 1);
    at = end;
  }
}

// A number into inlet 0 is its level while it is unconnected; into inlet 1
// the lock pitch, into inlet 2 the detune.
FLOAT_IN(SetFloat) {
  (void)thread;
  if (inlet == 0) {
    level = value;
  } else if (inlet == 1) {
    lockHz = value;
  } else if (inlet == 2) {
    detuneCents = value;
  }
}

INT_IN(SetInt) {
  SetFloat(static_cast<float>(value), inlet, thread);
}

// A list of numbers: MIDI notes and their velocities.
LIST_IN(SetList) {
  (void)inlet;
  (void)thread;
  std::array<std::string_view, MAX_TOKENS> tokens;
  const std::size_t count = Tokens(value, tokens.data(), tokens.size());
  ApplyWord("list", tokens.data(), count);
}

MESSAGES() {
  (void)value;
  std::array<std::string_view, MAX_TOKENS> tokens;
  const std::size_t count = Tokens(message, tokens.data(), tokens.size());
  if (count == 0) return;
  ApplyWord(tokens[0], tokens.data() + 1, count - 1);
}

CALC() {
  if ((middle.load(std::memory_order_relaxed) & FRESH) != 0u) {
    front = middle.exchange(front, std::memory_order_acq_rel) & ~FRESH;
    fn.notes(slots[front]);
  }
  fn.amount(amount.load())
      .threshold(threshold.load())
      .ambience(ambience.load())
      .bypass(bypass.load())
      .period(windowSize.load())
      .quality(static_cast<pitchShifter::QUALITY>(quality.load()))
      .enabled(retuning.load())
      .pitchDetection(detection.load());
  // Unconnected inlets (#1036) read their numbers.
  fn(inputs[0].Signal(level.load()), inputs[1].Signal(lockHz.load()),
     inputs[2].Signal(detuneCents.load()));

  // Right outlet first, as Max fires outlets right to left. The latency is
  // armed, sent after the traversal (#1037).
  const int latency = static_cast<int>(fn.latency());
  const bool once = reportOnce.exchange(false);
  if (once || (reportLatency.load() && latency != reported)) {
    outputs[2].ArmInt(latency);
    reported = latency;
  }
  outputs[1].SendBuffer(&fn.pitch(), thread);
  outputs[0].SendBuffer(&fn.output(), thread);
}

#undef className
