#include "dPoke.h"
#include "../pListArgs.h"
#include "../pSelector.h"

using namespace YSE::PATCHER;

#define className dPoke

namespace {

  // Up to `capacity` numbers from the start of `text`, read in place: no
  // copy, no allocation. Stops at the first token that is not a number.
  std::size_t ReadNumbers(const std::string& text, float* out, std::size_t capacity) {
    const char* s = text.c_str();
    const std::size_t length = text.size();
    std::size_t at = 0;
    std::size_t count = 0;
    while (count < capacity) {
      while (at < length && IsSelectorSeparator(s[at]))
        at++;
      const std::size_t begin = at;
      while (at < length && !IsSelectorSeparator(s[at]))
        at++;
      if (at == begin || !ReadNumericToken(s + begin, at - begin, out[count])) break;
      count++;
    }
    return count;
  }

} // namespace

CONSTRUCT_DSP() {
  constValue = 0.f;
  constIndex = -1.f;

  // in 0: the values (signal, float or int), and the list
  ADD_IN_0;
  REG_SIGNAL_IN;
  REG_FLOAT_IN(SetValue);
  REG_INT_IN(SetValueInt);
  REG_LIST_IN(List);

  // in 1: the indexes (signal, float or int)
  ADD_IN_1;
  REG_SIGNAL_IN;
  REG_FLOAT_IN(SetIndex);
  REG_INT_IN(SetIndexInt);

  // in 2: the channel, from 1
  ADD_IN_2;
  REG_FLOAT_IN(SetChannelFloat);
  REG_INT_IN(SetChannel);

  REG_PARM_PARSE;
  ADD_PARAM(bufferName);
  ADD_PARAM(channel);

  ADD_DESCRIPTION("Writes a signal into a named sample buffer, each sample at the index a second "
                  "signal gives. An index of -1 writes nothing.");
  ADD_CATEGORY(pCategory::SAMPLING);
  INLET_DOC(0, "value",
            "The values to write: a signal, or a number used while no signal is connected. A "
            "list 'value index [channel]' writes one value at once. 'set <name>' switches to "
            "another buffer (from a later block; not saved with the patch).",
            "any float");
  INLET_DOC(1, "index",
            "Where to write each value, in samples from 0: a signal, or a number used while no "
            "signal is connected. Below 0 or past the end of the buffer writes nothing.",
            "-1, 0 or higher");
  INLET_DOC(2, "channel", "The buffer channel to write, from 1.", "1 or higher");
  PARAM_DOC("name", "", "The buffer to write. A set message does not change what is saved.",
            "buffer name");
  PARAM_DOC("channel", "1", "The buffer channel to write, from 1.", "1 or higher");
}

void dPoke::SetParent(pObject* newParent) {
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

void dPoke::StoreChannel(int c) {
  channel.store(c < 1 ? 1 : c, std::memory_order_relaxed);
}

FLOAT_IN(SetValue) {
  (void)inlet;
  (void)thread;
  constValue = value;
}

INT_IN(SetValueInt) {
  (void)inlet;
  (void)thread;
  constValue = static_cast<float>(value);
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

// `value index [channel]`: one write, at once, on whichever thread sends.
LIST_IN(List) {
  (void)inlet;
  (void)thread;
  float numbers[3] = {};
  const std::size_t count = ReadNumbers(value, numbers, 3);
  if (count < 2) return;
  if (count == 3) {
    if (numbers[2] >= 1.f && numbers[2] < 2147483648.f) {
      StoreChannel(static_cast<int>(numbers[2]));
    } else if (numbers[2] < 1.f) {
      StoreChannel(1);
    }
  }
  // Another thread is writing a list: drop this one rather than wait.
  if (busy.exchange(true, std::memory_order_acquire)) return;
  {
    DSP::sampleWriteView view = binding.Current().message.pin();
    messagePath.channel(static_cast<UInt>(channel.load(std::memory_order_relaxed) - 1));
    messagePath(view, numbers[0], numbers[1]); // value, index
  } // the view goes before the guard
  busy.store(false, std::memory_order_release);
}

// `set [name]`, on any thread: handed to the background.
MESSAGES() {
  (void)value;
  binding.HandleSet(*this, message);
}

CALC() {
  (void)thread;
  // A `set` waiting: take it while no list is in. A list that holds the guard
  // only defers it to the next block.
  if (!busy.exchange(true, std::memory_order_acquire)) {
    binding.Take(*this);
    busy.store(false, std::memory_order_release);
  }
  DSP::sampleWriteView view = binding.Current().signal.pin();
  if (view.empty()) return;
  signalPath.channel(static_cast<UInt>(channel.load(std::memory_order_relaxed) - 1));

  // A connected signal wins over the number (issue #1036).
  const bool valueSignal = !inputs[0].IsConstant();
  const bool indexSignal = !inputs[1].IsConstant();
  if (valueSignal && indexSignal)
    signalPath(view, inputs[0].Signal(constValue), inputs[1].Signal(constIndex));
  else if (indexSignal)
    signalPath(view, constValue.load(), inputs[1].Signal(constIndex));
  else if (valueSignal)
    signalPath(view, inputs[0].Signal(constValue), constIndex.load());
  else
    signalPath(view, constValue.load(), constIndex.load());
}

#undef className
