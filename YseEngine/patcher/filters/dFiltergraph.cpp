#include "dFiltergraph.h"
#include "../math/gExprEval.h"
#include "../pListArgs.h"
#include "../pObjectList.hpp"
#include "../pSelector.h"

#include <cmath>
#include <string_view>

using namespace YSE::PATCHER;
using YSE::DSP::filterBands;

namespace {
  // The longest list the object sends: five coefficients per band, each at
  // most kExprValueTextMax characters with its separator.
  constexpr std::size_t SCRATCH_CAPACITY =
      filterBands::MAX_BANDS * 5 * (YSE::PATCHER::kExprValueTextMax + 1);

  // The most numbers a message is read for: a whole cascade.
  constexpr unsigned int MAX_NUMBERS = filterBands::MAX_BANDS * 5;

  // The token at or after `from`, walked in place so nothing allocates on
  // whichever thread delivers the message. `next` is where it ends; empty
  // when there is none.
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

  // The numbers from `from` on, at most `capacity` stored. -1 when a token
  // is not a number: such a message is not read at all.
  int ReadNumbers(const std::string& text, std::size_t from, float* out, unsigned int capacity) {
    int count = 0;
    std::size_t next = from;
    std::string_view token = Token(text, next, next);
    while (!token.empty()) {
      float value = 0.f;
      if (!ReadNumber(token, value)) return -1;
      if (static_cast<unsigned int>(count) < capacity) out[count] = value;
      ++count;
      token = Token(text, next, next);
    }
    return count;
  }

  void AppendNumber(std::string& out, float value) {
    char text[kExprValueTextMax];
    const int written = ExprFormatValue(ExprValue::Float(value), text, kExprValueTextMax);
    if (written <= 0) return;
    if (!out.empty()) out.push_back(' ');
    out.append(text, static_cast<std::size_t>(written));
  }

  void AppendInt(std::string& out, int value) {
    char text[FORMAT_INT_WIDTH];
    const std::size_t written = WriteInt(value, text);
    if (!out.empty()) out.push_back(' ');
    out.append(text, written);
  }

  // The mode words, in the order of filterBands::MODE.
  constexpr std::string_view MODE_NAMES[filterBands::MODE_COUNT] = {
      "display",   "lowpass",  "highpass",  "bandpass", "bandstop",
      "peaknotch", "lowshelf", "highshelf", "resonant", "allpass"};

  int ToMode(std::string_view word) {
    for (int i = 0; i < filterBands::MODE_COUNT; ++i)
      if (word == MODE_NAMES[i]) return i;
    return -1;
  }

  // A whole number in [0, limit), or -1.
  int ToIndex(float value, unsigned int limit) {
    if (!std::isfinite(value) || value < 0.f) return -1;
    const long rounded = std::lround(value);
    return rounded < static_cast<long>(limit) ? static_cast<int>(rounded) : -1;
  }

  // The edit_* constraint attributes, in filterBands::constraint order.
  constexpr std::string_view LIMIT_NAMES[6] = {"edit_minfreq", "edit_maxfreq", "edit_minamp",
                                               "edit_maxamp",  "edit_minQ",    "edit_maxQ"};

  void SetLimit(filterBands& fn, unsigned int band, unsigned int which, float value) {
    float limits[6];
    for (unsigned int i = 0; i < 6; ++i)
      limits[i] = fn.constraint(band, i);
    limits[which] = value;
    fn.constraints(band, limits[0], limits[1], limits[2], limits[3], limits[4], limits[5]);
  }
} // namespace

#define className dFiltergraph

