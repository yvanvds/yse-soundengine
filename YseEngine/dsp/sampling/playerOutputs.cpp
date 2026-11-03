/*
  ==============================================================================

    playerOutputs.cpp
    The output channels of a sample buffer player (issues #1140, #1141).

  ==============================================================================
*/

#include "playerOutputs.hpp"
#include "loopCrossfade.hpp"
#include "sampleBuffer.hpp"

#include <algorithm>

using namespace YSE::DSP;

playerOutputs::playerOutputs(UInt count) {
  channels(count);
}

void playerOutputs::channels(UInt count) {
  count = std::clamp<UInt>(count, 1, MAX_CHANNELS);
  outputs.clear();
  outputs.reserve(count);
  for (UInt c = 0; c < count; ++c)
    outputs.emplace_back(YSE::STANDARD_BUFFERSIZE);
}

UInt playerOutputs::clear(UInt length) {
  length = std::min(length, YSE::STANDARD_BUFFERSIZE);
  for (buffer& out : outputs) {
    // Within the capacity set by channels(): no allocation.
    if (out.getLength() != length) out.resize(length);
    std::fill(out.getPtr(), out.getPtr() + length, 0.f);
  }
  return length;
}

void playerOutputs::read(const sampleView& view, UInt i, Dbl frame, const loopCrossfade* xfade,
                         bool forward) {
  // The crossfade weight is the same for every channel: worked out once.
  Dbl partner = 0.0;
  const Flt g = xfade != nullptr ? xfade->weight(frame, forward, partner) : 0.f;
  const auto count = static_cast<UInt>(outputs.size());
  for (UInt k = 0; k < view.channels(); ++k) {
    Flt v = view.cubic(k, frame);
    if (g > 0.f) v += g * (view.cubic(k, partner) - v);
    outputs[k % count].getPtr()[i] += v;
  }
}
