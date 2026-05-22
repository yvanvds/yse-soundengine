/*
  ==============================================================================

    global.cpp
    Created: 27 Jan 2014 10:18:28pm
    Author:  yvan

  ==============================================================================
*/

#include "../internalHeaders.h"
#include "namedBus.h"


YSE::INTERNAL::global & YSE::INTERNAL::Global() {
    static global s;
    return s;
}

void YSE::INTERNAL::global::addSlowJob(threadPoolJob * job) {
  slowThreads.addJob(job);
}

void YSE::INTERNAL::global::addFastJob(threadPoolJob * job) {
  fastThreads.addJob(job);
}

YSE::INTERNAL::NamedBus& YSE::INTERNAL::global::namedBus() {
  // Tied to System::init / System::close lifecycle — callers must respect
  // that contract. Asserting (rather than lazy-creating) keeps the
  // "no persistence across init/close" guarantee from issue #121 honest.
  assert(bus && "INTERNAL::Global().namedBus() accessed outside of an active engine session");
  return *bus;
}

YSE::INTERNAL::global::global() : slowThreads(1), fastThreads(), bus(), update(false), active(false), sampleRateLocked(false) {}

YSE::INTERNAL::global::~global() = default;

void YSE::INTERNAL::global::init() {
  REVERB::Manager().create();
  bus = std::make_unique<NamedBus>();
}

void YSE::INTERNAL::global::close() {
  // first wait for all threads to exit
  slowThreads.shutdown();
  fastThreads.shutdown();
  // Tear down the bus last — by this point the audio device is already
  // closed (system::close() ran DEVICE::Manager().close() before us), so
  // no audio-thread producer can still be enqueuing publish() messages.
  bus.reset();
}