CONSTRUCT_DSP() {
  // in 0: messages, bang, a0
  ADD_IN_0;
  REG_BANG_IN(BangIn);
  REG_FLOAT_IN(FloatIn);
  REG_INT_IN(IntIn);
  REG_LIST_IN(ListIn);
  // in 1-4: a1 a2 b1 b2; in 5-7: frequency, gain, Q
  for (int i = 1; i < 8; ++i) {
    inputs.emplace_back(this, false, i);
    REG_FLOAT_IN(FloatIn);
    REG_INT_IN(IntIn);
    REG_LIST_IN(ListIn);
  }

  ADD_OUT_LIST; // 0: coefficients
  ADD_OUT_FLOAT; // 1: frequency
  ADD_OUT_FLOAT; // 2: gain
  ADD_OUT_FLOAT; // 3: Q
  ADD_OUT_FLOAT; // 4: bandwidth
  ADD_OUT_LIST; // 5: query reply
  ADD_OUT_INT; // 6: band index

  ADD_PARAM(settings);
  REG_PARM_CLEAR;
  REG_PARM_PARSE;

  scratch.reserve(SCRATCH_CAPACITY);

  ADD_DESCRIPTION("A filter editor: holds one to 24 biquad bands, each a filter type with a "
                  "frequency, a gain and a Q, or five coefficients, and sends them as the "
                  "coefficient list ~biquad and ~cascade take. A host draws the bands and the "
                  "response curve; the bands are saved with the patch.");
  ADD_CATEGORY(pCategory::FILTER);
  INLET_DOC(
      0, "in",
      "Bang: send the coefficients (and, for a filter mode, the edited band's index, "
      "bandwidth, Q, gain and frequency first). A number: a0 of the edited band in display "
      "mode. A list of numbers: a cascade, five coefficients per band. Mode words (display, "
      "lowpass, highpass, bandpass, bandstop, peaknotch, lowshelf, highshelf, resonant, "
      "allpass) with an optional frequency, gain and Q; mode, cascade, set, params, "
      "setparams, gainmode, analog, displaydot, options, setoptions, constraints, setconstraints, "
      "flat, setfilter, whichfilt, selectfilt, nfilters, query, autoout and the edit_* "
      "attributes.",
      "");
  INLET_DOC(1, "a1", "a1 of the edited band, in display mode; sends the coefficients.", "");
  INLET_DOC(2, "a2", "a2 of the edited band, in display mode; sends the coefficients.", "");
  INLET_DOC(3, "b1", "b1 of the edited band, in display mode; sends the coefficients.", "");
  INLET_DOC(4, "b2", "b2 of the edited band, in display mode; sends the coefficients.", "");
  INLET_DOC(5, "frequency",
            "The edited band's frequency in Hz; sends the coefficients. A list of three is the "
            "frequency, gain and Q; 'set <f> <g> <q>' sets them without sending.",
            "0 to samplerate/2 Hz");
  INLET_DOC(6, "gain", "The edited band's linear gain; sends the coefficients.", "");
  INLET_DOC(7, "q", "The edited band's Q, or a shelf's slope; sends the coefficients.",
            "0.01-10000");
  OUTLET_DOC(0, "coefficients",
             "The coefficients of every band, a0 a1 a2 b1 b2 per band, for ~biquad (one band) "
             "or ~cascade.",
             "a list of coefficients in sets of five");
  OUTLET_DOC(1, "frequency", "On a bang, the edited band's frequency in Hz.", "");
  OUTLET_DOC(2, "gain", "On a bang, the edited band's linear gain.", "");
  OUTLET_DOC(3, "q", "On a bang, the edited band's Q.", "");
  OUTLET_DOC(4, "bandwidth", "On a bang, the edited band's frequency over its Q, in Hz.", "");
  OUTLET_DOC(5, "query",
             "For 'query <hz>': the gain (linear) and phase (radians) of all bands together at "
             "that frequency.",
             "amplitude phase");
  OUTLET_DOC(6, "band", "On a bang, the index of the edited band.", "0-23");
  PARAM_DOC("settings", "",
            "'nfilters <n>', the number of bands (1-24, default 1), and 'autoout <0|1>', send "
            "the coefficients when the patch loads (default 0). The bands are saved with the "
            "patch.",
            "words and values");
}

// Control thread: a list parameter rebuilds the object, so these run on one
// no other thread has seen.
PARM_CLEAR() {
  settings.clear();
  fn = filterBands();
  edited = 0;
  highlighted = -1;
  autoOut = false;
}

PARM_PARSE() {
  std::string text;
  for (const std::string& token : settings) {
    text += token;
    text += ' ';
  }
  ApplySettings(text);
}

void dFiltergraph::ApplySettings(const std::string& text) {
  std::size_t next = 0;
  std::string_view word = Token(text, 0, next);
  while (!word.empty()) {
    std::size_t after = next;
    float number = 0.f;
    if (ReadNumber(Token(text, after, after), number)) {
      if (word == "nfilters") {
        fn.bands(number < 1.f ? 1u : static_cast<unsigned int>(std::lround(number)));
        next = after;
      } else if (word == "autoout") {
        autoOut = number != 0.f;
        next = after;
      }
    }
    word = Token(text, next, next);
  }
}

