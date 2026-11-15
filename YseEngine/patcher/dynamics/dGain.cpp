#include "dGain.h"
#include "../analysis/meterCommon.h"

using namespace YSE::PATCHER;
using namespace YSE::PATCHER::meterCommon;
using YSE::DSP::gainFader;

namespace {
  constexpr unsigned int CELLS = 2; // position, size
} // namespace

#define className dGain

CONSTRUCT_DSP() {
  interp = gainFader::DEFAULT_TIME;
  size = static_cast<float>(gainFader::DEFAULT_SIZE);
  scale = gainFader::DEFAULT_SCALE;
  inc = gainFader::DEFAULT_INCREMENT;

  // in 0: the signal to scale; numbers, lists, bang and the word messages
  ADD_IN_0;
  REG_SIGNAL_IN;
  REG_FLOAT_IN(SetPosition);
  REG_INT_IN(SetPositionInt);
  REG_BANG_IN(SendPosition);
  REG_LIST_IN(SetList);

  // in 1: the interpolation time in ms
  ADD_IN_1;
  REG_FLOAT_IN(SetTime);
  REG_INT_IN(SetTimeInt);

  // out 0: the signal times the ramped gain
  ADD_OUT_BUFFER;
  // out 1: the position
  ADD_OUT_INT;

  ADD_PARAM(interp);
  ADD_PARAM(size);
  ADD_PARAM(scale);
  ADD_PARAM(inc);

  ADD_DESCRIPTION("A mixer fader: scales a signal by the gain of a slider position on a log "
                  "taper, ramping to each new gain over the interpolation time. Position 128 of "
                  "158 is unity gain. The host draws the slider.");
  ADD_CATEGORY(pCategory::DYNAMICS);
  INLET_DOC(0, "signal",
            "The signal to scale; unconnected it reads as silence. A number sets the slider "
            "position (its whole part, 0 to size - 1) and sends it out; a bang sends it; a list "
            "'<position> [<size>]' is the GUI value back; 'set <n>' sets the position without "
            "sending it, and 'set 0 <n>' / 'set 1 <size>' write one GUI cell. Also takes "
            "'size <n>', 'scale <x>', 'inc <x>' and 'interp <ms>'.",
            "0 to size - 1");
  INLET_DOC(1, "interp",
            "The interpolation time in milliseconds: how long the gain takes to reach a new "
            "position. 0 jumps.",
            "0 or more ms");
  OUTLET_DOC(0, "out", "The signal times the gain, ramped to each new position.", "any float");
  OUTLET_DOC(1, "position",
             "The slider position, sent when a number, a list or a bang arrives, not on 'set'.",
             "0 to size - 1");
  PARAM_DOC("interp", "10",
            "The interpolation time in milliseconds ('~gain 50'); the right inlet and "
            "'interp <ms>' set it too. 0 jumps to each new gain.",
            "0 or more ms");
  PARAM_DOC("size", "158",
            "The number of slider steps ('~gain 10 128'): the positions are 0 to size - 1, and "
            "the gain at a position p above 0 is scale * inc ^ (p - size).",
            "2 or more");
  PARAM_DOC("scale", "7.94321",
            "The gain one step below the top of the slider times inc ('~gain 10 158 1'). The "
            "default makes position 128 unity gain.",
            "any float");
  PARAM_DOC("inc", "1.071519",
            "The factor between the gains of neighbouring positions ('~gain 10 158 7.94321 "
            "1.122'). The default is 0.6 dB a step.",
            "greater than 0");
}

int dGain::Held() const {
  // A live SetParams can shrink the size under a stored position.
  return gainFader::Position(static_cast<float>(position.load()), gainFader::Size(size.load()));
}

void dGain::Move(float to) {
  if (std::isnan(to)) return;
  position = gainFader::Position(to, gainFader::Size(size.load()));
}

void dGain::Resize(float steps) {
  if (std::isnan(steps)) return;
  size = steps;
  position = gainFader::Position(static_cast<float>(position.load()), gainFader::Size(steps));
}

FLOAT_IN(SetPosition) {
  (void)inlet;
  if (std::isnan(value)) return;
  Move(value);
  outputs[1].SendInt(Held(), thread);
}

INT_IN(SetPositionInt) {
  (void)inlet;
  Move(static_cast<float>(value));
  outputs[1].SendInt(Held(), thread);
}

BANG_IN(SendPosition) {
  (void)inlet;
  outputs[1].SendInt(Held(), thread);
}

// '<position> [<size>]', the GUI value: the size first, so the position is
// held to it, then the position as a number. Walked in place so nothing
// allocates on whichever thread delivers it.
LIST_IN(SetList) {
  (void)inlet;
  std::size_t next = 0;
  const std::string_view first = Token(value, 0, next);
  float to = 0.f;
  if (!ReadNumericToken(first.data(), first.size(), to) || std::isnan(to)) return;
  float steps = 0.f;
  if (Number(value, next, steps)) Resize(steps);
  Move(to);
  outputs[1].SendInt(Held(), thread);
}

FLOAT_IN(SetTime) {
  (void)inlet;
  (void)thread;
  if (!std::isnan(value)) interp = value;
}

INT_IN(SetTimeInt) {
  (void)inlet;
  (void)thread;
  interp = static_cast<float>(value);
}

MESSAGES() {
  (void)value;
  // A missing or malformed number is ignored.
  std::size_t next = 0;
  const std::string_view word = Token(message, 0, next);
  float number = 0.f;
  if (!Number(message, next, number)) return;

  if (word == "set") {
    // 'set <n>' is the position, unsent. Two numbers are a GUI cell write
    // (issue #551): 'set 0 <position>' or 'set 1 <size>'.
    std::size_t after = 0;
    Token(message, next, after);
    float cell = 0.f;
    if (!Number(message, after, cell)) {
      std::size_t rest = 0;
      if (!Token(message, after, rest).empty()) return; // a malformed second token
      Move(number);
    } else if (number == 0.f) {
      Move(cell);
    } else if (number == 1.f) {
      Resize(cell);
    }
  } else if (word == "size") {
    Resize(number);
  } else if (word == "scale") {
    scale = number;
  } else if (word == "inc") {
    inc = number;
  } else if (word == "interp") {
    interp = number;
  }
}

CALC() {
  // The settings first, so the position is held to the size in use.
  fn.size(size.load())
      .scale(scale.load())
      .increment(inc.load())
      .time(interp.load())
      .position(static_cast<Flt>(Held()));
  // Unconnected, the inlet reads as silence (#1036).
  outputs[0].SendBuffer(&fn(inputs[0].Signal(0.f)), thread);
}

// Host thread: the two cells.
GUI_VALUE() {
  return GetGuiValueAt(0) + ' ' + GetGuiValueAt(1);
}

GUI_VALUE_COUNT() {
  return CELLS;
}

GUI_VALUE_AT() {
  switch (index) {
  case 0:
    return std::to_string(Held());
  case 1:
    return std::to_string(gainFader::Size(size.load()));
  default:
    return std::string();
  }
}

#undef className
