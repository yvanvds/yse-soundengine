#include "dStash.h"
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
  enum class keyword {
    none,
    samplelevel,
    advancelevel,
    sampletriggermode,
    advancetriggermode,
    dir,
    mode,
    writemode,
    extend,
    interp,
    duration,
    size,
    maxsize
  };

  keyword ToKeyword(const char* text, std::size_t length) {
    const std::string_view word(text, length);
    if (word == "samplelevel") return keyword::samplelevel;
    if (word == "advancelevel") return keyword::advancelevel;
    if (word == "sampletriggermode") return keyword::sampletriggermode;
    if (word == "advancetriggermode") return keyword::advancetriggermode;
    if (word == "dir") return keyword::dir;
    if (word == "mode" || word == "advancemode") return keyword::mode;
    if (word == "writemode") return keyword::writemode;
    if (word == "extend") return keyword::extend;
    if (word == "interp") return keyword::interp;
    if (word == "duration" || word == "dur") return keyword::duration;
    if (word == "size") return keyword::size;
    if (word == "maxsize") return keyword::maxsize;
    return keyword::none;
  }

  // The ordered actions.
  enum kind : int { WRITE, WRITE_POSITION, ADVANCE_WRITE, ADVANCE, CAPTURE };

  constexpr unsigned int DEFAULT_CAPACITY = YSE::DSP::signalStash::DEFAULT_MAX_SIZE;
  constexpr float MAX_CAPACITY = 1048576.f;
  // The longest duration a message sets (the largest float below 2^32).
  constexpr float MAX_DURATION = 4294967040.f;
} // namespace

#define className dStash

CONSTRUCT_DSP() {
  Allocate(DEFAULT_CAPACITY);

  // in 0: the signal to record (a number while unconnected, also written);
  // a list fills the memory; messages
  ADD_IN_0;
  REG_SIGNAL_IN;
  REG_FLOAT_IN(SetInput);
  REG_INT_IN(SetInputInt);
  REG_LIST_IN(SetList);

  // in 1: the sample trigger; a number sets samplelevel, a bang records
  ADD_IN_1;
  REG_SIGNAL_IN;
  REG_FLOAT_IN(SetSampleLevel);
  REG_INT_IN(SetSampleLevelInt);
  REG_BANG_IN(Capture);

  // in 2: the advance trigger or read index; a number sets advancelevel, a
  // bang advances
  ADD_IN_2;
  REG_SIGNAL_IN;
  REG_FLOAT_IN(SetAdvanceLevel);
  REG_INT_IN(SetAdvanceLevelInt);
  REG_BANG_IN(Advance);

  // in 3: the write index (a number while unconnected, also the write
  // position); a bang moves the write position on
  ADD_IN_3;
  REG_SIGNAL_IN;
  REG_FLOAT_IN(SetWriteIndex);
  REG_INT_IN(SetWriteIndexInt);
  REG_BANG_IN(AdvanceWrite);

  // out 0: the value read from the memory
  ADD_OUT_BUFFER;

  REG_PARM_CLEAR;
  REG_PARM_PARSE;
  ADD_PARAM(arguments);

  ADD_DESCRIPTION("Stores signal values into a memory of places and recalls them: a sample and "
                  "hold that remembers, an interpolating lookup table, or an audio-rate step "
                  "sequencer.");
  ADD_CATEGORY(pCategory::FILTER);
  INLET_DOC(0, "in",
            "The signal to record (unconnected, the last number sent here, 0 at first). A number "
            "is also written at the write position; a list of numbers fills the memory and sets "
            "the size to its length. 'samplelevel <x>' and 'advancelevel <x>' set the trigger "
            "levels; 'sampletriggermode <0-6>' and 'advancetriggermode <0-6>' when a trigger "
            "fires (0 rising above its level, 1 falling below it, 2 either, 3 equal to it, 4 "
            "from zero to nonzero, 5 from nonzero to zero, 6 passing it as a phase); 'dir <0-3>' "
            "how an advance moves (0 forward, 1 backward, 2 random, 3 random without repeats "
            "until every place has played); 'mode <0-2>' (or 'advancemode') what the advance "
            "inlet does (0 a trigger that advances, 1 the place to read, 2 a 0-1 share of the "
            "size); 'writemode <0-2>' where a recording goes (0 the next place, 1 the place the "
            "write index names, 2 the write index as a 0-1 share of the size); 'extend <0-3>' an "
            "index outside the memory (0 reads 0, 1 the first or last place, 2 wraps, 3 changes "
            "nothing); 'interp <0-2>' an index between places (0 rounds down, 1 mixes the two, "
            "2 rounds); 'duration <n>' (or 'dur') above 0 a new output lasts n samples, then 0; "
            "'size <n>' the places in use, 1 to maxsize.",
            "");
  INLET_DOC(1, "sample",
            "The sample trigger: when it fires, the input is recorded (unconnected reads 0). A "
            "number sets samplelevel; a bang records the first sample of the next block.",
            "");
  INLET_DOC(2, "advance",
            "The advance trigger, or the place to read in modes 1 and 2 (unconnected reads 0). A "
            "number sets advancelevel; a bang advances the output.",
            "");
  INLET_DOC(3, "write",
            "The write index in writemodes 1 and 2 (unconnected, the last number sent here, 0 at "
            "first). A number also sets the write position that numbers in the left inlet "
            "write; a bang moves it one place on.",
            "");
  OUTLET_DOC(0, "out", "The value read from the memory.", "");
  PARAM_DOC("samplelevel", "0.5",
            "The sample level, the first number; a second number is the advance level (default "
            "0.5 and 0.5). The keywords of the left inlet's messages set those attributes "
            "(defaults: trigger modes 0, dir 0, mode 0, writemode 0, extend 2, interp 0, "
            "duration 0, size 1), and 'maxsize <n>' the capacity (default 1000, set only here). "
            "Messages and the memory are not saved.",
            "numbers, then keywords");
}

