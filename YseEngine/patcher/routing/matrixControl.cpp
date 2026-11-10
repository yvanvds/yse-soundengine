#include "matrixControl.h"
#include "../pListArgs.h"
#include "../pSelector.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string_view>

using namespace YSE::PATCHER;

namespace {
  // The token starting at or after `from` in `text`, walked in place so
  // nothing allocates on whichever thread delivers the message. `next` is
  // set to where the token ends. Empty when there is none.
  std::string_view Token(const std::string& text, std::size_t from, std::size_t& next) {
    const char* chars = text.c_str();
    const std::size_t size = text.size();
    std::size_t begin = from;
    while (begin < size && IsSelectorSeparator(chars[begin]))
      begin++;
    std::size_t end = begin;
    while (end < size && !IsSelectorSeparator(chars[end]))
      end++;
    next = end;
    return std::string_view(chars + begin, end - begin);
  }

  bool ReadNumber(std::string_view token, float& out) {
    return !token.empty() && ReadNumericToken(token.data(), token.size(), out) &&
           std::isfinite(out);
  }

  // A port number: a number from 0 up, truncated.
  bool ReadPort(std::string_view token, UInt& out) {
    float value = 0.f;
    if (!ReadNumber(token, value) || value < 0.f) return false;
    // Far beyond any port: the cast stays in range, and the port is ignored.
    out = static_cast<UInt>(std::min(value, 1048576.f));
    return true;
  }

  std::uint64_t Pack(Flt gain, Flt rampMs) {
    std::uint32_t g = 0;
    std::uint32_t r = 0;
    std::memcpy(&g, &gain, sizeof(g));
    std::memcpy(&r, &rampMs, sizeof(r));
    return (static_cast<std::uint64_t>(r) << 32U) | g;
  }

  Flt UnpackGain(std::uint64_t word) {
    const auto g = static_cast<std::uint32_t>(word & 0xFFFFFFFFULL);
    Flt gain = 0.f;
    std::memcpy(&gain, &g, sizeof(gain));
    return gain;
  }

  Flt UnpackRamp(std::uint64_t word) {
    const auto r = static_cast<std::uint32_t>(word >> 32U);
    Flt ramp = 0.f;
    std::memcpy(&ramp, &r, sizeof(ramp));
    return ramp;
  }

  Int ClampExclusive(float value) {
    if (!(value > 0.f)) return YSE::DSP::mixMatrix::EXCLUSIVE_OFF;
    if (value >= static_cast<float>(YSE::DSP::mixMatrix::EXCLUSIVE_BOTH))
      return YSE::DSP::mixMatrix::EXCLUSIVE_BOTH;
    return static_cast<Int>(value);
  }

  // The keywords of the arguments and messages. `ramptime` is ramp's alias.
  enum class keyword { none, ramp, exclusive };

  keyword ToKeyword(std::string_view word) {
    if (word == "ramp" || word == "ramptime") return keyword::ramp;
    if (word == "exclusive") return keyword::exclusive;
    return keyword::none;
  }

  constexpr std::size_t BITS = 64;
} // namespace

matrixControl::shape matrixControl::Parse(const std::vector<std::string>& arguments) {
  shape result;
  ramp.store(DSP::mixMatrix::DEFAULT_RAMP, std::memory_order_relaxed);
  exclusive.store(DSP::mixMatrix::EXCLUSIVE_OFF, std::memory_order_relaxed);
  dumps.store(DUMP_NONE, std::memory_order_relaxed);

  float numbers[3] = {static_cast<float>(DSP::mixMatrix::DEFAULT_CHANNELS),
                      static_cast<float>(DSP::mixMatrix::DEFAULT_CHANNELS), 1.f};
  unsigned int position = 0;
  for (std::size_t i = 0; i < arguments.size(); ++i) {
    float number = 0.f;
    const keyword word = ToKeyword(arguments[i]);
    // A keyword takes the number after it; a keyword with none is skipped.
    if (word != keyword::none) {
      if (i + 1 < arguments.size() && ReadNumber(arguments[i + 1], number)) {
        ++i;
        if (word == keyword::ramp)
          ramp.store(number < 0.f ? 0.f : number, std::memory_order_relaxed);
        else
          exclusive.store(ClampExclusive(number), std::memory_order_relaxed);
      }
      continue;
    }
    if (position < 3 && ReadNumber(arguments[i], number)) numbers[position++] = number;
  }

  const auto count = [](float value) {
    const float rounded = std::round(value);
    return rounded < 1.f ? 1u
                         : static_cast<UInt>(std::fmin(
                               rounded, static_cast<float>(DSP::mixMatrix::MAX_CHANNELS)));
  };
  result.inputs = count(numbers[0]);
  result.outputs = count(numbers[1]);
  result.defaultGain = numbers[2];
  defaultGain.store(result.defaultGain, std::memory_order_relaxed);

  inputs = result.inputs;
  outputs = result.outputs;
  const std::size_t size = static_cast<std::size_t>(inputs) * outputs;
  cells = std::make_unique<std::atomic<std::uint64_t>[]>(size);
  dirtyWords = (size + BITS - 1) / BITS;
  dirty = std::make_unique<std::atomic<std::uint64_t>[]>(dirtyWords);
  for (std::size_t i = 0; i < size; ++i)
    cells[i].store(Pack(0.f, 0.f), std::memory_order_relaxed);
  for (std::size_t i = 0; i < dirtyWords; ++i)
    dirty[i].store(0, std::memory_order_relaxed);
  anyDirty.store(false, std::memory_order_relaxed);
  return result;
}

