#include "dPeek.h"
#include "../pListArgs.h"
#include "../pSelector.h"
#include <cstring>

using namespace YSE::PATCHER;

#define className dPeek

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

  // A float index as an int, truncated; false for one no int holds.
  bool IndexOf(float value, int& index) {
    if (!(value > -2147483648.f && value < 2147483648.f)) return false;
    index = static_cast<int>(value);
    return true;
  }

} // namespace

CONSTRUCT_DSP() {
  pendingValue = 0.f;

  // in 0: index (int or float), list, clip
  ADD_IN_0;
  REG_INT_IN(Index);
  REG_FLOAT_IN(IndexFloat);
  REG_LIST_IN(List);

  // in 1: the value the next index writes
  ADD_IN_1;
  REG_FLOAT_IN(Value);
  REG_INT_IN(ValueInt);

  // in 2: the channel, from 1
  ADD_IN_2;
  REG_FLOAT_IN(ChannelFloat);
  REG_INT_IN(Channel);

  // out 0: the sample read
  ADD_OUT_FLOAT;

  REG_PARM_PARSE;
  ADD_PARAM(bufferName);
  ADD_PARAM(channel);
  ADD_PARAM(clip);

  ADD_DESCRIPTION("Reads and writes single samples of a named sample buffer by message: an "
                  "index reads the sample there, or writes the value sent just before it. Works "
                  "with the audio stopped too.");
  ADD_CATEGORY(pCategory::SAMPLING);
  INLET_DOC(0, "index",
            "A sample index, from 0: the sample there goes out, or the value from the middle "
            "inlet is written there. A list 'index value [channel]' writes at once. 'clip 0' "
            "and 'clip 1' turn clipping of written values off and on. 'set <name>' switches to "
            "another buffer (not saved with the patch).",
            "0 or higher");
  INLET_DOC(1, "value", "The value the next index writes, instead of reading.", "any float");
  INLET_DOC(2, "channel", "The buffer channel to read and write, from 1.", "1 or higher");
  OUTLET_DOC(0, "sample", "The sample at the index. Nothing when the index is outside the buffer.",
             "any float");
  PARAM_DOC("name", "",
            "The buffer to read and write. A set message does not change what is saved.",
            "buffer name");
  PARAM_DOC("channel", "1", "The buffer channel to read and write, from 1.", "1 or higher");
  PARAM_DOC("clip", "1", "1 limits written values to -1 to 1, 0 writes them as they are.",
            "0 or 1");
}

void dPeek::SetParent(pObject* newParent) {
  pObject::SetParent(newParent);
  // The scheduler `set` goes through, built here so a message on the audio
  // thread finds it.
  EnableFileIO();
}

// Control thread: the name is a string parameter, so a change of argument
// rebuilds the object and this runs on one no other thread has seen.
PARM_PARSE() {
  binding.Bind(bufferName);
  SetChannel(channel.load(std::memory_order_relaxed));
}

void dPeek::SetChannel(int value) {
  channel.store(value < 1 ? 1 : value, std::memory_order_relaxed);
}

void dPeek::Access(int index, YSE::THREAD thread) {
  float sample = 0.f;
  bool read = false;
  // Another thread is in: drop this one rather than wait (the writer is one
  // claim).
  if (busy.exchange(true, std::memory_order_acquire)) return;
  {
    binding.Take(*this); // a `set` waiting since the last message
    DSP::sampleWriteView view = binding.Current().pin();
    access.channel(static_cast<UInt>(channel.load(std::memory_order_relaxed) - 1));
    access.clip(clip.load(std::memory_order_relaxed) != 0);
    if (pending.exchange(false, std::memory_order_acq_rel)) {
      access.write(view, index, pendingValue.load(std::memory_order_relaxed));
    } else {
      read = access.read(view, index, sample);
    }
  } // the view goes before the guard
  busy.store(false, std::memory_order_release);
  if (read) outputs[0].SendFloat(sample, thread);
}

void dPeek::Write(int index, float value) {
  if (busy.exchange(true, std::memory_order_acquire)) return;
  {
    binding.Take(*this);
    DSP::sampleWriteView view = binding.Current().pin();
    access.channel(static_cast<UInt>(channel.load(std::memory_order_relaxed) - 1));
    access.clip(clip.load(std::memory_order_relaxed) != 0);
    access.write(view, index, value);
  }
  busy.store(false, std::memory_order_release);
}

INT_IN(Index) {
  (void)inlet;
  Access(value, thread);
}

FLOAT_IN(IndexFloat) {
  (void)inlet;
  int index = 0;
  if (IndexOf(value, index)) Access(index, thread);
}

// `index value [channel]`.
LIST_IN(List) {
  (void)inlet;
  (void)thread;
  float numbers[3] = {};
  const std::size_t count = ReadNumbers(value, numbers, 3);
  int index = 0;
  if (count < 2 || !IndexOf(numbers[0], index)) return;
  if (count == 3) {
    int c = 1;
    if (IndexOf(numbers[2], c)) SetChannel(c);
  }
  Write(index, numbers[1]);
}

FLOAT_IN(Value) {
  (void)inlet;
  (void)thread;
  pendingValue.store(value, std::memory_order_relaxed);
  pending.store(true, std::memory_order_release);
}

INT_IN(ValueInt) {
  (void)inlet;
  (void)thread;
  pendingValue.store(static_cast<float>(value), std::memory_order_relaxed);
  pending.store(true, std::memory_order_release);
}

FLOAT_IN(ChannelFloat) {
  (void)inlet;
  (void)thread;
  int c = 1;
  if (IndexOf(value, c)) SetChannel(c);
}

INT_IN(Channel) {
  (void)inlet;
  (void)thread;
  SetChannel(value);
}

// `set [name]` and `clip <flag>`, read in place.
MESSAGES() {
  (void)value;
  if (binding.HandleSet(*this, message)) return;
  const char* text = message.c_str();
  const std::size_t length = message.size();
  std::size_t at = 0;
  while (at < length && IsSelectorSeparator(text[at]))
    at++;
  const std::size_t begin = at;
  while (at < length && !IsSelectorSeparator(text[at]))
    at++;
  if (at - begin != 4 || std::memcmp(text + begin, "clip", 4) != 0) return;
  while (at < length && IsSelectorSeparator(text[at]))
    at++;
  const std::size_t argBegin = at;
  while (at < length && !IsSelectorSeparator(text[at]))
    at++;
  float flag = 0.f;
  if (!ReadNumericToken(text + argBegin, at - argBegin, flag)) return;
  clip.store(flag != 0.f ? 1 : 0, std::memory_order_relaxed);
}

#undef className