void dFiltergraph::Select(float index) {
  const int band = ToIndex(index, fn.bands());
  if (band >= 0) edited = static_cast<unsigned int>(band);
}

void dFiltergraph::SendCoefficients(YSE::THREAD thread) {
  float values[MAX_NUMBERS];
  const unsigned int n = fn.sections(values, MAX_NUMBERS);
  scratch.clear();
  for (unsigned int i = 0; i < n; ++i)
    AppendNumber(scratch, values[i]);
  outputs[0].SendList(scratch, thread);
}

void dFiltergraph::SendAll(YSE::THREAD thread) {
  if (fn.mode(edited) != filterBands::DISPLAY) {
    outputs[6].SendInt(static_cast<int>(edited), thread);
    outputs[4].SendFloat(fn.bandwidth(edited), thread);
    outputs[3].SendFloat(fn.q(edited), thread);
    outputs[2].SendFloat(fn.gain(edited), thread);
    outputs[1].SendFloat(fn.frequency(edited), thread);
  }
  SendCoefficients(thread);
}

void dFiltergraph::Handle(const std::string& text, YSE::THREAD thread) {
  std::size_t next = 0;
  const std::string_view word = Token(text, 0, next);
  if (word.empty()) return;
  float numbers[MAX_NUMBERS];

  // Numbers alone: one is a0, more a cascade.
  float first = 0.f;
  if (ReadNumber(word, first)) {
    const int n = ReadNumbers(text, 0, numbers, MAX_NUMBERS);
    if (n == 1) {
      if (fn.mode(edited) != filterBands::DISPLAY) return;
      fn.coefficient(edited, 0, first);
      SendCoefficients(thread);
    } else if (n > 1 && fn.cascade(numbers, static_cast<unsigned int>(n)) > 0) {
      if (edited >= fn.bands()) edited = 0;
      if (highlighted >= static_cast<int>(fn.bands())) highlighted = -1;
      SendCoefficients(thread);
    }
    return;
  }

  if (word == "bang") {
    SendAll(thread);
    return;
  }

  const int n = ReadNumbers(text, next, numbers, MAX_NUMBERS);
  if (n < 0) return;
  const unsigned int count = static_cast<unsigned int>(n);

  const int mode = ToMode(word);
  if (mode >= 0) {
    fn.mode(edited, static_cast<filterBands::MODE>(mode));
    if (count >= 1) fn.frequency(edited, numbers[0]);
    if (count >= 2) fn.gain(edited, numbers[1]);
    if (count >= 3) fn.q(edited, numbers[2]);
    SendCoefficients(thread);
    return;
  }

  // Words that send the coefficients, and their set... forms that do not.
  const bool send = word.substr(0, 3) != "set" && word.substr(0, 5) != "edit_";
  if (word == "mode" || word == "edit_mode") {
    const int m = count >= 1 ? ToIndex(numbers[0], filterBands::MODE_COUNT) : -1;
    if (m < 0) return;
    fn.mode(edited, static_cast<filterBands::MODE>(m));
  } else if (word == "cascade" || word == "set") {
    if (fn.cascade(numbers, count) == 0) return;
    if (edited >= fn.bands()) edited = 0;
    if (highlighted >= static_cast<int>(fn.bands())) highlighted = -1;
  } else if (word == "params" || word == "setparams") {
    if (count == 3) {
      fn.parameters(edited, numbers[0], numbers[1], numbers[2]);
    } else if (count == 4) {
      const int band = ToIndex(numbers[0], fn.bands());
      if (band < 0) return;
      fn.parameters(static_cast<unsigned int>(band), numbers[1], numbers[2], numbers[3]);
    } else {
      return;
    }
  } else if (word == "gainmode" || word == "edit_gainmode") {
    if (count < 1) return;
    fn.gainEnabled(edited, numbers[0] != 0.f);
  } else if (word == "analog" || word == "edit_analog") {
    if (count < 1) return;
    fn.analog(edited, numbers[0] != 0.f);
  } else if (word == "displaydot" || word == "edit_displaydot") {
    if (count < 1) return;
    fn.handle(edited, numbers[0] != 0.f);
    return;
  } else if (word == "options" || word == "setoptions") {
    // band, mode, gain enable, analog, dot
    if (count != 5) return;
    const int band = ToIndex(numbers[0], fn.bands());
    const int m = ToIndex(numbers[1], filterBands::MODE_COUNT);
    if (band < 0 || m < 0) return;
    const auto b = static_cast<unsigned int>(band);
    fn.mode(b, static_cast<filterBands::MODE>(m));
    fn.gainEnabled(b, numbers[2] != 0.f);
    fn.analog(b, numbers[3] != 0.f);
    fn.handle(b, numbers[4] != 0.f);
  } else if (word == "constraints" || word == "setconstraints") {
    if (count != 7) return;
    const int band = ToIndex(numbers[0], fn.bands());
    if (band < 0) return;
    fn.constraints(static_cast<unsigned int>(band), numbers[1], numbers[2], numbers[3], numbers[4],
                   numbers[5], numbers[6]);
  } else if (word == "flat") {
    bool any = false;
    for (unsigned int i = 0; i < count && i < MAX_NUMBERS; ++i) {
      const int band = ToIndex(numbers[i], fn.bands());
      if (band < 0) continue;
      fn.flat(static_cast<unsigned int>(band));
      any = true;
    }
    if (!any) return;
  } else if (word == "setfilter" || word == "whichfilt" || word == "edit_filter") {
    if (count >= 1) Select(numbers[0]);
    return;
  } else if (word == "selectfilt") {
    if (count < 1) return;
    if (numbers[0] < 0.f)
      highlighted = -1;
    else if (ToIndex(numbers[0], fn.bands()) >= 0)
      highlighted = ToIndex(numbers[0], fn.bands());
    return;
  } else if (word == "nfilters") {
    if (count < 1) return;
    fn.bands(numbers[0] < 1.f ? 1u : static_cast<unsigned int>(std::lround(numbers[0])));
    if (edited >= fn.bands()) edited = fn.bands() - 1;
    if (highlighted >= static_cast<int>(fn.bands())) highlighted = -1;
    return;
  } else if (word == "autoout") {
    if (count >= 1) autoOut = numbers[0] != 0.f;
    return;
  } else if (word == "query") {
    float amplitude = 0.f;
    float phase = 0.f;
    if (count < 1 || !fn.responseAt(numbers[0], amplitude, phase)) return;
    scratch.clear();
    AppendNumber(scratch, amplitude);
    AppendNumber(scratch, phase);
    outputs[5].SendList(scratch, thread);
    return;
  } else if (word == "edit_freq") {
    if (count < 1) return;
    fn.frequency(edited, numbers[0]);
  } else if (word == "edit_amp") {
    if (count < 1) return;
    fn.gain(edited, numbers[0]);
  } else if (word == "edit_Q" || word == "edit_q") {
    if (count < 1) return;
    fn.q(edited, numbers[0]);
  } else {
    unsigned int which = 0;
    while (which < 6 && word != LIMIT_NAMES[which])
      ++which;
    if (which == 6 || count < 1) return;
    SetLimit(fn, edited, which, numbers[0]);
  }
  if (send) SendCoefficients(thread);
}

