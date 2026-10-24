#include "oscillatorBankControl.h"

#include <cmath>
#include <cstring>
#include "../pListArgs.h"
#include "../pSelector.h"

using namespace YSE::PATCHER;

namespace {
  // The next token of `text` from `pos`: [begin, end). Empty at the end.
  void NextToken(const char* text, std::size_t length, std::size_t pos, std::size_t& begin,
                 std::size_t& end) {
    while (pos < length && IsSelectorSeparator(text[pos]))
      pos++;
    begin = pos;
    while (pos < length && !IsSelectorSeparator(text[pos]))
      pos++;
    end = pos;
  }

  bool Is(const char* text, std::size_t begin, std::size_t end, const char* word) {
    const std::size_t length = std::strlen(word);
    return end - begin == length && std::strncmp(text + begin, word, length) == 0;
  }

  // A count from a number: negative and non-finite are 0, huge saturates.
  UInt ToCount(float value) {
    if (!std::isfinite(value) || value <= 0.f) return 0;
    if (value >= 4294967040.f) return 0xFFFFFF00u;
    return static_cast<UInt>(value);
  }
} // namespace

oscillatorBankControl::oscillatorBankControl(bool tabpoints)
  : tabpoints(tabpoints), frequency(0.f), magnitude(0.f), index(0.f) {
  Build(DSP::oscillatorBank::DEFAULT_OSCILLATORS);
}

void oscillatorBankControl::Build(UInt bankCapacity) {
  capacity = bankCapacity < 1 ? 1 : bankCapacity;
  // A `set` for every oscillator is capacity + 1 events; room for two.
  UInt size = 256;
  while (size < 2 * (capacity + 1))
    size <<= 1;
  ring.assign(size, event{});
  mask = size - 1;
  head.store(0, std::memory_order_relaxed);
  tail.store(0, std::memory_order_relaxed);
}

int oscillatorBankControl::Referenced() const {
  const float at = index.load(std::memory_order_relaxed);
  if (!std::isfinite(at) || at <= -1.f || at >= static_cast<float>(capacity)) return -1;
  return at < 0.f ? 0 : static_cast<int>(at);
}

bool oscillatorBankControl::Room(UInt count) const {
  const UInt used = tail.load(std::memory_order_relaxed) - head.load(std::memory_order_acquire);
  return static_cast<UInt>(ring.size()) - used >= count;
}

bool oscillatorBankControl::Push(kind what, UInt target, Flt a, Flt b) {
  if (writing.test_and_set(std::memory_order_acquire)) {
    dropped.fetch_add(1, std::memory_order_relaxed);
    return false;
  }
  const bool fits = Room(1);
  if (fits) {
    const UInt at = tail.load(std::memory_order_relaxed);
    ring[at & mask] = event{what, target, a, b};
    tail.store(at + 1, std::memory_order_release);
  } else {
    dropped.fetch_add(1, std::memory_order_relaxed);
  }
  writing.clear(std::memory_order_release);
  return fits;
}

void oscillatorBankControl::Frequency(Flt value) {
  frequency.store(value, std::memory_order_relaxed);
  const int at = Referenced();
  if (at >= 0) Push(kind::FREQUENCY, static_cast<UInt>(at), value);
}

void oscillatorBankControl::Magnitude(Flt value) {
  magnitude.store(value, std::memory_order_relaxed);
  const int at = Referenced();
  if (at >= 0) Push(kind::MAGNITUDE, static_cast<UInt>(at), value);
}

void oscillatorBankControl::Phase(Flt value) {
  const int at = Referenced();
  if (at >= 0) Push(kind::PHASE, static_cast<UInt>(at), value);
}

void oscillatorBankControl::Index(Flt value) {
  index.store(value, std::memory_order_relaxed);
}

