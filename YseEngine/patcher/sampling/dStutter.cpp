#include "dStutter.h"
#include <cmath>
#include <string_view>
#include "../pListArgs.h"
#include "../pSelector.h"

using namespace YSE::PATCHER;
using YSE::DSP::stutter;

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

  // The number in the token after `from`; false when there is none, it is
  // not a number, or it is NaN. `next` is set to where the token ends.
  bool NumberAfter(const std::string& text, std::size_t from, std::size_t& next, float& number) {
    const std::string_view token = Token(text, from, next);
    return !token.empty() && ReadNumericToken(token.data(), token.size(), number) &&
           !std::isnan(number);
  }

  // A sample count or channel from a number: truncated toward 0, at least
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

#define className dStutter

CONSTRUCT_DSP() {
  REG_PARM_CLEAR;
  REG_PARM_PARSE;
  ADD_PARAM(arguments);

  ShapePorts();

  ADD_DESCRIPTION("Records its input all the time and, on a trigger, copies the most recent "
                  "samples into a fragment that one or more phases replay: stutter and glitch "
                  "effects.");
  ADD_CATEGORY(pCategory::SAMPLING);
  PARAM_DOC("stutter", "88200 4410 1 0 1",
            "The record buffer in samples (at most 4194304), the samples a trigger copies, the "
            "trigger polarity (above 0 positive, else negative), the samples copied per block "
            "(0 all at once) and the number of outputs (1-30): '~stutter 44100 2205 1 0 2'. "
            "Later numbers and messages do not change what is saved.",
            "five numbers");
}

void dStutter::SetParent(pObject* newParent) {
  pObject::SetParent(newParent);
  // The scheduler `setbuf` goes through, built here so a message on the
  // audio thread finds it.
  EnableFileIO();
}

