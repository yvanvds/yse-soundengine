/*
  ==============================================================================

    global.cpp
    Created: 27 Jan 2014 10:18:28pm
    Author:  yvan

  ==============================================================================
*/

#include "../internalHeaders.h"
#include "namedBus.h"

#if YSE_ENABLE_PYTHON
#include "../python/scriptRuntime.h"
#include "../python/dsl_runtime.h"
#include <atomic>
#include <memory>
namespace {
  // Process-global script runtime, owned here rather than as a global:: member
  // so global.h stays free of any Python type and its layout never depends on
  // YSE_ENABLE_PYTHON (which would otherwise be an ODR hazard between engine
  // and test translation units).
  std::unique_ptr<YSE::INTERNAL::ScriptRuntime> g_scriptRuntime;

  // Error sink the C API (issue #125) installs. Stored as atomics so the host
  // thread can swap it while the main thread is mid-dispatch in
  // drainScriptResults() — the project's lock-free callback-bridge convention
  // (see yse_c_internal.hpp). user_data is published before the function
  // pointer (release) and read after it (acquire) so a dispatch never sees a
  // half-installed pair.
  std::atomic<YSE::INTERNAL::global::ScriptErrorSink> g_scriptErrorSink{nullptr};
  std::atomic<void*>                                  g_scriptErrorUser{nullptr};
}
#endif


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

void YSE::INTERNAL::global::startScripting() {
#if YSE_ENABLE_PYTHON
  // Reset the DSL tick/generation for a fresh session before the worker
  // launches (yse.tick starts at 0 on every System::init).
  dsl::reset();
  if (!g_scriptRuntime) {
    g_scriptRuntime = std::make_unique<ScriptRuntime>();
  }
  g_scriptRuntime->start();
#endif
}

void YSE::INTERNAL::global::wakeScripting() {
#if YSE_ENABLE_PYTHON
  // Advance the DSL tick once per update before waking the worker so the
  // script thread sees the new tick when it processes schedules.
  dsl::advanceTick();
  if (g_scriptRuntime) g_scriptRuntime->wake();
#endif
}

void YSE::INTERNAL::global::stopScripting() {
#if YSE_ENABLE_PYTHON
  if (g_scriptRuntime) {
    g_scriptRuntime->stop();
    g_scriptRuntime.reset();
  }
#endif
}

void YSE::INTERNAL::global::setScriptErrorSink(ScriptErrorSink sink, void* userdata) {
#if YSE_ENABLE_PYTHON
  // Publish user_data first, then the function pointer, both with release
  // ordering — mirrors the acquire loads in drainScriptResults().
  g_scriptErrorUser.store(userdata, std::memory_order_release);
  g_scriptErrorSink.store(sink, std::memory_order_release);
#else
  (void)sink;
  (void)userdata;
#endif
}

void YSE::INTERNAL::global::pushScript(std::string source) {
#if YSE_ENABLE_PYTHON
  if (g_scriptRuntime) g_scriptRuntime->pushEval(std::move(source));
#else
  (void)source;
#endif
}

void YSE::INTERNAL::global::drainScriptResults() {
#if YSE_ENABLE_PYTHON
  if (!g_scriptRuntime) return;
  EvalResult result;
  while (g_scriptRuntime->tryPopResult(result)) {
    if (result.status != EvalStatus::Error) continue;
    // Load the function pointer first, bail if cleared, then its user_data —
    // acquire ordering pairs with the releases in setScriptErrorSink(). Re-read
    // per result so a swap between two drained errors takes effect immediately
    // and the previous sink never receives a later traceback.
    ScriptErrorSink sink = g_scriptErrorSink.load(std::memory_order_acquire);
    if (sink == nullptr) continue;
    void* user = g_scriptErrorUser.load(std::memory_order_acquire);
    // result.traceback outlives the call; the pointer is valid only for its
    // duration (documented contract in yse_python.h).
    sink(result.traceback.c_str(), user);
  }
#endif
}

YSE::INTERNAL::global::global() : slowThreads(1), fastThreads(), bus(), update(false), active(false), sampleRateLocked(false) {}

YSE::INTERNAL::global::~global() = default;

void YSE::INTERNAL::global::init() {
  // Revive the worker pools first: close() joins them for good, so a second
  // session would otherwise start with dead pools — no slow-pool file loading,
  // no fast-pool DSP fan-out or manager setup/delete jobs (issue #140). No-op
  // on the very first session, where the pools are still live from the ctor.
  slowThreads.startup();
  fastThreads.startup();
  REVERB::Manager().create();
  bus = std::make_unique<NamedBus>();
}

void YSE::INTERNAL::global::close() {
  // first wait for all threads to exit
  slowThreads.shutdown();
  fastThreads.shutdown();
  // Threads are joined now, so the reverb manager's session state can be torn
  // down synchronously. This clears the global reverb's implementation handle
  // so a subsequent System::init() can re-create it instead of asserting
  // (issue #132).
  REVERB::Manager().destroy();
  // Tear down the bus last — by this point the audio device is already
  // closed (system::close() ran DEVICE::Manager().close() before us), so
  // no audio-thread producer can still be enqueuing publish() messages.
  bus.reset();
}
