/*
  ==============================================================================

    domainClock.cpp
    Created for issue #249 — domain clocks.

  ==============================================================================
*/

#include "../internalHeaders.h"

// The published beat position must be readable from the control/UI thread
// while the audio thread writes it, so it has to be a genuinely lock-free
// atomic — a spinlock on the audio thread would violate RT discipline. All
// supported engine targets are 64-bit (Windows/Linux x64, Android arm64-v8a +
// x86_64), where an 8-byte atomic is always lock-free.
static_assert(std::atomic<Dbl>::is_always_lock_free,
              "domain clocks need a lock-free atomic<double> for cross-thread beat reads");

YSE::CLOCK::domainClock::domainClock(const std::string& name, Flt initialTempo)
  : name(name),
    beat(0.0),
    tempo(initialTempo),
    tempoTarget(initialTempo),
    tempoRate(0.0),
    ramping(false),
    beatOut(0.0),
    tempoOut(initialTempo),
    reqTempo(0.f),
    reqRamp(0.f),
    reqDirty(false),
    released(false),
    objectStatus(OBJECT_READY) {}

void YSE::CLOCK::domainClock::requestTempo(Flt bpm, Flt rampSeconds) {
  reqTempo.store(bpm, std::memory_order_relaxed);
  reqRamp.store(rampSeconds < 0.f ? 0.f : rampSeconds, std::memory_order_relaxed);
  // Publish the two operands before flagging the request so the audio thread,
  // which acquires on the flag, observes both.
  reqDirty.store(true, std::memory_order_release);
}

bool YSE::CLOCK::domainClock::update(Flt delta) {
  if (released.load(std::memory_order_acquire)) return false;

  // Consume a pending setTempo. The acquire on the flag pairs with the release
  // in requestTempo so the operands below are visible.
  if (reqDirty.exchange(false, std::memory_order_acquire)) {
    const Dbl target = static_cast<Dbl>(reqTempo.load(std::memory_order_relaxed));
    const Dbl ramp = static_cast<Dbl>(reqRamp.load(std::memory_order_relaxed));
    tempoTarget = target;
    if (ramp > 0.0) {
      tempoRate = (target - tempo) / ramp;
      ramping = (tempoRate != 0.0);
    } else {
      tempo = target;
      tempoRate = 0.0;
      ramping = false;
    }
  }

  const Dbl tempoBefore = tempo;
  if (ramping) {
    tempo += tempoRate * static_cast<Dbl>(delta);
    // Stop exactly on the target when this block crosses it.
    if ((tempoRate > 0.0 && tempo >= tempoTarget) || (tempoRate < 0.0 && tempo <= tempoTarget)) {
      tempo = tempoTarget;
      tempoRate = 0.0;
      ramping = false;
    }
  }
  const Dbl tempoAfter = tempo;

  // Beat is the integral of tempo/60 over the block. Tempo is piecewise-linear
  // within a block, so the exact integral is the midpoint (average) tempo.
  beat += static_cast<Dbl>(delta) * (tempoBefore + tempoAfter) * 0.5 / 60.0;

  beatOut.store(beat, std::memory_order_release);
  tempoOut.store(static_cast<Flt>(tempoAfter), std::memory_order_release);
  return true;
}
