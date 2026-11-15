#include "dGridmeter.h"
#include "meterCommon.h"

using namespace YSE::PATCHER;
using namespace YSE::PATCHER::meterCommon;
using YSE::DSP::levelMeter;

namespace {
  constexpr float DEFAULT_RANGE = 60.f;
  constexpr float DEFAULT_TIME = 300.f;
  constexpr float DEFAULT_INTERVAL = 20.f;

  // A range is a positive number of dB.
  float Range(float dB) {
    return dB > 0.f ? dB : DEFAULT_RANGE;
  }

  // A cell number from 1 as an index, or -1 when it names no cell.
  int CellIndex(float number, unsigned int cells) {
    if (!(number >= 1.f) || number >= static_cast<float>(cells) + 1.f) return -1;
    return static_cast<int>(number) - 1;
  }
} // namespace

#define className dGridmeter

CONSTRUCT_DSP() {
  REG_PARM_CLEAR;
  REG_PARM_PARSE;
  ADD_PARAM(arguments);
  for (auto& db : shownDb)
    db.store(levelMeter::SILENCE, std::memory_order_relaxed);
  // '0 ' per cell; nothing grows on the thread a mute arrives on.
  scratch.reserve(2 * MAX_CELLS);

  ShapePorts();

  // out 0: the mute states after every change
  ADD_OUT_LIST;

  ADD_DESCRIPTION("A grid of level meters: measures the peak level of one or more signals with "
                  "attack and release ballistics and keeps each as the brightness of a cell, "
                  "with a mute state per cell, for the host to draw.");
  ADD_CATEGORY(pCategory::ANALYSIS);
  OUTLET_DOC(0, "mutes",
             "The mute state of every cell, 1 muted and 0 active, sent after the block that "
             "follows a mute, muteall, solo or list.",
             "a list of 0 or 1");
  PARAM_DOC("gridmeter", "1 60 300 300 20",
            "The number of cells, one signal inlet each (1-64), the range in dB that the "
            "brightness covers below 0 dB, the attack and the release in milliseconds, and the "
            "update interval in milliseconds (10-2000): '~gridmeter 8 48'. Later messages do "
            "not change what is saved.",
            "a cell count, a range in dB, then times in ms");
}