void dStutter::ShapePorts() {
  // The numbers in order; anything else is ignored.
  float values[5] = {static_cast<float>(stutter::DEFAULT_CAPACITY),
                     static_cast<float>(stutter::DEFAULT_SIZE), 1.f, 0.f, 1.f};
  int numbers = 0;
  for (const std::string& token : arguments) {
    float number = 0.f;
    if (numbers >= 5) break;
    if (token.empty() || !ReadNumericToken(token, number) || std::isnan(number)) continue;
    values[numbers++] = number;
  }
  const int capacity = Whole(values[0], 1);
  const int count = Whole(values[4], 1);
  fn = stutter(static_cast<UInt>(capacity), static_cast<UInt>(count));
  // The settings now, as CALC() hands them over at every block.
  fn.size(static_cast<UInt>(Whole(values[1], 0)))
      .polarity(values[2] > 0.f)
      .copyRate(static_cast<UInt>(Whole(values[3], 0)));
  maxSize.store(static_cast<int>(fn.capacity()), std::memory_order_relaxed);
  copySize.store(Whole(values[1], 0), std::memory_order_relaxed);
  positive.store(values[2] > 0.f, std::memory_order_relaxed);
  copyRate.store(Whole(values[3], 0), std::memory_order_relaxed);
  ampVar.store(0.f, std::memory_order_relaxed);
  dropout.store(0.f, std::memory_order_relaxed);
  repeat.store(0.f, std::memory_order_relaxed);
  trigger.store(0.f, std::memory_order_relaxed);
  for (aFlt& p : phaseConst)
    p.store(0.f, std::memory_order_relaxed);

  // Rebuilt rather than resized, as ~tapout: the phases and outputs follow
  // the arguments.
  const int outCount = static_cast<int>(fn.outputs());
  inputs.clear();
  outputs.clear();

  // in 0: the signal recorded, a bang, the size, and the messages
  ADD_IN_0;
  REG_SIGNAL_IN;
  REG_BANG_IN(Bang);
  REG_FLOAT_IN(SetSizeFloat);
  REG_INT_IN(SetSize);
  INLET_DOC(0, "input",
            "The signal recorded, all the time; unconnected it records silence. A bang copies "
            "the most recent samples into the fragment at the next block; a number sets how "
            "many a trigger copies. 'clear' sets the recording and the fragment to 0; "
            "'maxsize <n>' holds the size to n (within the first argument); 'polarity <0|1>' "
            "picks negative or positive triggers; 'ampvar <0-1>' varies each trigger's gain "
            "at random; 'dropout <%>' and 'repeat <%>' are the chances that a trigger silences "
            "the outputs or keeps the old fragment; 'setbuf <name> [offset] [chan]' writes the "
            "fragment into a buffer. None of them is saved with the patch.",
            "any float");

  // in 1: the trigger (signal, float or int)
  ADD_IN_1;
  REG_SIGNAL_IN;
  REG_FLOAT_IN(SetTrigger);
  REG_INT_IN(SetTriggerInt);
  INLET_DOC(1, "trigger",
            "The trigger: a positive trigger is the signal going from 0 or below to above 0, a "
            "negative one from 0 or above to below 0. Each copies the most recent samples into "
            "the fragment. A number is used while no signal is connected.",
            "any float");

  // in 2 and on: one phase per output; out 0 and on: the fragments
  for (int k = 0; k < outCount; ++k) {
    inputs.emplace_back(this, false, k + 2);
    REG_SIGNAL_IN;
    REG_FLOAT_IN(SetPhase);
    REG_INT_IN(SetPhaseInt);
    ADD_OUT_BUFFER;
    const std::string n = outCount == 1 ? std::string() : " " + std::to_string(k + 1);
    const std::string output = outCount == 1 ? std::string("the output") : "output" + n;
    const std::string phase = outCount == 1 ? std::string("the phase") : "phase" + n;
    inputs.back().SetDoc("phase" + n,
                         "Where " + output +
                             " reads the fragment: 0 to 1 across it, wrapping, so a ~phasor "
                             "loops it. A number is used while no signal is connected.",
                         "0 to 1");
    outputs.back().SetDoc("out" + n,
                          "The fragment read at " + phase +
                              ", interpolated. Silence before the first trigger and after a "
                              "dropout.",
                          "any float");
  }
}

PARM_CLEAR() {
  arguments.clear();
  ShapePorts();
}

// Control thread: the arguments are a list parameter, so a change rebuilds
// the object and this runs on one no audio thread has seen.
PARM_PARSE() {
  ShapePorts();
}

BANG_IN(Bang) {
  (void)inlet;
  (void)thread;
  bangPending.store(true, std::memory_order_relaxed);
}

FLOAT_IN(SetSizeFloat) {
  (void)inlet;
  (void)thread;
  copySize.store(Whole(value, 0), std::memory_order_relaxed);
}

INT_IN(SetSize) {
  (void)inlet;
  (void)thread;
  copySize.store(AtLeast(value, 0), std::memory_order_relaxed);
}

FLOAT_IN(SetTrigger) {
  (void)inlet;
  (void)thread;
  trigger.store(value, std::memory_order_relaxed);
}

INT_IN(SetTriggerInt) {
  (void)inlet;
  (void)thread;
  trigger.store(static_cast<float>(value), std::memory_order_relaxed);
}

FLOAT_IN(SetPhase) {
  (void)thread;
  const int k = inlet - 2;
  if (k >= 0 && k < static_cast<int>(stutter::MAX_OUTPUTS))
    phaseConst[static_cast<std::size_t>(k)].store(value, std::memory_order_relaxed);
}

INT_IN(SetPhaseInt) {
  (void)thread;
  const int k = inlet - 2;
  if (k >= 0 && k < static_cast<int>(stutter::MAX_OUTPUTS))
    phaseConst[static_cast<std::size_t>(k)].store(static_cast<float>(value),
                                                  std::memory_order_relaxed);
}

