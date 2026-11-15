#include "dNumber.h"
#include "meterCommon.h"
#include "../../headers/constants.hpp"

using namespace YSE::PATCHER;
using namespace YSE::PATCHER::meterCommon;

namespace {
  constexpr unsigned int CELLS = 3; // value, mode, allow

  // Max's display modes, which are also the bits of `allow`.
  constexpr int OUTPUT = 1;
  constexpr int MONITOR = 2;
  constexpr int BOTH = OUTPUT | MONITOR;

  int AllowOf(int allow) {
    return allow >= OUTPUT && allow <= BOTH ? allow : BOTH;
  }
} // namespace

#define className dNumber

CONSTRUCT_DSP() {
  ramp = 0.f;
  interval = 100.f;
  mode = MONITOR;
  allow = BOTH;

  // in 0: the signal to monitor; numbers, lists, bang and the word messages
  ADD_IN_0;
  REG_SIGNAL_IN;
  REG_BANG_IN(Output);
  REG_FLOAT_IN(SetValue);
  REG_INT_IN(SetValueInt);
  REG_LIST_IN(SetList);

  // in 1: the ramp time in ms
  ADD_IN_1;
  REG_FLOAT_IN(SetRamp);
  REG_INT_IN(SetRampInt);

  // out 0: the constant signal, ramped
  ADD_OUT_BUFFER;
  // out 1: the monitored input every interval, sent after the block (#1037)
  ADD_OUT_FLOAT;

  ADD_PARAM(ramp);
  ADD_PARAM(interval);
  ADD_PARAM(mode);
  ADD_PARAM(allow);

  ADD_DESCRIPTION("A signal monitor and constant generator: shows the value of a signal and "
                  "sends it out every update interval, and outputs the last number it was given "
                  "as a constant signal, ramping to each new number over the ramp time. The host "
                  "draws the number box.");
  ADD_CATEGORY(pCategory::ANALYSIS);
  INLET_DOC(0, "signal",
            "The signal to monitor; unconnected it reads as silence. A number is clamped to the "
            "minimum and maximum, shown, and becomes the output; a list '<value> <ramp ms>' sets "
            "the ramp time first; 'set <n>' shows a number without outputting it; a bang in "
            "output mode outputs the shown number. Also takes 'mode <1|2>' (1 output, 2 "
            "monitor), 'allow <1-3>', 'monitormode <0|1>', 'sigoutmode <0|1>', 'min [n]' and "
            "'max [n]' (no number removes the limit), 'interval <ms>' and 'ft1 <ms>'.",
            "any float");
  INLET_DOC(1, "ramp",
            "The ramp time in milliseconds: how long the output takes to reach a new number. 0 "
            "jumps.",
            "0 or more ms");
  OUTLET_DOC(0, "out",
             "The last number as a constant signal, ramped linearly to each new one. It ignores "
             "the monitored signal and the mode.",
             "any float");
  OUTLET_DOC(1, "monitor",
             "The monitored signal, one sample every update interval, in either mode.",
             "any float");
  PARAM_DOC("ramp", "0",
            "The ramp time in milliseconds ('~number 50'); the right inlet and 'ft1 <ms>' set "
            "it too. 0 jumps to each new number.",
            "0 or more ms");
  PARAM_DOC("interval", "100",
            "The update interval in milliseconds: how often the monitored signal is shown and "
            "sent ('~number 0 250'). 10 to 2000.",
            "10-2000 ms");
  PARAM_DOC("mode", "2",
            "The display mode: 1 shows the output number, 2 the monitored signal ('~number 0 "
            "100 1'). A mode allow excludes gives way to the one it leaves.",
            "1-2");
  PARAM_DOC("allow", "3",
            "Which display modes there are: 1 output only, 2 monitor only, 3 both ('~number 0 "
            "100 1 1').",
            "1-3");
}

int dNumber::Mode() const {
  const int modes = AllowOf(allow.load());
  const int wanted = mode.load() == OUTPUT ? OUTPUT : MONITOR;
  return (modes & wanted) != 0 ? wanted : modes;
}

void dNumber::Take(float number, bool showOnly) {
  if (std::isnan(number)) return;
  if (hasMinimum.load() && number < minimum.load()) number = minimum.load();
  if (hasMaximum.load() && number > maximum.load()) number = maximum.load();
  shownNumber = number;
  if (!showOnly) value = number;
}

// Max: "In output mode, outputs the current signal value": the shown number
// becomes the output, so a number shown with 'set' goes out on a bang.
BANG_IN(Output) {
  (void)inlet;
  (void)thread;
  if (Mode() == OUTPUT) value = shownNumber.load();
}