// `set f m f m ...`: one PAIR per pair (up to the capacity) and a FADE_FROM,
// all or nothing. Numbers are read in place, twice: once to count, once to
// write, so the request is refused whole rather than half applied.
void oscillatorBankControl::PushSet(const char* text, std::size_t from, std::size_t length) {
  UInt numbers = 0;
  std::size_t begin = 0;
  std::size_t end = from;
  for (;;) {
    NextToken(text, length, end, begin, end);
    if (begin == end) break;
    float number = 0.f;
    if (ReadNumericToken(text + begin, end - begin, number)) numbers++;
  }
  UInt pairs = numbers / 2;
  if (pairs > capacity) pairs = capacity;

  if (writing.test_and_set(std::memory_order_acquire)) {
    dropped.fetch_add(1, std::memory_order_relaxed);
    return;
  }
  if (!Room(pairs + 1)) {
    dropped.fetch_add(1, std::memory_order_relaxed);
    writing.clear(std::memory_order_release);
    return;
  }
  UInt at = tail.load(std::memory_order_relaxed);
  UInt written = 0;
  bool haveFrequency = false;
  float pending = 0.f;
  end = from;
  while (written < pairs) {
    NextToken(text, length, end, begin, end);
    if (begin == end) break;
    float number = 0.f;
    if (!ReadNumericToken(text + begin, end - begin, number)) continue;
    if (!haveFrequency) {
      pending = number;
      haveFrequency = true;
      continue;
    }
    ring[at & mask] = event{kind::PAIR, written, pending, number};
    at++;
    written++;
    haveFrequency = false;
  }
  ring[at & mask] = event{kind::FADE_FROM, written};
  tail.store(at + 1, std::memory_order_release);
  writing.clear(std::memory_order_release);
}

void oscillatorBankControl::Message(const std::string& message) {
  const char* text = message.c_str();
  const std::size_t length = message.size();
  std::size_t begin = 0;
  std::size_t end = 0;
  NextToken(text, length, 0, begin, end);

  if (Is(text, begin, end, "set")) {
    PushSet(text, end, length);
    return;
  }
  if (Is(text, begin, end, "clear")) {
    Push(kind::CLEAR, 0);
    return;
  }
  if (Is(text, begin, end, "silence")) {
    Push(kind::SILENCE, 0);
    return;
  }

  kind what = kind::SIZE;
  if (Is(text, begin, end, "size")) {
    what = kind::SIZE;
  } else if (Is(text, begin, end, "freqsmooth")) {
    what = kind::FREQUENCY_SMOOTHING;
  } else if (Is(text, begin, end, "magsmooth")) {
    what = kind::MAGNITUDE_SMOOTHING;
  } else if (Is(text, begin, end, "framesync")) {
    what = kind::FRAME_SYNC;
  } else if (tabpoints && Is(text, begin, end, "tabpoints")) {
    what = kind::TABLE_SIZE;
  } else {
    return;
  }
  std::size_t arg = 0;
  std::size_t argEnd = 0;
  NextToken(text, length, end, arg, argEnd);
  float number = 0.f;
  if (!ReadNumericToken(text + arg, argEnd - arg, number)) return;
  if (what == kind::FRAME_SYNC) {
    Push(what, number != 0.f ? 1u : 0u);
    return;
  }
  Push(what, ToCount(number));
}

void oscillatorBankControl::Apply(DSP::oscillatorBank& bank, bool signals) {
  UInt at = head.load(std::memory_order_relaxed);
  const UInt last = tail.load(std::memory_order_acquire);
  for (; at != last; ++at) {
    const event& e = ring[at & mask];
    switch (e.what) {
    case kind::FREQUENCY:
      if (!signals) bank.setFrequency(e.target, e.a);
      break;
    case kind::MAGNITUDE:
      if (!signals) bank.setMagnitude(e.target, e.a);
      break;
    case kind::PHASE:
      // Max: the phase inlet sets a phase only with frame sync on.
      if (!signals && bank.frameSync()) bank.setPhase(e.target, e.a);
      break;
    case kind::PAIR:
      bank.set(e.target, e.a, e.b);
      break;
    case kind::FADE_FROM:
      bank.fadeFrom(e.target);
      break;
    case kind::CLEAR:
      bank.clear();
      break;
    case kind::SILENCE:
      bank.silence();
      break;
    case kind::SIZE:
      bank.size(e.target);
      break;
    case kind::FREQUENCY_SMOOTHING:
      bank.frequencySmoothing(e.target);
      break;
    case kind::MAGNITUDE_SMOOTHING:
      bank.magnitudeSmoothing(e.target);
      break;
    case kind::FRAME_SYNC:
      bank.frameSync(e.target != 0);
      break;
    case kind::TABLE_SIZE:
      bank.tableSize(e.target);
      break;
    }
  }
  head.store(last, std::memory_order_release);
}
