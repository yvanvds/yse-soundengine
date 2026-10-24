#include "dSwing.h"
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

#define className dSwing

CONSTRUCT_DSP() {
  swing = 0.5f;

  // in 0: the ramp to swing (signal); float / int set the swing; messages
  ADD_IN_0;
  REG_SIGNAL_IN;
  REG_FLOAT_IN(SetSwing);
  REG_INT_IN(SetSwingInt);

  ADD_OUT_BUFFER;

  ADD_PARAM(swing);
  ADD_PARAM(syncupdate);

  ADD_DESCRIPTION("Splits each cycle of a 0-1 ramp such as a ~phasor into two full ramps of "
                  "unequal length, in phase with it: swing or shuffle on a phasor clock.");
  ADD_CATEGORY(pCategory::OSC);
  INLET_DOC(0, "phase",
            "The ramp to swing, a signal from 0 to 1 (unconnected reads 0). A number sets the "
            "swing. 'swing <value>'; 'syncupdate <0|1>'.",
            "0-1");
  OUTLET_DOC(0, "out", "Two ramps per input cycle, the first lasting the swing share of it.",
             "0-1");
  PARAM_DOC("swing", "0.5",
            "Share of the input cycle taken by the first ramp; 0.5 makes two equal ramps.",
            "0.15-0.85");
  PARAM_DOC("syncupdate", "0",
            "0: a new swing applies at once; 1: it waits for the input to wrap, so no ramp "
            "changes length halfway.",
            "0-1");
}

FLOAT_IN(SetSwing) {
  (void)inlet;
  (void)thread;
  swing = value;
}

INT_IN(SetSwingInt) {
  (void)inlet;
  (void)thread;
  swing = static_cast<float>(value);
}

MESSAGES() {
  (void)value;
  std::size_t cursor = 0;
  std::size_t begin = 0;
  std::size_t length = 0;
  if (!NextToken(message, cursor, begin, length)) return;

  const bool isSwing = Is(message, begin, length, "swing", 5);
  const bool isSync = Is(message, begin, length, "syncupdate", 10);
  if (!isSwing && !isSync) return;

  std::size_t arg = 0;
  std::size_t argLength = 0;
  float number = 0.f;
  if (!NextToken(message, cursor, arg, argLength)) return;
  if (!ReadNumericToken(message.c_str() + arg, argLength, number)) return;

  if (isSwing)
    swing = number;
  else
    syncupdate = number != 0.f ? 1 : 0;
}

CALC() {
  fn.syncUpdate(syncupdate != 0).swing(swing);

  // An unconnected inlet reads 0 (#1036).
  DSP::buffer& out = fn(inputs[0].Signal(0.f));
  outputs[0].SendBuffer(&out, thread);
}

#undef className
