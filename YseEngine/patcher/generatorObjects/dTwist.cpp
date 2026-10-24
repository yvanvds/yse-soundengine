#include "dTwist.h"
#include "../pListArgs.h"
#include "../pSelector.h"

using namespace YSE::PATCHER;

namespace {
  // The next token of `message` from `cursor` on, walked in place so nothing
  // allocates on whichever thread delivers the message. False at the end.
  bool NextToken(const std::string& message, std::size_t& cursor, std::size_t& begin,
                 std::size_t& length) {
    const char* text = message.c_str();
    const std::size_t size = message.size();
    while (cursor < size && IsSelectorSeparator(text[cursor]))
      cursor++;
    begin = cursor;
    while (cursor < size && !IsSelectorSeparator(text[cursor]))
      cursor++;
    length = cursor - begin;
    return length > 0;
  }

  bool Is(const std::string& message, std::size_t begin, std::size_t length, const char* name,
          std::size_t nameLength) {
    return length == nameLength && message.compare(begin, nameLength, name) == 0;
  }
} // namespace

#define className dTwist

CONSTRUCT_DSP() {
  curve = 0.f;

  // in 0: the ramp to bend (signal); float / int set the curve; messages
  ADD_IN_0;
  REG_SIGNAL_IN;
  REG_FLOAT_IN(SetCurve);
  REG_INT_IN(SetCurveInt);

  ADD_OUT_BUFFER;

  ADD_PARAM(curve);
  ADD_PARAM(shapemode);
  ADD_PARAM(syncupdate);

  ADD_DESCRIPTION("Bends a linear 0-1 ramp such as a ~phasor into an exponential curve, in "
                  "phase with it: accelerating or slowing clocks and curved envelopes.");
  ADD_CATEGORY(pCategory::OSC);
  INLET_DOC(0, "phase",
            "The ramp to bend, a signal from 0 to 1 (unconnected reads 0). A number sets the "
            "curve. 'curve <value>'; 'shapemode <0|1>'; 'syncupdate <0|1>'.",
            "0-1");
  OUTLET_DOC(0, "out", "The curved ramp, from 0 to 1 with the same start and end as the input.",
             "0-1");
  PARAM_DOC("curve", "0",
            "Curvature: 0 is linear, above 0 starts slow and speeds up, below 0 starts fast and "
            "slows down; steeper towards -1 and 1.",
            "-1 to 1");
  PARAM_DOC("shapemode", "1",
            "1: a falling ramp is bent the same way as a rising one; 0: it follows the rising "
            "curve backwards.",
            "0-1");
  PARAM_DOC("syncupdate", "0",
            "0: a new curve applies at once; 1: it waits for the input ramp to wrap or turn, so "
            "the output never jumps.",
            "0-1");
}

FLOAT_IN(SetCurve) {
  (void)inlet;
  (void)thread;
  curve = value;
}

INT_IN(SetCurveInt) {
  (void)inlet;
  (void)thread;
  curve = static_cast<float>(value);
}

MESSAGES() {
  (void)value;
  std::size_t cursor = 0;
  std::size_t begin = 0;
  std::size_t length = 0;
  if (!NextToken(message, cursor, begin, length)) return;

  const bool isCurve = Is(message, begin, length, "curve", 5);
  const bool isShape = Is(message, begin, length, "shapemode", 9);
  const bool isSync = Is(message, begin, length, "syncupdate", 10);
  if (!isCurve && !isShape && !isSync) return;

  std::size_t arg = 0;
  std::size_t argLength = 0;
  float number = 0.f;
  if (!NextToken(message, cursor, arg, argLength)) return;
  if (!ReadNumericToken(message.c_str() + arg, argLength, number)) return;

  if (isCurve)
    curve = number;
  else if (isShape)
    shapemode = number != 0.f ? 1 : 0;
  else
    syncupdate = number != 0.f ? 1 : 0;
}

CALC() {
  fn.syncUpdate(syncupdate != 0).shapeMode(shapemode != 0).curve(curve);

  // An unconnected inlet reads 0 (#1036).
  DSP::buffer& out = fn(inputs[0].Signal(0.f));
  outputs[0].SendBuffer(&out, thread);
}

#undef className