Flt matrixControl::Target(UInt in, UInt out) const {
  if (in >= inputs || out >= outputs) return 0.f;
  return UnpackGain(cells[Index(in, out)].load(std::memory_order_relaxed));
}

void matrixControl::Write(UInt in, UInt out, Flt gain, Flt rampMs) {
  const std::size_t index = Index(in, out);
  cells[index].store(Pack(gain, rampMs), std::memory_order_relaxed);
  // Release: the audio thread that takes the bit sees the word.
  dirty[index / BITS].fetch_or(std::uint64_t{1} << (index % BITS), std::memory_order_release);
  anyDirty.store(true, std::memory_order_release);
}

void matrixControl::Set(UInt in, UInt out, Flt gain, Flt rampMs) {
  if (in >= inputs || out >= outputs || !std::isfinite(gain)) return;
  if (gain != 0.f) {
    DSP::mixMatrix::Exclusions(Exclusive(), in, out, inputs, outputs,
                               [this, rampMs](UInt i, UInt o) {
                                 if (Target(i, o) != 0.f) Write(i, o, 0.f, rampMs);
                               });
  }
  Write(in, out, gain, rampMs);
}

bool matrixControl::List(const std::string& list) {
  std::size_t next = 0;
  float values[4] = {0.f, 0.f, 0.f, -1.f};
  std::size_t count = 0;
  for (std::string_view token = Token(list, 0, next); !token.empty();
       token = Token(list, next, next)) {
    if (count == 4 || !ReadNumber(token, values[count])) return false;
    ++count;
  }
  if (count < 3 || values[0] < 0.f || values[1] < 0.f) return false;
  // Far beyond any port: the casts stay in range, and the port is ignored.
  const auto in = static_cast<UInt>(std::min(values[0], 1048576.f));
  const auto out = static_cast<UInt>(std::min(values[1], 1048576.f));
  const Flt rampMs = count == 4 ? std::max(values[3], 0.f) : Ramp();
  Set(in, out, values[2], rampMs);
  return true;
}

bool matrixControl::Message(const std::string& message) {
  std::size_t next = 0;
  const std::string_view word = Token(message, 0, next);
  float value = 0.f;

  // A list that starts with a number.
  if (ReadNumber(word, value)) return List(message);

  if (word == "connect" || word == "disconnect") {
    // connect <in> <out> [<out> ...] [<float gain>]: a float as the last item
    // is the gain for every outlet named, else the default gain.
    const bool connecting = word == "connect";
    UInt in = 0;
    if (!ReadPort(Token(message, next, next), in)) return true;
    const std::size_t first = next;
    Flt gain = connecting ? defaultGain.load(std::memory_order_relaxed) : 0.f;
    std::string_view last;
    for (std::string_view t = Token(message, next, next); !t.empty();
         t = Token(message, next, next))
      last = t;
    const bool lastIsGain =
        connecting && !last.empty() && TokenLooksLikeFloat(last.data(), last.size());
    if (lastIsGain && !ReadNumber(last, gain)) return true;

    next = first;
    std::string_view t = Token(message, next, next);
    while (!t.empty()) {
      const std::string_view following = Token(message, next, next);
      // The gain is the last item; it names no outlet.
      if (following.empty() && lastIsGain) break;
      UInt out = 0;
      if (ReadPort(t, out)) Set(in, out, gain, Ramp());
      t = following;
    }
    return true;
  }

  if (word == "clear") {
    const Flt rampMs = Ramp();
    for (UInt in = 0; in < inputs; ++in) {
      for (UInt out = 0; out < outputs; ++out) {
        if (Target(in, out) != 0.f) Write(in, out, 0.f, rampMs);
      }
    }
    return true;
  }

  const keyword attribute = ToKeyword(word);
  if (attribute == keyword::ramp) {
    if (ReadNumber(Token(message, next, next), value))
      ramp.store(value < 0.f ? 0.f : value, std::memory_order_relaxed);
    return true;
  }
  if (attribute == keyword::exclusive) {
    if (ReadNumber(Token(message, next, next), value))
      exclusive.store(ClampExclusive(value), std::memory_order_relaxed);
    return true;
  }

  if (word == "dump") {
    dumps.fetch_or(DUMP_CURRENT, std::memory_order_acq_rel);
    return true;
  }
  // The connections are the gains the crosspoints are heading for.
  if (word == "dumptarget" || word == "dumpconnections") {
    dumps.fetch_or(DUMP_TARGET, std::memory_order_acq_rel);
    return true;
  }
  // There is no console to print to, and a dictionary named in a message is
  // not read: both are accepted and do nothing (see dMatrix.h).
  if (word == "print" || word == "dictionary") return true;
  return false;
}

void matrixControl::Apply(DSP::mixMatrix& fn) {
  if (!anyDirty.exchange(false, std::memory_order_acquire)) return;
  for (std::size_t w = 0; w < dirtyWords; ++w) {
    std::uint64_t bits = dirty[w].exchange(0, std::memory_order_acquire);
    for (std::size_t bit = 0; bits != 0; ++bit, bits >>= 1U) {
      if ((bits & 1U) == 0) continue;
      const std::size_t index = (w * BITS) + bit;
      const std::uint64_t word = cells[index].load(std::memory_order_relaxed);
      fn.set(static_cast<UInt>(index / outputs), static_cast<UInt>(index % outputs),
             UnpackGain(word), UnpackRamp(word));
    }
  }
}