void dFiltergraph::HandleFrequencyInlet(const std::string& text, YSE::THREAD thread) {
  std::size_t next = 0;
  const std::string_view word = Token(text, 0, next);
  const bool quiet = word == "set";
  float numbers[3];
  const int n = ReadNumbers(text, quiet ? next : 0, numbers, 3);
  if (n == 1 && !quiet) {
    fn.frequency(edited, numbers[0]);
  } else if (n == 3) {
    fn.parameters(edited, numbers[0], numbers[1], numbers[2]);
  } else {
    return;
  }
  if (!quiet) SendCoefficients(thread);
}

BANG_IN(BangIn) {
  (void)inlet;
  if (busy.exchange(true, std::memory_order_acquire)) {
    dropped.fetch_add(1, std::memory_order_relaxed);
    return;
  }
  SendAll(thread);
  busy.store(false, std::memory_order_release);
}

FLOAT_IN(FloatIn) {
  if (busy.exchange(true, std::memory_order_acquire)) {
    dropped.fetch_add(1, std::memory_order_relaxed);
    return;
  }
  // Held across the send: the list is written in `scratch`.
  bool changed = true;
  if (inlet <= 4) {
    // A coefficient counts in display mode only, as in Max.
    if (fn.mode(edited) == filterBands::DISPLAY)
      fn.coefficient(edited, static_cast<unsigned int>(inlet), value);
    else
      changed = false;
  } else if (inlet == 5) {
    fn.frequency(edited, value);
  } else if (inlet == 6) {
    fn.gain(edited, value);
  } else {
    fn.q(edited, value);
  }
  if (changed) SendCoefficients(thread);
  busy.store(false, std::memory_order_release);
}