void dGridmeter::ShapePorts() {
  // The cell count, the range, the attack, the release, the interval.
  float values[5] = {1.f, DEFAULT_RANGE, DEFAULT_TIME, DEFAULT_TIME, DEFAULT_INTERVAL};
  int numbers = 0;
  for (const std::string& token : arguments) {
    float number = 0.f;
    if (numbers >= 5) break;
    if (token.empty() || !ReadNumericToken(token, number) || std::isnan(number)) continue;
    values[numbers++] = number;
  }
  const auto most = static_cast<float>(MAX_CELLS);
  const unsigned int count = values[0] >= most ? MAX_CELLS
                             : values[0] < 1.f ? 1u
                                               : static_cast<unsigned int>(values[0]);
  cells = count;
  range = Range(values[1]);
  attack = values[2];
  release = values[3];
  interval = values[4];

  // Rebuilt rather than resized, as ~loudness: the inlets follow the
  // arguments.
  inputs.clear();
  for (unsigned int i = 0; i < count; i++) {
    if (i == 0) {
      // in 0: 'mute', 'muteall', 'solo', a list of mute states, 'range',
      // 'attack', 'release', 'interval'
      ADD_IN_0;
      REG_SIGNAL_IN;
      REG_LIST_IN(SetMutes);
      INLET_DOC(0, count == 1 ? "signal" : "signal 1",
                "The signal of the first cell; unconnected it reads as silence. Also takes "
                "'mute <cell> <0|1>', 'muteall <0|1>', 'solo <cell>' (every other cell muted, "
                "0 unmutes all), a list of mute states from cell 1, 'range <dB>', 'attack "
                "<ms>', 'release <ms>' and 'interval <ms>'. Cells count from 1.",
                "any float");
    } else {
      inputs.emplace_back(this, false, i);
      REG_SIGNAL_IN;
      const std::string n = std::to_string(i + 1);
      inputs.back().SetDoc("signal " + n,
                           "The signal of cell " + n + "; unconnected it reads as silence.",
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

// After the block's traversal, on the audio thread.
void dGridmeter::FlushArmedMessages(YSE::THREAD thread) {
  if (!mutesPending) return;
  mutesPending = false;
  scratch.clear();
  const unsigned int count = cells.load();
  for (unsigned int i = 0; i < count; ++i) {
    if (i > 0) scratch.push_back(' ');
    scratch.push_back(muted[i].load() ? '1' : '0');
  }
  outputs[0].SendList(scratch, thread);
}

LIST_IN(SetMutes) {
  (void)inlet;
  // Mute states from cell 1; cells past the list keep theirs.
  float states[MAX_CELLS];
  const int n = ExprParseFloatList(value.c_str(), states, static_cast<int>(MAX_CELLS));
  if (n <= 0) return;
  const unsigned int count = cells.load();
  for (unsigned int i = 0; i < count && i < static_cast<unsigned int>(n); ++i)
    muted[i] = states[i] != 0.f;
  mutesChanged = true;
  (void)thread;
}

MESSAGES() {
  (void)value;
  std::size_t next = 0;
  const std::string_view word = Token(message, 0, next);
  const unsigned int count = cells.load();
  float number = 0.f;
  if (!Number(message, next, number)) return;
  if (word == "mute") {
    const int cell = CellIndex(number, count);
    Token(message, next, next); // past the cell number
    float state = 0.f;
    if (cell < 0 || !Number(message, next, state)) return;
    muted[static_cast<std::size_t>(cell)] = state != 0.f;
  } else if (word == "muteall") {
    for (unsigned int i = 0; i < count; ++i)
      muted[i] = number != 0.f;
  } else if (word == "solo") {
    const int cell = number == 0.f ? -1 : CellIndex(number, count);
    if (cell < 0 && number != 0.f) return;
    for (unsigned int i = 0; i < count; ++i)
      muted[i] = cell >= 0 && static_cast<int>(i) != cell;
  } else {
    if (word == "range") {
      range = Range(number);
    } else if (word == "attack") {
      attack = number;
    } else if (word == "release") {
      release = number;
    } else if (word == "interval") {
      interval = number;
    }
    return;
  }
  mutesChanged = true;
}

CALC() {
  (void)thread;
  const float a = attack.load();
  const float r = release.load();
  const float ms = Interval(interval.load());
  const std::size_t count = inputs.size();
  for (std::size_t i = 0; i < count; ++i) {
    levelMeter& m = fn[i];
    m.interval(ms).attack(a).release(r);
    // Unconnected inlets read as silence (#1036).
    m(inputs[i].Signal(0.f));
    shownDb[i].store(m.levelDb(), std::memory_order_relaxed);
  }
  // Never Send* from CALC(): the list goes out after the traversal.
  if (mutesChanged.exchange(false)) {
    mutesPending = true;
    MarkOutletArmed();
  }
}

// Host thread: every brightness, then every mute state, from the atomics.
std::string dGridmeter::GetGuiValue() {
  const unsigned int count = GetGuiValueCount();
  std::string out;
  for (unsigned int i = 0; i < count; ++i) {
    if (i > 0) out.push_back(' ');
    out += GetGuiValueAt(i);
  }
  return out;
}

unsigned int dGridmeter::GetGuiValueCount() const {
  return 2 * cells.load();
}

std::string dGridmeter::GetGuiValueAt(unsigned int index) {
  const unsigned int count = cells.load();
  if (index < count) {
    const float dB = shownDb[index].load(std::memory_order_relaxed);
    const float r = range.load();
    return Cell(levelMeter::position(dB, -r, 0.f));
  }
  if (index < 2 * count) return muted[index - count].load() ? "1" : "0";
  return std::string();
}

#undef className