// `clear`, `maxsize <n>`, `polarity <n>`, `ampvar <x>`, `dropout <%>`,
// `repeat <%>`, `setbuf <name> [offset] [chan]`, on any thread.
MESSAGES() {
  (void)value;
  std::size_t next = 0;
  float number = 0.f;
  const std::string_view word = Token(message, 0, next);
  if (word == "clear") {
    clearPending.store(true, std::memory_order_relaxed);
    return;
  }
  if (word == "setbuf") {
    const std::string_view name = Token(message, next, next);
    if (name.empty()) return;
    int offset = 0;
    int channel = 1;
    if (NumberAfter(message, next, next, number)) {
      offset = Whole(number, 0);
      if (NumberAfter(message, next, next, number)) channel = Whole(number, 1);
    }
    setbufOffset.store(offset, std::memory_order_relaxed);
    setbufChannel.store(channel, std::memory_order_relaxed);
    if (writer.Request(*this, name.data(), name.size()))
      setbufPending.fetch_add(1, std::memory_order_relaxed);
    return;
  }
  if (!NumberAfter(message, next, next, number)) return;
  if (word == "maxsize") {
    maxSize.store(Whole(number, 0), std::memory_order_relaxed);
  } else if (word == "polarity") {
    positive.store(number > 0.f, std::memory_order_relaxed);
  } else if (word == "ampvar") {
    ampVar.store(number, std::memory_order_relaxed);
  } else if (word == "dropout") {
    dropout.store(number, std::memory_order_relaxed);
  } else if (word == "repeat") {
    repeat.store(number, std::memory_order_relaxed);
  }
}

CALC() {
  // A setbuf whose buffer was found since the last block: the fragment, as
  // it is now, written into it.
  if (writer.Take(*this) && setbufPending.load(std::memory_order_relaxed) > 0) {
    setbufPending.fetch_sub(1, std::memory_order_relaxed);
    DSP::sampleWriteView view = writer.Current().pin();
    fn.copyTo(view, static_cast<UInt>(setbufChannel.load(std::memory_order_relaxed) - 1),
              static_cast<UInt>(setbufOffset.load(std::memory_order_relaxed)));
  }
  if (clearPending.exchange(false, std::memory_order_relaxed)) fn.clear();
  if (bangPending.exchange(false, std::memory_order_relaxed)) fn.trigger();

  fn.maxSize(static_cast<UInt>(maxSize.load(std::memory_order_relaxed)))
      .size(static_cast<UInt>(copySize.load(std::memory_order_relaxed)))
      .polarity(positive.load(std::memory_order_relaxed))
      .copyRate(static_cast<UInt>(copyRate.load(std::memory_order_relaxed)))
      .ampVariation(ampVar.load(std::memory_order_relaxed))
      .dropoutChance(dropout.load(std::memory_order_relaxed))
      .repeatChance(repeat.load(std::memory_order_relaxed));

  // A connected signal wins over the number (issue #1036).
  const UInt count = fn.outputs();
  for (UInt k = 0; k < count; ++k) {
    inlet& in = inputs[k + 2];
    const float fallback = phaseConst[k].load(std::memory_order_relaxed);
    phases[k] = in.IsConstant() ? stutter::signal(fallback) : stutter::signal(in.Signal(fallback));
  }
  const stutter::signal trig = inputs[1].IsConstant()
                                   ? stutter::signal(trigger.load(std::memory_order_relaxed))
                                   : stutter::signal(inputs[1].Signal(trigger.load()));
  if (inputs[0].IsConstant()) {
    fn(YSE::STANDARD_BUFFERSIZE, stutter::signal(0.f), trig, phases.data());
  } else {
    DSP::buffer& in = inputs[0].Signal(0.f);
    fn(in.getLength(), stutter::signal(in), trig, phases.data());
  }
  for (UInt k = 0; k < count; ++k)
    outputs[k].SendBuffer(&fn.out(k), thread);
}

#undef className
