#include "dClick.h"
#include "../pListArgs.h"
#include "../pSelector.h"

using namespace YSE::PATCHER;

#define className dClick

CONSTRUCT_DSP() {
  pendingScale = 1.f;

  // in 0: the trigger signal, bang, float, int and `set <values...>`
  ADD_IN_0;
  REG_SIGNAL_IN;
  REG_BANG_IN(Trigger);
  REG_FLOAT_IN(TriggerFloat);
  REG_INT_IN(TriggerInt);

  // out 0: the clicks
  ADD_OUT_BUFFER;

  REG_PARM_CLEAR;
  REG_PARM_PARSE;
  ADD_PARAM(arguments);

  ADD_DESCRIPTION("Outputs an impulse, or a short stored waveform, each time it is banged: an "
                  "excitation for filters and resonators such as a Karplus-Strong string.");
  ADD_CATEGORY(pCategory::OSC);
  INLET_DOC(0, "trigger",
            "A bang plays the waveform once; a number plays it scaled by that number; "
            "'set <values...>' stores a new waveform of up to 256 values ('set' alone is the "
            "single-sample impulse again). A signal triggers it at every sample where it moves "
            "from 0 to a value that is not 0. A bang or a number lands at the start of the next "
            "block; the signal is sample-accurate.",
            "any float");
  OUTLET_DOC(0, "out", "The waveform after each trigger, silence in between.", "any float");
  PARAM_DOC("waveform", "1",
            "The waveform played on a trigger, up to 256 values. None is the single-sample "
            "impulse 1. A set message does not change what is saved.",
            "list of floats, usually 0 to 1");
}

// Control thread: a list parameter rebuilds the object, so these run on one
// no audio thread has seen, and may write the generator directly. Clear runs
// first on every Set, parse only when there are arguments.
PARM_CLEAR() {
  arguments.clear();
  generator.set(nullptr, 0);
}

PARM_PARSE() {
  waveform parsed;
  for (const std::string& token : arguments) {
    if (parsed.count == DSP::click::MAX_LENGTH) break;
    float number = 0.f;
    if (ReadNumericToken(token, number)) parsed.values[parsed.count++] = number;
  }
  generator.set(parsed.values.data(), parsed.count);
}

BANG_IN(Trigger) {
  pendingScale = 1.f;
  triggerPending = true;
}

FLOAT_IN(TriggerFloat) {
  pendingScale = value;
  triggerPending = true;
}

INT_IN(TriggerInt) {
  pendingScale = static_cast<float>(value);
  triggerPending = true;
}

// `set <values...>`. The text is walked in place and the numbers are read
// straight into the sender's slot, so this allocates nothing on whichever
// thread delivers it.
MESSAGES() {
  (void)value;
  const char* text = message.c_str();
  const std::size_t length = message.size();
  std::size_t begin = 0;
  while (begin < length && IsSelectorSeparator(text[begin]))
    begin++;
  std::size_t end = begin;
  while (end < length && !IsSelectorSeparator(text[end]))
    end++;
  if (end - begin != 3 || message.compare(begin, 3, "set") != 0) return;

  if (writing.test_and_set(std::memory_order_acquire)) {
    dropped.fetch_add(1, std::memory_order_relaxed);
    return;
  }
  waveform& slot = slots[back];
  slot.count = 0;
  std::size_t pos = end;
  while (pos < length && slot.count < DSP::click::MAX_LENGTH) {
    while (pos < length && IsSelectorSeparator(text[pos]))
      pos++;
    std::size_t tokenEnd = pos;
    while (tokenEnd < length && !IsSelectorSeparator(text[tokenEnd]))
      tokenEnd++;
    float number = 0.f;
    if (tokenEnd > pos && ReadNumericToken(text + pos, tokenEnd - pos, number))
      slot.values[slot.count++] = number;
    pos = tokenEnd;
  }
  // Publish the slot and take the one CALC() left in the middle, read or not.
  back = middle.exchange(back | FRESH, std::memory_order_acq_rel) & ~FRESH;
  writing.clear(std::memory_order_release);
}

CALC() {
  if ((middle.load(std::memory_order_relaxed) & FRESH) != 0u) {
    front = middle.exchange(front, std::memory_order_acq_rel) & ~FRESH;
    const waveform& next = slots[front];
    generator.set(next.values.data(), next.count);
  }
  if (triggerPending.exchange(false)) generator.trigger(pendingScale);

  // A connected trigger signal adds sample-accurate triggers (#1036: an
  // unconnected inlet has no signal to read).
  DSP::buffer& out = inputs[0].IsConstant() ? generator() : generator(inputs[0].Signal(0.f));
  outputs[0].SendBuffer(&out, thread);
}

#undef className
