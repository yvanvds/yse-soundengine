/*
  ==============================================================================

    groovePlayer.cpp
    Variable-rate looping playback of a sample buffer (issue #1141).

  ==============================================================================
*/

#include "groovePlayer.hpp"
#include "loopCrossfade.hpp"
#include "sampleBuffer.hpp"
#include "sampleRegion.hpp"

#include <algorithm>
#include <cmath>

using namespace YSE::DSP;

groovePlayer::groovePlayer(UInt count) : outputs(count), syncOut(YSE::STANDARD_BUFFERSIZE) {}

groovePlayer& groovePlayer::channels(UInt count) {
  outputs.channels(count);
  return *this;
}

groovePlayer& groovePlayer::position(Flt ms) {
  cursor = ms > 0.f ? static_cast<Dbl>(ms) : 0.0; // also for NaN
  jump = JUMP::NONE;
  direction = 1.0;
  isPlaying = true;
  return *this;
}

groovePlayer& groovePlayer::startLoop() {
  jump = JUMP::LOOP_START;
  direction = 1.0;
  isPlaying = true;
  return *this;
}

groovePlayer& groovePlayer::endLoop() {
  jump = JUMP::LOOP_END;
  direction = 1.0;
  isPlaying = true;
  return *this;
}

groovePlayer& groovePlayer::stop() {
  isPlaying = false;
  return *this;
}

buffer& groovePlayer::operator()(const sampleView& view, buffer& speed) {
  Process(view, speed.getPtr(), 0.f, speed.getLength());
  return outputs.out(0);
}

buffer& groovePlayer::operator()(const sampleView& view, Flt speed, UInt length) {
  Process(view, nullptr, speed, length);
  return outputs.out(0);
}

buffer& groovePlayer::phase(const sampleView& view, buffer& phases) {
  ProcessPhase(view, phases.getPtr(), 0.f, phases.getLength());
  return outputs.out(0);
}

buffer& groovePlayer::phase(const sampleView& view, Flt at, UInt length) {
  ProcessPhase(view, nullptr, at, length);
  return outputs.out(0);
}

UInt groovePlayer::Begin(UInt blockLength) {
  const UInt length = outputs.clear(blockLength);
  // Within the capacity set at construction: no allocation.
  if (syncOut.getLength() != length) syncOut.resize(length);
  std::fill(syncOut.getPtr(), syncOut.getPtr() + length, 0.f);
  return length;
}

void groovePlayer::Wrap(Dbl& frame, Dbl step, Dbl low, Dbl high) {
  const Dbl span = high - low;
  if (loopMode == GROOVE_LOOP::ON) {
    // Jump to the other point by as much as the position overshot.
    if (step >= 0.0 && frame >= high) {
      frame = low + std::fmod(frame - high, span);
    } else if (step < 0.0 && frame < low) {
      frame = high - std::fmod(low - frame, span);
    }
    return;
  }
  // PENDULUM: reflect off the point, and turn round, once per lap of two
  // loop lengths the position overshot by.
  if (step > 0.0 && frame >= high) {
    const Dbl over = std::fmod(frame - high, 2.0 * span);
    if (over <= span) {
      frame = high - over;
      direction = -direction;
    } else {
      frame = low + (over - span);
    }
  } else if (step < 0.0 && frame < low) {
    const Dbl under = std::fmod(low - frame, 2.0 * span);
    if (under <= span) {
      frame = low + under;
      direction = -direction;
    } else {
      frame = high - (under - span);
    }
  }
}

