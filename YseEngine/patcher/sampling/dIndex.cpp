#include "dIndex.h"

using namespace YSE::PATCHER;

#define className dIndex

CONSTRUCT_DSP() {
  constIndex = 0.f;

  // in 0: the indexes (signal, float or int), and set
  ADD_IN_0;
  REG_SIGNAL_IN;
  REG_FLOAT_IN(SetIndex);
  REG_INT_IN(SetIndexInt);

  // in 1: the channel, from 1
  ADD_IN_1;
  REG_FLOAT_IN(SetChannelFloat);
  REG_INT_IN(SetChannel);

  // out 0: the samples read
  ADD_OUT_BUFFER;

  REG_PARM_PARSE;
  ADD_PARAM(bufferName);
  ADD_PARAM(channel);

  ADD_DESCRIPTION("Reads a named sample buffer at the sample indexes a signal gives, without "
                  "interpolation: a fractional index reads the whole sample below it.");
  ADD_CATEGORY(pCategory::SAMPLING);
  INLET_DOC(0, "index",
            "Where to read, in samples from 0: a signal, or a number used while no signal is "
            "connected. Below 0 reads the first sample, past the end the last. 'set <name>' "
            "switches to another buffer (from a later block; not saved with the patch).",
            "0 or higher");
  INLET_DOC(1, "channel", "The buffer channel to read, from 1.", "1 or higher");
  OUTLET_DOC(0, "out",
             "The sample at each index. Silence without a buffer, or for a channel the buffer "
             "lacks.",
             "any float");
  PARAM_DOC("name", "", "The buffer to read. A set message does not change what is saved.",
            "buffer name");
  PARAM_DOC("channel", "1", "The buffer channel to read, from 1.", "1 or higher");
}

void dIndex::SetParent(pObject* newParent) {
  pObject::SetParent(newParent);
  // The scheduler `set` goes through, built here so a message on the audio
  // thread finds it.
  EnableFileIO();
}

// Control thread: the name is a string parameter, so a change of argument
// rebuilds the object and this runs on one no audio thread has seen.
PARM_PARSE() {
  binding.Bind(bufferName);
  StoreChannel(channel.load(std::memory_order_relaxed));
}

void dIndex::StoreChannel(int c) {
  channel.store(c < 1 ? 1 : c, std::memory_order_relaxed);
}

FLOAT_IN(SetIndex) {
  (void)inlet;
  (void)thread;
  constIndex = value;
}

INT_IN(SetIndexInt) {
  (void)inlet;
  (void)thread;
  constIndex = static_cast<float>(value);
}

FLOAT_IN(SetChannelFloat) {
  (void)inlet;
  (void)thread;
  if (value >= 1.f && value < 2147483648.f) {
    StoreChannel(static_cast<int>(value));
  } else if (value < 1.f) {
    StoreChannel(1);
  }
}

INT_IN(SetChannel) {
  (void)inlet;
  (void)thread;
  StoreChannel(value);
}

// `set [name]`, on any thread: handed to the background.
MESSAGES() {
  (void)value;
  binding.HandleSet(*this, message);
}

CALC() {
  binding.Take(*this); // a `set` waiting since the last block
  DSP::sampleView view = binding.Current().pin();
  reader.channel(static_cast<UInt>(channel.load(std::memory_order_relaxed) - 1));

  // A connected signal wins over the number (issue #1036).
  DSP::buffer& out = inputs[0].IsConstant() ? reader(view, constIndex.load())
                                            : reader(view, inputs[0].Signal(constIndex));
  outputs[0].SendBuffer(&out, thread);
}

#undef className
