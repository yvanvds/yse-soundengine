#include "dSash.h"
#include "../pListArgs.h"
#include "../pSelector.h"

#include <cmath>
#include <string_view>

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

  // The number after a one-argument message; false when it is missing or not
  // a number.
  bool Argument(const std::string& message, std::size_t& cursor, float& number) {
    std::size_t begin = 0;
    std::size_t length = 0;
    if (!NextToken(message, cursor, begin, length)) return false;
    return ReadNumericToken(message.c_str() + begin, length, number);
  }

  // The attribute keywords of the arguments and messages.
  enum class keyword { none, samplelevel, advancelevel, dir, mode, size, maxsize };

  keyword ToKeyword(const char* text, std::size_t length) {
    const std::string_view word(text, length);
    if (word == "samplelevel") return keyword::samplelevel;
    if (word == "advancelevel") return keyword::advancelevel;
    if (word == "dir") return keyword::dir;
    if (word == "mode") return keyword::mode;
    if (word == "size") return keyword::size;
    if (word == "maxsize") return keyword::maxsize;
    return keyword::none;
  }

  constexpr unsigned int DEFAULT_CAPACITY = YSE::DSP::sampleHoldMemory::DEFAULT_MAX_SIZE;
  constexpr float MAX_CAPACITY = 1048576.f;
} // namespace

#define className dSash

CONSTRUCT_DSP() {
  Allocate(DEFAULT_CAPACITY);

  // in 0: the signal to sample (a number while unconnected); a list fills
  // the memory; messages
  ADD_IN_0;
  REG_SIGNAL_IN;
  REG_FLOAT_IN(SetInput);
  REG_INT_IN(SetInputInt);
  REG_LIST_IN(SetList);

  // in 1: the sample trigger; a number sets samplelevel
  ADD_IN_1;
  REG_SIGNAL_IN;
  REG_FLOAT_IN(SetSampleLevel);
  REG_INT_IN(SetSampleLevelInt);

  // in 2: the advance trigger; a number sets advancelevel
  ADD_IN_2;
  REG_SIGNAL_IN;
  REG_FLOAT_IN(SetAdvanceLevel);
  REG_INT_IN(SetAdvanceLevelInt);

  // out 0: the value read from the memory
  ADD_OUT_BUFFER;

  REG_PARM_CLEAR;
  REG_PARM_PARSE;
  ADD_PARAM(arguments);

  ADD_DESCRIPTION("Sample and hold with memory: records its input into a ring of stored values on "
                  "one trigger and plays them back on another, so a sequence of sampled values "
                  "can loop, such as a random melody that repeats.");
  ADD_CATEGORY(pCategory::FILTER);
  INLET_DOC(0, "in",
            "The signal to sample (unconnected, the last number sent here, 0 at first). A list "
            "of numbers fills the memory and sets the size to its length. 'samplelevel <x>' and "
            "'advancelevel <x>' set the two trigger levels; 'dir <0-3>' (how an advance moves: "
            "0 forward, 1 backward, 2 random, 3 random without repeats until every value has "
            "played); 'mode <0-2>' (how the advance trigger is read: 0 rising above "
            "advancelevel, 1 its whole value is the place to read, 2 a 0-1 phase divided into "
            "size parts); 'size <n>' (the number of values in use, 1 to maxsize).",
            "");
  INLET_DOC(1, "sample",
            "The sample trigger: rising above samplelevel records the input into the next place "
            "(unconnected reads 0). A number sets samplelevel.",
            "");
  INLET_DOC(2, "advance",
            "The advance trigger, read as mode says, which picks the place played (unconnected "
            "reads 0). A number sets advancelevel.",
            "");
  OUTLET_DOC(0, "out", "The value at the place played.", "");
  PARAM_DOC("attributes", "",
            "Keywords and their numbers: 'samplelevel <x>', 'advancelevel <x>' (default 0.5 "
            "and 0.5), 'dir <n>', 'mode <n>' (default 0 and 0), 'size <n>' (default 1) and "
            "'maxsize <n>' (the capacity, default 1000, set only here). Messages and the "
            "memory are not saved.",
            "keywords and numbers");
}

void dSash::Allocate(unsigned int capacity) {
  fn.maxSize(capacity);
  listValues.reset(new std::atomic<float>[capacity]);
  for (unsigned int i = 0; i < capacity; ++i)
    listValues[i].store(0.f, std::memory_order_relaxed);
  listCapacity = capacity;
  listScratch.assign(capacity, 0.f);
}

// Control thread: a list parameter rebuilds the object, so these run on one
// no audio thread has seen, and may resize the DSP class directly. Clear
// runs first on every Set, parse only when there are arguments.
PARM_CLEAR() {
  arguments.clear();
  input = 0.f;
  samplelevel = 0.5f;
  advancelevel = 0.5f;
  dir = DSP::sampleHoldMemory::FORWARD;
  mode = DSP::sampleHoldMemory::LEVEL;
  size = 1;
  if (listCapacity != DEFAULT_CAPACITY) Allocate(DEFAULT_CAPACITY);
}

PARM_PARSE() {
  // maxsize first, wherever it is, so a size clamps against it.
  for (std::size_t i = 0; i + 1 < arguments.size(); ++i) {
    float number = 0.f;
    const std::string& token = arguments[i];
    if (ToKeyword(token.c_str(), token.size()) != keyword::maxsize) continue;
    if (ReadNumericToken(arguments[i + 1], number) && number >= 1.f && number <= MAX_CAPACITY &&
        static_cast<unsigned int>(number) != listCapacity)
      Allocate(static_cast<unsigned int>(number));
  }
  for (std::size_t i = 0; i < arguments.size(); ++i) {
    const std::string& token = arguments[i];
    const keyword word = ToKeyword(token.c_str(), token.size());
    // A keyword takes the number after it; anything else is skipped.
    if (word == keyword::none) continue;
    float number = 0.f;
    if (i + 1 < arguments.size() && ReadNumericToken(arguments[i + 1], number)) {
      ++i;
      if (word != keyword::maxsize) SetAttribute(static_cast<int>(word), number);
    }
  }
}

