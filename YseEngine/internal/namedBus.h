/*
  ==============================================================================

    namedBus.h
    Created: 2026-05-22

    Global named-bus addressing primitive (issue #121, epic #119).

    Engine-internal only. Subscribers register against a UTF-8 string name and
    receive callbacks when any producer publishes a matching value. Producers
    on the main thread dispatch synchronously; producers on the audio thread
    enqueue into a lock-free SPSC queue that is drained from `system::update()`.

  ==============================================================================
*/

#ifndef NAMED_BUS_H_INCLUDED
#define NAMED_BUS_H_INCLUDED

#include <atomic>
#include <cstdint>
#include <cstring>
#include <functional>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

#include "../headers/enums.hpp"
#include "../utils/lfQueue.hpp"

namespace YSE {
  namespace INTERNAL {

    using BusValue = std::variant<std::monostate, int, float, std::string, std::vector<float>>;
    using SubHandle = std::uint64_t;
    using Subscriber = std::function<void(const BusValue&)>;

    class NamedBus {
    public:
      NamedBus();
      ~NamedBus();

      NamedBus(const NamedBus&) = delete;
      NamedBus& operator=(const NamedBus&) = delete;

      // Publish a value to `name`.
      //
      //   thread == T_GUI : if the caller is the control thread (the one that
      //                     constructed the bus and runs drainPending()),
      //                     dispatch synchronously to every subscriber. A T_GUI
      //                     publish arriving on ANY other thread — the gMetro
      //                     timer thread, a script thread — is instead parked in
      //                     the control-thread inbox and dispatched on the next
      //                     drainPending(). Inline dispatch off the control
      //                     thread would run a subscriber's setter, which pushes
      //                     into a per-implementation single-producer message
      //                     queue, concurrently with the control thread's own
      //                     setters — corrupting that SPSC queue (issue #193).
      //   thread == T_DSP : enqueue into this thread's lock-free SPSC queue.
      //                     Multiple render threads publish concurrently (the
      //                     audio callback thread plus every fast-pool worker
      //                     that renders a child channel), so each producing
      //                     thread owns its own queue — claimed on first use
      //                     and drained from `drainPending()`. This keeps every
      //                     queue genuinely single-producer/single-consumer
      //                     (issue #187). The queue entry has a fixed
      //                     footprint, so only int and float values are
      //                     accepted on the audio path. String / list /
      //                     monostate publishes from T_DSP are dropped silently
      //                     (no allocation, no log) — composers route
      //                     non-trivial payloads through main-thread bridges.
      //                     Names longer than `kNameCapacity` bytes are
      //                     truncated; producers must keep names short.
      void publish(const std::string& name, const BusValue& value, YSE::THREAD thread);

      // Register a subscriber for `name`. The returned handle is unique for
      // the lifetime of the bus and can be passed to `unsubscribe()`.
      SubHandle subscribe(const std::string& name, Subscriber callback);

      // Drop the subscription that owns `handle`. No-op if the handle is
      // unknown (e.g. already unsubscribed, or never issued).
      void unsubscribe(SubHandle handle);

      // Drain the audio-thread queue and dispatch each message. Must run on
      // the same thread that registers subscribers (main / update thread).
      // Called once per `system::update()` tick.
      void drainPending();

      static constexpr std::size_t kNameCapacity = 63; // null terminator follows
      static constexpr std::size_t kQueueCapacity = 1024;

    private:
      struct PooledMessage {
        enum class Kind : std::uint8_t { Int = 1, Float = 2 };
        Kind kind{Kind::Int};
        char nameStorage[kNameCapacity + 1]{};
        union {
          int i;
          float f;
        } value{};
      };

      struct Subscription {
        SubHandle handle;
        Subscriber callback;
      };

      void dispatch(const std::string& name, const BusValue& value);

      // Route this thread's T_DSP publish to its own SPSC queue, claiming a
      // slot on first use. Returns nullptr once every provisioned slot is
      // taken (more render threads than expected) — the caller then drops.
      lfQueue<PooledMessage>* producerQueue();

      // One SPSC queue per potential render-thread producer. Fixed at
      // construction (never resized) so `producerQueue()` and `drainPending()`
      // touch it without synchronisation. unique_ptr because lfQueue is
      // non-movable and needs a non-default capacity.
      std::vector<std::unique_ptr<lfQueue<PooledMessage>>> audioQueues_;
      std::atomic<std::size_t> nextSlot_{0};
      // Distinguishes this bus instance from any earlier one a surviving
      // thread may have cached a slot against — engine restart builds a fresh
      // NamedBus that may reuse the same address (issue #187).
      const std::uint64_t generation_;

      // Identity of the control thread: the thread that constructs the bus
      // (System::init runs on it) and that also runs subscribe(), dispatch()
      // and drainPending(). Set once at construction and only ever read
      // thereafter, so no synchronisation is needed — the constructing thread
      // happens-before every other thread that later publishes. A T_GUI
      // publish comparing unequal to this is off the control thread and must be
      // deferred rather than dispatched inline (issue #193).
      //
      // The identity is deliberately frozen at construction rather than derived
      // from the first drainPending() call (issue #290). Construction is the
      // earliest possible signal, and capturing it here lets a control-thread
      // T_GUI publish dispatch inline immediately — before update() has ever
      // run a drain tick (a behaviour the "control-thread T_GUI publish
      // dispatches synchronously" test pins down). Deriving it lazily would
      // force every such publish onto the deferred path until the first
      // update(). The contract this rests on — System::init() and
      // System::update() run on the same control thread — is the engine-wide
      // single-control-thread assumption, documented on system::update(). An
      // app that inits on one thread but updates on another still behaves
      // correctly (all publishes take the deferred path) but loses the inline
      // fast path.
      const std::thread::id controlThread_;

      // T_GUI publishes that arrived on a non-control thread (gMetro timer
      // thread, script thread), parked here and drained on the control thread
      // in drainPending(). Guarded by pendingMutex_. The producers are
      // control-rate threads that may lock; the audio thread never reaches this
      // path (it publishes only on T_DSP, handled lock-free above). Holding a
      // full BusValue keeps every payload kind — bang / int / float / string /
      // list — intact, unlike the fixed-footprint audio-path pool.
      std::mutex pendingMutex_;
      std::vector<std::pair<std::string, BusValue>> pendingControl_;

      mutable std::shared_mutex subsMutex_;
      std::unordered_map<std::string, std::vector<Subscription>> subs_;
      std::unordered_map<SubHandle, std::string> handleIndex_;
      std::atomic<SubHandle> nextHandle_{1};
    };

    NamedBus& Bus();

  } // namespace INTERNAL
} // namespace YSE

#endif // NAMED_BUS_H_INCLUDED