INT_IN(IntIn) {
  FloatIn(static_cast<float>(value), inlet, thread);
}

LIST_IN(ListIn) {
  if (busy.exchange(true, std::memory_order_acquire)) {
    dropped.fetch_add(1, std::memory_order_relaxed);
    return;
  }
  if (inlet == 0) {
    Handle(value, thread);
  } else if (inlet == 5) {
    HandleFrequencyInlet(value, thread);
  } else {
    // A single number on another inlet is that number.
    float numbers[2];
    if (ReadNumbers(value, 0, numbers, 2) == 1) {
      busy.store(false, std::memory_order_release);
      FloatIn(numbers[0], inlet, thread);
      return;
    }
  }
  busy.store(false, std::memory_order_release);
}

void dFiltergraph::Loadbang(YSE::THREAD thread) {
  if (!autoOut) return;
  if (busy.exchange(true, std::memory_order_acquire)) {
    dropped.fetch_add(1, std::memory_order_relaxed);
    return;
  }
  SendCoefficients(thread);
  busy.store(false, std::memory_order_release);
}

// ─── the GUI value ───────────────────────────────────────────────────────────

std::string dFiltergraph::Cell(const filterBands& bands, unsigned int index,
                               unsigned int editedBand, int highlightedBand) {
  std::string out;
  if (index == 0) {
    AppendInt(out, static_cast<int>(bands.bands()));
    AppendInt(out, static_cast<int>(editedBand));
    AppendInt(out, highlightedBand);
    return out;
  }
  const unsigned int b = index - 1;
  const filterBands::coefficientSet c = bands.coefficients(b);
  AppendInt(out, static_cast<int>(bands.mode(b)));
  AppendNumber(out, bands.frequency(b));
  AppendNumber(out, bands.gain(b));
  AppendNumber(out, bands.q(b));
  AppendInt(out, bands.gainEnabled(b) ? 1 : 0);
  AppendInt(out, bands.handle(b) ? 1 : 0);
  AppendNumber(out, c.a0);
  AppendNumber(out, c.a1);
  AppendNumber(out, c.a2);
  AppendNumber(out, c.b1);
  AppendNumber(out, c.b2);
  AppendInt(out, bands.analog(b) ? 1 : 0);
  return out;
}

std::string dFiltergraph::GetGuiValue() {
  // Host thread: copy under the guard, format after releasing it.
  if (busy.exchange(true, std::memory_order_acquire)) return std::string();
  const filterBands copy = fn;
  const unsigned int e = edited;
  const int h = highlighted;
  busy.store(false, std::memory_order_release);

  std::string out = Cell(copy, 0, e, h);
  for (unsigned int i = 1; i <= copy.bands(); ++i) {
    out.push_back(' ');
    out += Cell(copy, i, e, h);
  }
  return out;
}

unsigned int dFiltergraph::GetGuiValueCount() const {
  if (busy.exchange(true, std::memory_order_acquire)) return 0;
  const unsigned int n = fn.bands() + 1;
  busy.store(false, std::memory_order_release);
  return n;
}

std::string dFiltergraph::GetGuiValueAt(unsigned int index) {
  if (busy.exchange(true, std::memory_order_acquire)) return std::string();
  const filterBands copy = fn;
  const unsigned int e = edited;
  const int h = highlighted;
  busy.store(false, std::memory_order_release);
  // Range-checked against the count read now.
  if (index > copy.bands()) return std::string();
  return Cell(copy, index, e, h);
}

// ─── persistence ─────────────────────────────────────────────────────────────