FLOAT_IN(SetValue) {
  (void)inlet;
  (void)thread;
  Take(value, false);
}

INT_IN(SetValueInt) {
  (void)inlet;
  (void)thread;
  Take(static_cast<float>(value), false);
}

// '<value> <ramp ms>': the ramp time first, so this change takes it. Walked
// in place so nothing allocates on whichever thread delivers it.
LIST_IN(SetList) {
  (void)inlet;
  (void)thread;
  std::size_t next = 0;
  const std::string_view first = Token(value, 0, next);
  float number = 0.f;
  if (!ReadNumericToken(first.data(), first.size(), number)) return;
  float time = 0.f;
  if (Number(value, next, time)) ramp = time;
  Take(number, false);
}

FLOAT_IN(SetRamp) {
  (void)inlet;
  (void)thread;
  if (!std::isnan(value)) ramp = value;
}

INT_IN(SetRampInt) {
  (void)inlet;
  (void)thread;
  ramp = static_cast<float>(value);
}

MESSAGES() {
  (void)value;
  // A missing or malformed number is ignored, except after 'min' and 'max',
  // where no number at all removes the limit.
  std::size_t next = 0;
  const std::string_view word = Token(message, 0, next);
  float number = 0.f;
  const bool has = Number(message, next, number);

  if (word == "min" || word == "max") {
    std::size_t after = 0;
    const bool none = Token(message, next, after).empty();
    if (!has && !none) return;
    if (word == "min") {
      if (has) minimum = number;
      hasMinimum = has;
    } else {
      if (has) maximum = number;
      hasMaximum = has;
    }
    return;
  }
  if (!has) return;

  if (word == "set") {
    Take(number, true);
  } else if (word == "mode") {
    const int wanted = number == 1.f ? OUTPUT : (number == 2.f ? MONITOR : 0);
    if ((AllowOf(allow.load()) & wanted) != 0) mode = wanted;
  } else if (word == "allow") {
    if (number == 1.f || number == 2.f || number == 3.f) allow = static_cast<int>(number);
  } else if (word == "monitormode" || word == "sigoutmode") {
    // The two halves of allow; a change that would leave no mode is ignored.
    const int bit = word == "monitormode" ? MONITOR : OUTPUT;
    const int modes = AllowOf(allow.load());
    const int changed = number != 0.f ? (modes | bit) : (modes & ~bit);
    if (changed != 0) allow = changed;
  } else if (word == "interval") {
    interval = number;
  } else if (word == "ft1") {
    ramp = number;
  }
}

CALC() {
  // The output: the last number, ramped over the ramp time at the live
  // sample rate. It ignores the input and the mode.
  fn.ramp(static_cast<Flt>(static_cast<double>(ramp.load()) * YSE::SAMPLERATE / 1000.0));
  target = value.load();
  outputs[0].SendBuffer(&fn(target), thread);

  // The monitor. Unconnected, the inlet reads as silence (#1036).
  DSP::buffer& in = inputs[0].Signal(0.f);
  const auto length = static_cast<double>(in.getLength());
  if (length <= 0.0) return;
  // The period follows the live sample rate, so it is worked out every block.
  const double period = static_cast<double>(Interval(interval.load())) * YSE::SAMPLERATE / 1000.0;
  // samplesLeft counts from the start of this block to the next report.
  if (samplesLeft < length) {
    // Several reports due in one block are one: the last of them.
    const double due = std::ceil((length - samplesLeft) / period);
    const double at = samplesLeft + (due - 1.0) * period;
    const float sample = in.getPtr()[static_cast<std::size_t>(at)];
    shownInput = sample;
    // Never Send* from CALC(): the patcher sends this after the traversal.
    outputs[1].ArmFloat(sample);
    samplesLeft += due * period;
  }
  samplesLeft -= length;
}

// Host thread: the three cells.
std::string dNumber::GetGuiValue() {
  return GetGuiValueAt(0) + ' ' + GetGuiValueAt(1) + ' ' + GetGuiValueAt(2);
}

unsigned int dNumber::GetGuiValueCount() const {
  return CELLS;
}

std::string dNumber::GetGuiValueAt(unsigned int index) {
  switch (index) {
  case 0:
    return Cell(Mode() == OUTPUT ? shownNumber.load() : shownInput.load());
  case 1:
    return Mode() == OUTPUT ? "1" : "2";
  case 2:
    return std::to_string(AllowOf(allow.load()));
  default:
    return std::string();
  }
}

#undef className
