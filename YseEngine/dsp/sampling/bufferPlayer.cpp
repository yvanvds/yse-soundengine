/*
  ==============================================================================

    bufferPlayer.cpp
    Position-based playback of a sample buffer (issue #1140).

  ==============================================================================
*/

#include "bufferPlayer.hpp"
#include "loopCrossfade.hpp"
#include "sampleBuffer.hpp"

#include <algorithm>
#include <cmath>

using namespace YSE::DSP;

bufferPlayer::bufferPlayer(UInt count) : outputs(count) {}

bufferPlayer& bufferPlayer::channels(UInt count) {
  outputs.channels(count);
  return *this;
}

bufferPlayer& bufferPlayer::start(Flt startMs, Flt endMs, Flt durationMs) {
  startPoint = startMs > 0.f ? startMs : 0.f; // also for NaN
  endPoint = endMs >= 0.f ? endMs : -1.f;
  duration = durationMs > 0.f ? durationMs : 0.f;
  isPlaying = true;
  isPaused = false;
  starting = true;
  return *this;
}

bufferPlayer& bufferPlayer::stop() {
  isPlaying = false;
  isPaused = false;
  starting = false;
  return *this;
}

bufferPlayer& bufferPlayer::pause() {
  if (isPlaying) isPaused = true;
  return *this;
}

bufferPlayer& bufferPlayer::resume() {
  isPaused = false;
  return *this;
}

buffer& bufferPlayer::operator()(const sampleView& view, buffer& positionsMs) {
  finished = false;
  const UInt length = outputs.clear(positionsMs.getLength());
  if (view.empty()) return outputs.out(0);
  const Dbl toFrame = static_cast<Dbl>(view.sampleRate()) * 0.001;
  const Flt* at = positionsMs.getPtr();
  for (UInt i = 0; i < length; ++i)
    outputs.read(view, i, static_cast<Dbl>(at[i]) * toFrame, nullptr, true);
  return outputs.out(0);
}

buffer& bufferPlayer::operator()(const sampleView& view, UInt blockLength) {
  finished = false;
  const UInt length = outputs.clear(blockLength);
  if (!isPlaying || isPaused || view.empty()) return outputs.out(0);

  // The points in ms, kept inside the buffer as it is in this block.
  const Dbl rate = static_cast<Dbl>(view.sampleRate());
  const Dbl lengthMs = static_cast<Dbl>(view.frames()) * 1000.0 / rate;
  const Dbl from = std::min(static_cast<Dbl>(startPoint), lengthMs);
  const Dbl to = endPoint < 0.f ? lengthMs : std::min(static_cast<Dbl>(endPoint), lengthMs);
  if (starting) {
    cursor = from;
    starting = false;
  }
  const Dbl span = std::fabs(to - from);
  if (!(span > 0.0)) {
    // Nothing to play: an end at once.
    isPlaying = false;
    finished = true;
    return outputs.out(0);
  }
  const bool forward = to > from;

  // Milliseconds of buffer per output sample, from the live engine rate.
  const Dbl engineRate = YSE::SAMPLERATE > 0 ? static_cast<Dbl>(YSE::SAMPLERATE) : 44100.0;
  const Dbl speed = duration > 0.f ? span / static_cast<Dbl>(duration) : 1.0;
  const Dbl step = speed * 1000.0 / engineRate;
  const Dbl toFrame = rate * 0.001;

  loopCrossfade xfade;
  const loopCrossfade* fading = nullptr;
  if (looping && crossfading && fadeMs > 0.f) {
    xfade = loopCrossfade::make(from * toFrame, to * toFrame, static_cast<Dbl>(fadeMs) * toFrame);
    fading = &xfade;
  }

  for (UInt i = 0; i < length; ++i) {
    if (forward) {
      if (cursor < from) cursor = from; // the buffer grew under a held start
      if (cursor >= to) {
        if (!looping) break;
        cursor = from + std::fmod(cursor - from, span);
      }
    } else {
      if (cursor > from) cursor = from;
      if (cursor <= to) {
        if (!looping) break;
        cursor = from - std::fmod(from - cursor, span);
      }
    }
    outputs.read(view, i, cursor * toFrame, fading, forward);
    cursor += forward ? step : -step;
  }
  // Ended as the end point is reached, not a block later.
  if (!looping && (forward ? cursor >= to : cursor <= to)) {
    isPlaying = false;
    isPaused = false;
    finished = true;
  }
  return outputs.out(0);
}