bool dFiltergraph::DumpState(nlohmann::json::value_type& json) {
  const saveGuard guard(busy);
  if (!guard.Held()) return false;
  json["bands"] = fn.bands();
  json["edited"] = edited;
  json["highlighted"] = highlighted;
  for (unsigned int b = 0; b < fn.bands(); ++b) {
    nlohmann::json band;
    band["mode"] = static_cast<int>(fn.mode(b));
    band["frequency"] = fn.frequency(b);
    band["gain"] = fn.gain(b);
    band["q"] = fn.q(b);
    band["gainmode"] = fn.gainEnabled(b);
    band["displaydot"] = fn.handle(b);
    band["analog"] = fn.analog(b);
    for (unsigned int i = 0; i < 6; ++i)
      band["constraints"].push_back(fn.constraint(b, i));
    // In display mode the coefficients are the band; otherwise they follow
    // from the settings and the sample rate.
    if (fn.mode(b) == filterBands::DISPLAY) {
      const filterBands::coefficientSet c = fn.coefficients(b);
      band["coefficients"] = {c.a0, c.a1, c.a2, c.b1, c.b2};
    }
    json["band"].push_back(band);
  }
  return true;
}

namespace {
  // A stored number, or `fallback` for anything else: a hand-edited file
  // must not throw on load.
  float Number(const nlohmann::json& value, float fallback) {
    return value.is_number() ? value.get<float>() : fallback;
  }

  float Field(const nlohmann::json& object, const char* key, float fallback) {
    const auto found = object.find(key);
    return found == object.end() ? fallback : Number(*found, fallback);
  }

  // A stored flag: saved as a boolean, read from a number too; absent or
  // anything else is off.
  bool Flag(const nlohmann::json& object, const char* key) {
    const auto found = object.find(key);
    if (found == object.end()) return false;
    if (found->is_boolean()) return found->get<bool>();
    return Number(*found, 0.f) != 0.f;
  }
} // namespace

void dFiltergraph::RestoreState(const nlohmann::json::value_type& json) {
  // ParseJSON, on a freshly built object no other thread has seen.
  const auto stored = json.find("band");
  if (stored == json.end() || !stored->is_array()) return;
  const float count = Field(json, "bands", static_cast<float>(stored->size()));
  fn.bands(count < 1.f ? 1u : static_cast<unsigned int>(count));
  unsigned int b = 0;
  for (const auto& band : *stored) {
    if (b >= filterBands::MAX_BANDS) break;
    if (!band.is_object()) {
      ++b;
      continue;
    }
    // Constraints first, so the settings are clamped into them as the inlet
    // would; the mode before the coefficients, since switching to display
    // keeps the coefficients the band had.
    const auto limits = band.find("constraints");
    if (limits != band.end() && limits->is_array() && limits->size() == 6)
      fn.constraints(b, Number((*limits)[0], 0.f), Number((*limits)[1], 0.f),
                     Number((*limits)[2], 0.f), Number((*limits)[3], 0.f),
                     Number((*limits)[4], 0.f), Number((*limits)[5], 0.f));
    fn.parameters(b, Field(band, "frequency", 1000.f), Field(band, "gain", 1.f),
                  Field(band, "q", filterBands::FLAT_Q));
    fn.gainEnabled(b, Flag(band, "gainmode"));
    fn.handle(b, Flag(band, "displaydot"));
    // Absent in patches saved before #1209: off.
    fn.analog(b, Flag(band, "analog"));
    const int m = static_cast<int>(Field(band, "mode", static_cast<float>(filterBands::LOWPASS)));
    if (m >= 0 && m < filterBands::MODE_COUNT) fn.mode(b, static_cast<filterBands::MODE>(m));
    const auto c = band.find("coefficients");
    if (c != band.end() && c->is_array() && c->size() == 5)
      fn.coefficients(b, filterBands::coefficientSet{Number((*c)[0], 1.f), Number((*c)[1], 0.f),
                                                     Number((*c)[2], 0.f), Number((*c)[3], 0.f),
                                                     Number((*c)[4], 0.f)});
    ++b;
  }
  const float e = Field(json, "edited", 0.f);
  edited = e >= 0.f && e < static_cast<float>(fn.bands()) ? static_cast<unsigned int>(e) : 0;
  const float h = Field(json, "highlighted", -1.f);
  highlighted = h >= 0.f && h < static_cast<float>(fn.bands()) ? static_cast<int>(h) : -1;
}

#undef className