void dStash::Allocate(unsigned int capacity) {
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
  writeIndex = 0.f;
  samplelevel = 0.5f;
  advancelevel = 0.5f;
  sampletriggermode = DSP::signalStash::ASCENDING;
  advancetriggermode = DSP::signalStash::ASCENDING;
  dir = DSP::signalStash::FORWARD;
  mode = DSP::signalStash::NEXT;
  writemode = DSP::signalStash::NEXT;
  extend = DSP::signalStash::WRAP;
  interp = DSP::signalStash::TRUNCATE;
  duration = 0;
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
  int levels = 0;
  for (std::size_t i = 0; i < arguments.size(); ++i) {
    const std::string& token = arguments[i];
    const keyword word = ToKeyword(token.c_str(), token.size());
    float number = 0.f;
    // A keyword takes the number after it; a keyword with none is skipped.
    if (word != keyword::none) {
      if (i + 1 < arguments.size() && ReadNumericToken(arguments[i + 1], number)) {
        ++i;
        if (word != keyword::maxsize) SetAttribute(static_cast<int>(word), number);
      }
      continue;
    }
    // The first two bare numbers are the levels, as Max's arguments; later
    // ones are ignored.
    if (levels < 2 && ReadNumericToken(token, number) && std::isfinite(number)) {
      SetAttribute(static_cast<int>(levels == 0 ? keyword::samplelevel : keyword::advancelevel),
                   number);
      ++levels;
    }
  }
}

void dStash::SetAttribute(int which, float number) {
  switch (static_cast<keyword>(which)) {
  case keyword::samplelevel:
    if (std::isfinite(number)) samplelevel = number;
    break;
  case keyword::advancelevel:
    if (std::isfinite(number)) advancelevel = number;
    break;
  case keyword::sampletriggermode:
    if (number >= 0.f && number < 7.f) sampletriggermode = static_cast<int>(number);
    break;
  case keyword::advancetriggermode:
    if (number >= 0.f && number < 7.f) advancetriggermode = static_cast<int>(number);
    break;
  case keyword::dir:
    if (number >= 0.f && number < 4.f) dir = static_cast<int>(number);
    break;
  case keyword::mode:
    if (number >= 0.f && number < 3.f) mode = static_cast<int>(number);
    break;
  case keyword::writemode:
    if (number >= 0.f && number < 3.f) writemode = static_cast<int>(number);
    break;
  case keyword::extend:
    if (number >= 0.f && number < 4.f) extend = static_cast<int>(number);
    break;
  case keyword::interp:
    if (number >= 0.f && number < 3.f) interp = static_cast<int>(number);
    break;
  case keyword::duration:
    if (number >= 0.f && number <= MAX_DURATION) duration = static_cast<unsigned int>(number);
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

void dStash::PostList(const std::string& text) {
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

void dStash::Post(int kind, float value) {
  (void)actions.try_push(action{kind, value});
}

FLOAT_IN(SetInput) {
  (void)inlet;
  (void)thread;
  input = value;
  Post(WRITE, value);
}

INT_IN(SetInputInt) {
  (void)inlet;
  (void)thread;
  input = static_cast<float>(value);
  Post(WRITE, static_cast<float>(value));
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

BANG_IN(Capture) {
  (void)inlet;
  (void)thread;
  Post(CAPTURE, 0.f);
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

BANG_IN(Advance) {
  (void)inlet;
  (void)thread;
  Post(ADVANCE, 0.f);
}

FLOAT_IN(SetWriteIndex) {
  (void)inlet;
  (void)thread;
  if (!std::isfinite(value)) return;
  writeIndex = value;
  Post(WRITE_POSITION, value);
}

INT_IN(SetWriteIndexInt) {
  (void)inlet;
  (void)thread;
  writeIndex = static_cast<float>(value);
  Post(WRITE_POSITION, static_cast<float>(value));
}

BANG_IN(AdvanceWrite) {
  (void)inlet;
  (void)thread;
  Post(ADVANCE_WRITE, 0.f);
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
      .sampleTriggerMode(static_cast<DSP::signalStash::TRIGGER_MODE>(sampletriggermode.load()))
      .advanceTriggerMode(static_cast<DSP::signalStash::TRIGGER_MODE>(advancetriggermode.load()))
      .direction(static_cast<DSP::signalStash::DIRECTION>(dir.load()))
      .mode(static_cast<DSP::signalStash::MODE>(mode.load()))
      .writeMode(static_cast<DSP::signalStash::MODE>(writemode.load()))
      .range(static_cast<DSP::signalStash::RANGE>(extend.load()))
      .interpolation(static_cast<DSP::signalStash::INTERPOLATION>(interp.load()))
      .duration(duration.load());

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

  // The ordered actions, in the order they came.
  action next{0, 0.f};
  while (actions.try_pop(next)) {
    switch (next.kind) {
    case WRITE:
      fn.write(next.value);
      break;
    case WRITE_POSITION:
      fn.writePosition(next.value);
      break;
    case ADVANCE_WRITE:
      fn.advanceWrite();
      break;
    case ADVANCE:
      fn.advance();
      break;
    case CAPTURE:
      fn.capture();
      break;
    default:
      break;
    }
  }

  // Unconnected inlets (#1036): the input and write index read their
  // numbers, the triggers 0.
  DSP::buffer& out = fn(inputs[0].Signal(input.load()), inputs[1].Signal(0.f),
                        inputs[2].Signal(0.f), inputs[3].Signal(writeIndex.load()));
  outputs[0].SendBuffer(&out, thread);
}

#undef className
