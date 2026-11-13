/*
  ==============================================================================

    stutter.cpp
    A capture buffer whose last fragment is replayed by phase (issue #1150).

  ==============================================================================
*/

#include "stutter.hpp"
#include "sampleBuffer.hpp"
#include "../generators/whiteSource.hpp"

#include <algorithm>
#include <cmath>

using namespace YSE::DSP;

namespace {

  inline UInt BlockLength(UInt length) {
    return length > YSE::STANDARD_BUFFERSIZE ? YSE::STANDARD_BUFFERSIZE : length;
  }

  inline UInt Clamp(UInt value, UInt lowest, UInt highest) {
    return value < lowest ? lowest : (value > highest ? highest : value);
  }

  // Within [0, highest]; NaN is 0.
  inline Flt Within(Flt value, Flt highest) {
    if (!(value > 0.f)) return 0.f;
    return value > highest ? highest : value;
  }

} // namespace

stutter::stutter(UInt capacity, UInt outputs)
  : record(Clamp(capacity, 1, MAX_CAPACITY), 0.f),
    play(record.size(), 0.f),
    largest(static_cast<UInt>(record.size())),
    copySize(std::min(DEFAULT_SIZE, largest)),
    random(detail::WHITE_DEFAULT_SEED) {
  const UInt count = Clamp(outputs, 1, MAX_OUTPUTS);
  outs.reserve(count);
  for (UInt k = 0; k < count; ++k)
    outs.emplace_back(YSE::STANDARD_BUFFERSIZE);
  for (buffer& b : outs)
    b = 0.f;
}

stutter& stutter::maxSize(UInt samples) {
  largest = std::min(samples, capacity());
  copySize = std::min(copySize, largest);
  return *this;
}

stutter& stutter::size(UInt samples) {
  copySize = std::min(samples, largest);
  return *this;
}

stutter& stutter::ampVariation(Flt amount) {
  ampVar = Within(amount, 1.f);
  return *this;
}

stutter& stutter::dropoutChance(Flt percent) {
  dropout = Within(percent, 100.f);
  return *this;
}

stutter& stutter::repeatChance(Flt percent) {
  repeat = Within(percent, 100.f);
  return *this;
}

stutter& stutter::clear() {
  std::fill(record.begin(), record.end(), 0.f);
  std::fill(play.begin(), play.end(), 0.f);
  fragmentLength = 0;
  copied = 0;
  return *this;
}

Flt stutter::draw() {
  // NextWhite is in [-1, 1).
  return static_cast<Flt>((detail::NextWhite(random) + 1.0) * 0.5);
}

void stutter::fire() {
  // Always three draws, so a seed repeats a run exactly.
  const bool keep = draw() * 100.f < repeat;
  muted = draw() * 100.f < dropout;
  level = 1.f - ampVar * draw();
  if (keep) return;
  // The last copySize samples recorded end just before `write`.
  const UInt cap = capacity();
  fragmentLength = copySize;
  copyFrom = (write + cap - copySize) % cap;
  copied = 0;
  copyChunk(rate == 0 ? fragmentLength : rate);
}

void stutter::copyChunk(UInt count) {
  const UInt cap = capacity();
  const UInt end = std::min(fragmentLength, copied + count);
  const Flt* from = record.data();
  Flt* to = play.data();
  // At most two runs: up to the end of the record buffer, then from its
  // start.
  while (copied < end) {
    const UInt at = (copyFrom + copied) % cap;
    const UInt run = std::min(end - copied, cap - at);
    std::copy(from + at, from + at + run, to + copied);
    copied += run;
  }
}

Flt stutter::read(Flt phase) const {
  if (!std::isfinite(phase)) return 0.f;
  const UInt len = fragmentLength;
  const Dbl wrapped = static_cast<Dbl>(phase) - std::floor(static_cast<Dbl>(phase));
  const Dbl position = wrapped * static_cast<Dbl>(len);
  auto i0 = static_cast<UInt>(position);
  if (i0 >= len) i0 = len - 1; // a fraction that rounds up to 1
  const UInt i1 = i0 + 1 == len ? 0 : i0 + 1;
  const auto frac = static_cast<Flt>(position - static_cast<Dbl>(i0));
  const Flt a = play[i0];
  return a + frac * (play[i1] - a);
}

void stutter::operator()(UInt length, signal in, signal trigger, const signal* phases) {
  length = BlockLength(length);
  const UInt count = outputs();
  for (buffer& b : outs) {
    // Within the capacity set in the constructor: no allocation.
    if (b.getLength() != length) b.resize(length);
  }
  // A copy spread over blocks goes on before this block's triggers.
  if (copied < fragmentLength) copyChunk(rate == 0 ? fragmentLength : rate);

  const UInt cap = capacity();
  Flt* rec = record.data();
  Flt* outPtr[MAX_OUTPUTS] = {};
  for (UInt k = 0; k < count; ++k)
    outPtr[k] = outs[k].getPtr();

  for (UInt i = 0; i < length; ++i) {
    rec[write] = in.at(i);
    write = write + 1 == cap ? 0 : write + 1;

    Flt t = trigger.at(i);
    if (std::isnan(t)) t = 0.f;
    const bool edge = rising ? (lastTrigger <= 0.f && t > 0.f) : (lastTrigger >= 0.f && t < 0.f);
    lastTrigger = t;
    if (edge || (i == 0 && triggerPending)) fire();
    if (i == 0) triggerPending = false;

    if (fragmentLength == 0 || muted) {
      for (UInt k = 0; k < count; ++k)
        outPtr[k][i] = 0.f;
      continue;
    }
    for (UInt k = 0; k < count; ++k)
      outPtr[k][i] = level * read(phases[k].at(i));
  }
  // An empty block leaves a trigger() pending for one with samples.
}

void stutter::operator()(buffer& in, signal trigger, signal phase) {
  signal phases[MAX_OUTPUTS];
  const UInt count = outputs();
  for (UInt k = 0; k < count; ++k)
    phases[k] = phase;
  (*this)(in.getLength(), signal(in), trigger, phases);
}

UInt stutter::copyTo(sampleWriteView& view, UInt channel, UInt frame) const {
  if (fragmentLength == 0) return 0;
  return view.write(channel, frame, play.data(), fragmentLength);
}