void dSash::SetAttribute(int which, float number) {
  switch (static_cast<keyword>(which)) {
  case keyword::samplelevel:
    if (std::isfinite(number)) samplelevel = number;
    break;
  case keyword::advancelevel:
    if (std::isfinite(number)) advancelevel = number;
    break;
  case keyword::dir:
    if (number >= 0.f && number < 4.f) dir = static_cast<int>(number);
    break;
  case keyword::mode:
    if (number >= 0.f && number < 3.f) mode = static_cast<int>(number);
    break;
  case keyword::size:
    if (number >= 1.f && number < MAX_CAPACITY + 1.f) {
      const auto n = static_cast<unsigned int>(number);
      size = n > listCapacity ? listCapacity : n;
    }
    break;
  case keyword::maxsize: // a creation argument only, as in Max
  case keyword::none:
    break;
  }
}

void dSash::PostList(const std::string& text) {
  // Count first, so the size goes out before the table.
  std::size_t cursor = 0;
  std::size_t begin = 0;
  std::size_t length = 0;
  float number = 0.f;
  unsigned int n = 0;
  while (n < listCapacity && NextToken(text, cursor, begin, length)) {
    if (ReadNumericToken(text.c_str() + begin, length, number) && std::isfinite(number)) ++n;
  }
  if (n == 0) return;
  size.store(n, std::memory_order_relaxed);

  listVersion.fetch_add(1, std::memory_order_acq_rel);
  cursor = 0;
  unsigned int written = 0;
  while (written < n && NextToken(text, cursor, begin, length)) {
    if (!ReadNumericToken(text.c_str() + begin, length, number) || !std::isfinite(number)) continue;
    listValues[written++].store(number, std::memory_order_relaxed);
  }
  listCount.store(written, std::memory_order_relaxed);
  listVersion.fetch_add(1, std::memory_order_release);
}

FLOAT_IN(SetInput) {
  (void)inlet;
  (void)thread;
  input = value;
}

INT_IN(SetInputInt) {
  (void)inlet;
  (void)thread;
  input = static_cast<float>(value);
}

LIST_IN(SetList) {
  (void)inlet;
  (void)thread;
  // A word first is a command that came as a list, as text through a
  // receive does: the inlet hands every list to a list handler. A list of
  // numbers fills the memory.
  std::size_t cursor = 0;
  std::size_t begin = 0;
  std::size_t length = 0;
  float number = 0.f;
  if (!NextToken(value, cursor, begin, length)) return;
  if (!ReadNumericToken(value.c_str() + begin, length, number)) {
    SetMessage(value, 0.f);
    return;
  }
  PostList(value);
}

FLOAT_IN(SetSampleLevel) {
  (void)inlet;
  (void)thread;
  if (std::isfinite(value)) samplelevel = value;
}

INT_IN(SetSampleLevelInt) {
  (void)inlet;
  (void)thread;
  samplelevel = static_cast<float>(value);
}

FLOAT_IN(SetAdvanceLevel) {
  (void)inlet;
  (void)thread;
  if (std::isfinite(value)) advancelevel = value;
}

INT_IN(SetAdvanceLevelInt) {
  (void)inlet;
  (void)thread;
  advancelevel = static_cast<float>(value);
}

MESSAGES() {
  (void)value;
  std::size_t cursor = 0;
  std::size_t begin = 0;
  std::size_t length = 0;
  if (!NextToken(message, cursor, begin, length)) return;
  const keyword word = ToKeyword(message.c_str() + begin, length);
  float number = 0.f;
  if (word == keyword::none || !Argument(message, cursor, number)) return;
  SetAttribute(static_cast<int>(word), number);
}

CALC() {
  fn.sampleLevel(samplelevel.load())
      .advanceLevel(advancelevel.load())
      .direction(static_cast<DSP::sampleHoldMemory::DIRECTION>(dir.load()))
      .mode(static_cast<DSP::sampleHoldMemory::MODE>(mode.load()));

  // The size first: a list posts its size before its table, so the table's
  // block finds the size already applied.
  const unsigned int places = size.load(std::memory_order_relaxed);
  if (places != appliedSize) {
    fn.size(places);
    appliedSize = places;
  }

  // A fresh list, when no write is under way and none came in meanwhile.
  const unsigned int before = listVersion.load(std::memory_order_acquire);
  if (before != listSeen && (before & 1u) == 0) {
    unsigned int n = listCount.load(std::memory_order_relaxed);
    if (n > listCapacity) n = listCapacity;
    for (unsigned int i = 0; i < n; ++i)
      listScratch[i] = listValues[i].load(std::memory_order_relaxed);
    std::atomic_thread_fence(std::memory_order_acquire);
    if (listVersion.load(std::memory_order_relaxed) == before) {
      listSeen = before;
      fn.load(listScratch.data(), n);
    }
  }

  // Unconnected inlets (#1036): the input reads its number, the triggers 0.
  DSP::buffer& out =
      fn(inputs[0].Signal(input.load()), inputs[1].Signal(0.f), inputs[2].Signal(0.f));
  outputs[0].SendBuffer(&out, thread);
}

#undef className
