#include "dScope.h"
#include "meterCommon.h"

using namespace YSE::PATCHER;
using namespace YSE::PATCHER::meterCommon;
using YSE::DSP::scopeCapture;

namespace {
  constexpr unsigned int HEADER = 3; // mode, points, serial

  // Appends one cell of a trace. Host thread.
  void Append(std::string& out, float value) {
    char text[YSE::PATCHER::kExprValueTextMax];
    const int written = YSE::PATCHER::ExprFormatValue(YSE::PATCHER::ExprValue::Float(value), text,
                                                      YSE::PATCHER::kExprValueTextMax);
    if (written > 0)
      out.append(text, static_cast<std::size_t>(written));
    else
      out.append("0.");
  }

  // Cell `index` of `t`; "" past the end. Host thread.
  std::string TraceCell(const scopeCapture::trace& t, unsigned int index) {
    switch (index) {
    case 0:
      return t.mode == scopeCapture::XY ? "1" : "0";
    case 1:
      return std::to_string(t.size);
    case 2:
      return std::to_string(t.serial);
    default:
      break;
    }
    const unsigned int value = index - HEADER;
    if (value >= 2 * t.size) return std::string();
    const float* values = (value & 1u) != 0 ? t.second : t.first;
    return Cell(values[value >> 1]);
  }
} // namespace

#define className dScope

CONSTRUCT_DSP() {
  calccount = static_cast<float>(scopeCapture::DEFAULT_PERIOD);
  bufsize = static_cast<float>(scopeCapture::DEFAULT_SIZE);
  delay = 0.f;
  trigger = 0.f;
  triglevel = 0.f;

  // in 0: the signal or X; an int sets calccount; the word messages
  ADD_IN_0;
  REG_SIGNAL_IN;
  REG_FLOAT_IN(SetPeriod);
  REG_INT_IN(SetPeriodInt);

  // in 1: Y; an int sets bufsize
  ADD_IN_1;
  REG_SIGNAL_IN;
  REG_FLOAT_IN(SetSize);
  REG_INT_IN(SetSizeInt);

  ADD_PARAM(calccount);
  ADD_PARAM(bufsize);
  ADD_PARAM(delay);
  ADD_PARAM(trigger);
  ADD_PARAM(triglevel);

  ADD_DESCRIPTION("An oscilloscope: captures traces of a signal, or of an X/Y pair of signals, "
                  "with a number of samples per point and an optional trigger, for the host to "
                  "draw.");
  ADD_CATEGORY(pCategory::ANALYSIS);
  INLET_DOC(0, "signal",
            "The signal to show, or X when the right inlet has a cord; unconnected it reads as "
            "silence. An int (or the whole part of a float) sets calccount. Also takes "
            "'calccount <n>', 'bufsize <n>', 'delay <ms>', 'trigger <0|1|2>' and "
            "'triglevel <x>'.",
            "any float");
  INLET_DOC(1, "y",
            "Y: with a cord here the scope shows X against Y, without one a waveform. An int (or "
            "the whole part of a float) sets bufsize.",
            "any float");
  PARAM_DOC("calccount", "256",
            "The samples each point of a trace stands for ('~scope 64'). A waveform point is "
            "their minimum and maximum, an X/Y point the first of them.",
            "2-8192");
  PARAM_DOC("bufsize", "128",
            "The points of a trace ('~scope 64 256'). A trace covers calccount * bufsize "
            "samples.",
            "2-8192");
  PARAM_DOC("delay", "0",
            "The wait in milliseconds between the end of a trace and the trigger of the next "
            "('~scope 256 128 500').",
            "0 or more ms");
  PARAM_DOC("trigger", "0",
            "When a trace starts after the delay ('~scope 256 128 0 1'): 0 at once, 1 when the "
            "signal (or X) rises to triglevel, 2 when it falls to it.",
            "0, 1 or 2");
  PARAM_DOC("triglevel", "0", "The level the triggers watch for ('~scope 256 128 0 1 0.5').",
            "any float");
}

FLOAT_IN(SetPeriod) {
  (void)inlet;
  (void)thread;
  if (!std::isnan(value)) calccount = value;
}

INT_IN(SetPeriodInt) {
  (void)inlet;
  (void)thread;
  calccount = static_cast<float>(value);
}

FLOAT_IN(SetSize) {
  (void)inlet;
  (void)thread;
  if (!std::isnan(value)) bufsize = value;
}

INT_IN(SetSizeInt) {
  (void)inlet;
  (void)thread;
  bufsize = static_cast<float>(value);
}

MESSAGES() {
  (void)value;
  // A missing or malformed number is ignored.
  std::size_t next = 0;
  const std::string_view word = Token(message, 0, next);
  float number = 0.f;
  if (!Number(message, next, number)) return;
  if (word == "calccount") {
    calccount = number;
  } else if (word == "bufsize") {
    bufsize = number;
  } else if (word == "delay") {
    delay = number;
  } else if (word == "trigger") {
    trigger = number;
  } else if (word == "triglevel") {
    triglevel = number;
  }
}

CALC() {
  (void)thread;
  fn.period(scopeCapture::Period(calccount.load()))
      .size(scopeCapture::Size(bufsize.load()))
      .delay(delay.load())
      .trigger(scopeCapture::Trigger(trigger.load()))
      .level(triglevel.load());
  // Unconnected, an inlet reads as silence (#1036); a cord on the right
  // inlet makes it X/Y. No outlets: the host polls the traces.
  if (inputs[1].IsConstant())
    fn(inputs[0].Signal(0.f));
  else
    fn(inputs[0].Signal(0.f), inputs[1].Signal(0.f));
}

// Host thread: the newest trace, every cell.
GUI_VALUE() {
  std::lock_guard<std::mutex> lock(reading);
  const scopeCapture::trace t = fn.acquire();
  std::string out;
  out.reserve(16 + static_cast<std::size_t>(t.size) * 24);
  out += TraceCell(t, 0);
  out += ' ';
  out += TraceCell(t, 1);
  out += ' ';
  out += TraceCell(t, 2);
  for (UInt i = 0; i < t.size; i++) {
    out += ' ';
    Append(out, t.first[i]);
    out += ' ';
    Append(out, t.second[i]);
  }
  return out;
}

GUI_VALUE_COUNT() {
  std::lock_guard<std::mutex> lock(reading);
  return HEADER + 2 * fn.held().size;
}

// Cell 0 takes the newest trace; the others read the one it took.
GUI_VALUE_AT() {
  std::lock_guard<std::mutex> lock(reading);
  const scopeCapture::trace t = index == 0 ? fn.acquire() : fn.held();
  return TraceCell(t, index);
}

#undef className
