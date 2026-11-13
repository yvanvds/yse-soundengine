#include "dLookup.h"
#include <cmath>
#include <string_view>
#include "../pListArgs.h"
#include "../pSelector.h"

using namespace YSE::PATCHER;

namespace {

  // The token starting at or after `from` in `text`, walked in place so
  // nothing allocates on whichever thread delivers the message. `next` is
  // set to where the token ends. Empty when there is none.
  std::string_view Token(const std::string& text, std::size_t from, std::size_t& next) {
    const char* chars = text.c_str();
    const std::size_t length = text.size();
    std::size_t begin = from;
    while (begin < length && IsSelectorSeparator(chars[begin]))
      begin++;
    std::size_t end = begin;
    while (end < length && !IsSelectorSeparator(chars[end]))
      end++;
    next = end;
    return std::string_view(chars + begin, end - begin);
  }

  // A frame count or channel from a number: truncated toward 0, at least
  // `lowest`, saturated at the int range; NaN is `lowest`.
  int Whole(float value, int lowest) {
    if (!(value >= static_cast<float>(lowest))) return lowest;
    if (!(value < 2147483648.f)) return 2147483647;
    return static_cast<int>(value);
  }

  int AtLeast(int value, int lowest) {
    return value < lowest ? lowest : value;
  }

} // namespace

#define className dLookup

CONSTRUCT_DSP() {
  constInput = 0.f;

  // in 0: the input (signal, float or int), and the messages
  ADD_IN_0;
  REG_SIGNAL_IN;
  REG_FLOAT_IN(SetInput);
  REG_INT_IN(SetInputInt);

  // in 1: the offset (signal, float or int)
  ADD_IN_1;
  REG_SIGNAL_IN;
  REG_FLOAT_IN(SetOffsetFloat);
  REG_INT_IN(SetOffset);

  // in 2: the size (signal, float or int)
  ADD_IN_2;
  REG_SIGNAL_IN;
  REG_FLOAT_IN(SetSizeFloat);
  REG_INT_IN(SetSize);

  // out 0: the values looked up
  ADD_OUT_BUFFER;

  REG_PARM_PARSE;
  ADD_PARAM(bufferName);
  ADD_PARAM(tableOffset);
  ADD_PARAM(tableSize);
  ADD_PARAM(channel);

  ADD_DESCRIPTION("Uses part of a named sample buffer as a transfer function: a signal from -1 "
                  "to 1 reads the table from its first entry to its last, so a curve drawn into "
                  "the buffer shapes the signal (waveshaping).");
  ADD_CATEGORY(pCategory::SAMPLING);
  INLET_DOC(0, "input",
            "The signal looked up, -1 to 1, or a number used while no signal is connected. "
            "Beyond -1 or 1 outputs 0. 'chan <n>', 'offset <n>' and 'size <n>' change the "
            "channel and the table; 'set <name>' switches to another buffer from a later block "
            "(none of them saved with the patch).",
            "-1 to 1");
  INLET_DOC(1, "offset",
            "The first frame of the table, the one -1 reads: a number, or a signal read at the "
            "first sample of each block. A number is not saved with the patch.",
            "0 or higher");
  INLET_DOC(2, "size",
            "The frame the table ends before, counted from the start of the buffer: a number, "
            "or a signal read at the first sample of each block. A number is not saved with the "
            "patch.",
            "0 or higher");
  OUTLET_DOC(0, "out",
             "The table's value at each input, a straight line between two entries. Silence "
             "without a buffer, or when the table has no entries.",
             "any float");
  PARAM_DOC("name", "",
            "The buffer used as the table. A set message does not change what is saved.",
            "buffer name");
  PARAM_DOC("offset", "0", "The first frame of the table, the one -1 reads.", "0 or higher");
  PARAM_DOC("size", "512",
            "The frame the table ends before, counted from the start of the buffer: the table "
            "holds size minus offset entries. Past the end of the buffer it stops at the last "
            "frame.",
            "0 or higher");
  PARAM_DOC("chan", "1", "The buffer channel read, from 1.", "1 or higher");
}

void dLookup::SetParent(pObject* newParent) {
  pObject::SetParent(newParent);
  // The scheduler `set` goes through, built here so a message on the audio
  // thread finds it.
  EnableFileIO();
}

// Control thread: the name is a string parameter, so a change of argument
// rebuilds the object and this runs on one no audio thread has seen.
PARM_PARSE() {
  binding.Bind(bufferName);
  tableOffset.store(AtLeast(tableOffset.load(std::memory_order_relaxed), 0),
                    std::memory_order_relaxed);
  tableSize.store(AtLeast(tableSize.load(std::memory_order_relaxed), 0), std::memory_order_relaxed);
  channel.store(AtLeast(channel.load(std::memory_order_relaxed), 1), std::memory_order_relaxed);
}

FLOAT_IN(SetInput) {
  (void)inlet;
  (void)thread;
  constInput = value;
}

INT_IN(SetInputInt) {
  (void)inlet;
  (void)thread;
  constInput = static_cast<float>(value);
}

FLOAT_IN(SetOffsetFloat) {
  (void)inlet;
  (void)thread;
  tableOffset.store(Whole(value, 0), std::memory_order_relaxed);
}

INT_IN(SetOffset) {
  (void)inlet;
  (void)thread;
  tableOffset.store(AtLeast(value, 0), std::memory_order_relaxed);
}

FLOAT_IN(SetSizeFloat) {
  (void)inlet;
  (void)thread;
  tableSize.store(Whole(value, 0), std::memory_order_relaxed);
}

INT_IN(SetSize) {
  (void)inlet;
  (void)thread;
  tableSize.store(AtLeast(value, 0), std::memory_order_relaxed);
}

// `set [name]`, `chan <n>`, `offset <n>`, `size <n>`, on any thread.
MESSAGES() {
  (void)value;
  if (binding.HandleSet(*this, message)) return;
  std::size_t next = 0;
  const std::string_view word = Token(message, 0, next);
  const std::string_view argument = Token(message, next, next);
  float number = 0.f;
  if (!ReadNumericToken(argument.data(), argument.size(), number) || std::isnan(number)) return;
  if (word == "chan") {
    channel.store(Whole(number, 1), std::memory_order_relaxed);
  } else if (word == "offset") {
    tableOffset.store(Whole(number, 0), std::memory_order_relaxed);
  } else if (word == "size") {
    tableSize.store(Whole(number, 0), std::memory_order_relaxed);
  }
}

CALC() {
  binding.Take(*this); // a `set` waiting since the last block
  DSP::sampleView view = binding.Current().pin();

  // The offset and size: a connected signal's first sample wins over the
  // number (issue #1036), as Max reads them once per block.
  const int first = inputs[1].IsConstant() ? tableOffset.load(std::memory_order_relaxed)
                                           : Whole(inputs[1].Signal(0.f).getPtr()[0], 0);
  const int end = inputs[2].IsConstant() ? tableSize.load(std::memory_order_relaxed)
                                         : Whole(inputs[2].Signal(0.f).getPtr()[0], 0);
  fn.channel(static_cast<UInt>(channel.load(std::memory_order_relaxed) - 1))
      .offset(first)
      .size(end);

  DSP::buffer& out =
      inputs[0].IsConstant() ? fn(view, constInput.load()) : fn(view, inputs[0].Signal(constInput));
  outputs[0].SendBuffer(&out, thread);
}

#undef className