void groovePlayer::Process(const sampleView& view, const Flt* speeds, Flt constant,
                           UInt blockLength) {
  const UInt length = Begin(blockLength);
  Flt* sync = syncOut.getPtr();
  if (view.empty()) return;

  // The loop in frames of the buffer as it is in this block.
  const Flt rate = view.sampleRate();
  const sampleRegion region = sampleRegion::fromMs(loopStartMs, loopEndMs, rate, view.frames());
  const auto low = static_cast<Dbl>(region.start);
  const auto high = static_cast<Dbl>(region.end);
  const Dbl toFrame = static_cast<Dbl>(rate) * 0.001;
  if (jump == JUMP::LOOP_START) cursor = low / toFrame;
  if (jump == JUMP::LOOP_END) cursor = high / toFrame;
  jump = JUMP::NONE;
  if (!isPlaying) return;

  // Kept inside the buffer as it is now.
  const auto last = static_cast<Dbl>(view.frames());
  Dbl frame = cursor * toFrame;
  frame = frame > 0.0 ? (frame < last ? frame : last) : 0.0;

  // Frames of buffer per output sample at speed 1, from the live engine rate.
  const Dbl engineRate = YSE::SAMPLERATE > 0 ? static_cast<Dbl>(YSE::SAMPLERATE) : 44100.0;
  const Dbl ratio = static_cast<Dbl>(rate) / engineRate;

  const bool looping = loopMode != GROOVE_LOOP::OFF && !region.empty();
  loopCrossfade xfade;
  const loopCrossfade* fading = nullptr;
  if (looping && loopMode == GROOVE_LOOP::ON && crossfading && fadeMs > 0.f) {
    xfade = loopCrossfade::make(low, high, static_cast<Dbl>(fadeMs) * toFrame);
    fading = &xfade;
  }

  Dbl step = 0.0;
  for (UInt i = 0; i < length; ++i) {
    Flt s = speeds != nullptr ? speeds[i] : constant;
    if (std::isnan(s)) s = 0.f;
    step = static_cast<Dbl>(s) * ratio * direction;
    if (looping) {
      Wrap(frame, step, low, high);
      step = static_cast<Dbl>(s) * ratio * direction; // a pendulum may have turned
    } else if (OffTheEnd(frame, step, last)) {
      break;
    }
    // The loop, ends included: a pendulum turns on its end point.
    const bool inLoop = !region.empty() && frame >= low && frame <= high;
    outputs.read(view, i, frame, inLoop ? fading : nullptr, step >= 0.0);
    if (inLoop) sync[i] = region.phase(frame);
    frame += step;
  }
  // Stopped as the end is passed, not a block later.
  if (!looping && isPlaying) OffTheEnd(frame, step, last);
  cursor = frame / toFrame;
}

void groovePlayer::ProcessPhase(const sampleView& view, const Flt* phases, Flt constant,
                                UInt blockLength) {
  const UInt length = Begin(blockLength);
  Flt* sync = syncOut.getPtr();
  jump = JUMP::NONE; // the phase says where it reads
  if (view.empty() || !isPlaying) return;

  // The loop in frames of the buffer as it is in this block.
  const Flt rate = view.sampleRate();
  const sampleRegion region = sampleRegion::fromMs(loopStartMs, loopEndMs, rate, view.frames());
  if (region.empty()) return;
  const auto low = static_cast<Dbl>(region.start);
  const auto high = static_cast<Dbl>(region.end);
  const Dbl toFrame = static_cast<Dbl>(rate) * 0.001;

  loopCrossfade xfade;
  const loopCrossfade* fading = nullptr;
  if (crossfading && fadeMs > 0.f) {
    xfade = loopCrossfade::make(low, high, static_cast<Dbl>(fadeMs) * toFrame);
    fading = &xfade;
  }

  Dbl frame = cursor * toFrame;
  for (UInt i = 0; i < length; ++i) {
    const auto raw = static_cast<Dbl>(phases != nullptr ? phases[i] : constant);
    Dbl at = lastPhase;
    if (std::isfinite(raw)) at = raw >= 0.0 && raw <= 1.0 ? raw : raw - std::floor(raw);

    // The short way round: a ramp wrapping from 1 to 0 still rises.
    Dbl moved = at - lastPhase;
    if (moved > 0.5) moved -= 1.0;
    if (moved < -0.5) moved += 1.0;
    if (moved != 0.0) phaseFalling = moved < 0.0;
    lastPhase = at;

    frame = low + at * (high - low);
    outputs.read(view, i, frame, fading, !phaseFalling);
    sync[i] = static_cast<Flt>(at);
  }
  cursor = frame / toFrame;
}

bool groovePlayer::OffTheEnd(Dbl& frame, Dbl step, Dbl last) {
  // Forward it plays [0, last), backward [0, last].
  if (!((step > 0.0 && frame >= last) || (step < 0.0 && frame < 0.0))) return false;
  frame = frame < 0.0 ? 0.0 : last;
  isPlaying = false; // until the next jump
  return true;
}
