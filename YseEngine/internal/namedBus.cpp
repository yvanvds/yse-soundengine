/*
  ==============================================================================

    namedBus.cpp
    Created: 2026-05-22

  ==============================================================================
*/

#include "../internalHeaders.h"
#include "namedBus.h"

#include <algorithm>
#include <mutex>
#include <thread>
#include <utility>

namespace YSE {
  namespace INTERNAL {

    namespace {
      // Monotonic across the whole process so every NamedBus instance gets a
      // distinct stamp even when a new one is allocated at the address a freed
      // one used (engine restart). Paired with the thread_local slot cache in
      // producerQueue().
      std::atomic<std::uint64_t> g_busGeneration{0};

      // Tap handles (issue #389) draw from a process-global counter rather
      // than a per-bus one: a host-held tap handle can outlive the bus it was
      // registered on (engine close/re-init), and per-bus numbering would let
      // the next session's bus hand out the same value again — a stale
      // unsubscribeTap() would then silently drop somebody else's live tap.
      // Process-global numbering makes a stale handle permanently unknown, so
      // unsubscribeTap() on it is a guaranteed no-op.
      std::atomic<std::uint64_t> g_tapHandleCounter{0};

      // Per-thread slot into the current bus's queue pool. `generation` marks
      // which bus instance `index` was claimed against; a mismatch means this
      // thread has not yet claimed a slot on the live bus.
      struct ProducerSlot {
        std::uint64_t generation = 0;
        std::size_t index = 0;
        bool valid = false;
      };
      thread_local ProducerSlot t_slot;

      // Provision a queue per render thread: the audio callback thread plus one
      // per fast-pool worker (sized to hardware_concurrency), with headroom for
      // transient producers. Bounded so a machine reporting a huge core count
      // does not reserve an unreasonable amount of queue memory.
      std::size_t provisionedQueueCount() {
        unsigned int hw = std::thread::hardware_concurrency();
        if (hw == 0) hw = 4; // hardware_concurrency() may report 0
        std::size_t count = static_cast<std::size_t>(hw) + 4;
        constexpr std::size_t kMaxQueues = 128;
        return count < kMaxQueues ? count : kMaxQueues;
      }
    } // namespace

    NamedBus::NamedBus()
      : generation_(g_busGeneration.fetch_add(1, std::memory_order_relaxed) + 1),
        controlThread_(std::this_thread::get_id()) {
      const std::size_t count = provisionedQueueCount();
      audioQueues_.reserve(count);
      for (std::size_t i = 0; i < count; ++i) {
        audioQueues_.push_back(std::make_unique<lfQueue<PooledMessage>>(kQueueCapacity));
      }
    }

    NamedBus::~NamedBus() = default;

    lfQueue<NamedBus::PooledMessage>* NamedBus::producerQueue() {
      // Claim a slot the first time this thread publishes on the live bus. The
      // fetch_add runs at most once per thread per session — steady-state
      // publishing hits neither an atomic nor a lock here.
      if (t_slot.generation != generation_) {
        const std::size_t slot = nextSlot_.fetch_add(1, std::memory_order_relaxed);
        t_slot.generation = generation_;
        t_slot.index = slot;
        t_slot.valid = slot < audioQueues_.size();
      }
      if (!t_slot.valid) return nullptr; // more producers than provisioned
      return audioQueues_[t_slot.index].get();
    }

    NamedBus& Bus() {
      return Global().namedBus();
    }

    void NamedBus::publish(const std::string& name, const BusValue& value, YSE::THREAD thread) {
      if (thread == T_DSP) {
        // Audio-thread path: must not allocate, must not lock. The queue entry
        // has a fixed footprint, so we accept only int and float.
        PooledMessage msg;
        if (const int* ip = std::get_if<int>(&value)) {
          msg.kind = PooledMessage::Kind::Int;
          msg.value.i = *ip;
        } else if (const float* fp = std::get_if<float>(&value)) {
          msg.kind = PooledMessage::Kind::Float;
          msg.value.f = *fp;
        } else {
          // Unsupported payload on the audio path. Drop silently — callers
          // route strings and lists through the main-thread publish path.
          return;
        }

        // Route to this thread's own SPSC queue. producerQueue() returns null
        // only if more render threads publish than were provisioned — drop
        // rather than share a queue and break the single-producer contract.
        lfQueue<PooledMessage>* queue = producerQueue();
        if (queue == nullptr) return;

        const std::size_t n = name.size() < kNameCapacity ? name.size() : kNameCapacity;
        std::memcpy(msg.nameStorage, name.data(), n);
        msg.nameStorage[n] = '\0';

        // try_push never allocates; if the queue is saturated the message is
        // dropped rather than blocking the audio callback.
        queue->try_push(msg);
        return;
      }

      // Control-thread / GUI path. Only the control thread may dispatch
      // synchronously: a subscriber callback runs an interface setter, which
      // pushes into a per-implementation single-producer message queue, and
      // that queue tolerates exactly one producer. A T_GUI publish arriving on
      // another thread (the gMetro timer thread propagating a bang, a script
      // thread) must therefore NOT dispatch inline — doing so would race the
      // control thread on those SPSC queues and cross-link their block lists
      // (issue #193). Park it for the next drainPending() instead.
      if (std::this_thread::get_id() == controlThread_) {
        dispatch(name, value);
        return;
      }

      // Off-control-thread publish. These producers are control-rate threads
      // (never the audio callback, which only ever publishes on T_DSP above),
      // so taking a mutex here does not touch the real-time path.
      {
        std::lock_guard<std::mutex> lock(pendingMutex_);
        pendingControl_.emplace_back(name, value);
      }
    }

    SubHandle NamedBus::subscribe(const std::string& name, Subscriber callback) {
      const SubHandle handle = nextHandle_.fetch_add(1, std::memory_order_relaxed);
      std::unique_lock lock(subsMutex_);
      subs_[name].push_back(Subscription{handle, std::move(callback)});
      handleIndex_.emplace(handle, name);
      return handle;
    }

    void NamedBus::unsubscribe(SubHandle handle) {
      std::unique_lock lock(subsMutex_);
      auto idxIt = handleIndex_.find(handle);
      if (idxIt == handleIndex_.end()) return;
      auto subsIt = subs_.find(idxIt->second);
      if (subsIt != subs_.end()) {
        auto& list = subsIt->second;
        list.erase(std::remove_if(list.begin(), list.end(),
                                  [handle](const Subscription& s) { return s.handle == handle; }),
                   list.end());
        if (list.empty()) {
          subs_.erase(subsIt);
        }
      }
      handleIndex_.erase(idxIt);
    }

    TapHandle NamedBus::subscribeTap(const std::string& prefix, TapSubscriber callback) {
      const TapHandle handle = g_tapHandleCounter.fetch_add(1, std::memory_order_relaxed) + 1;
      std::unique_lock lock(subsMutex_);
      taps_.push_back(TapSubscription{handle, prefix, std::move(callback)});
      return handle;
    }

    void NamedBus::unsubscribeTap(TapHandle handle) {
      std::unique_lock lock(subsMutex_);
      taps_.erase(std::remove_if(taps_.begin(), taps_.end(),
                                 [handle](const TapSubscription& t) { return t.handle == handle; }),
                  taps_.end());
    }

    void NamedBus::drainPending() {
      // Drain every producer queue. Each is single-consumer (only this call
      // pops), so touching them all from the update thread is race-free. Empty
      // queues cost a single try_pop; slots past nextSlot_ are never claimed.
      PooledMessage msg;
      for (auto& queue : audioQueues_) {
        while (queue->try_pop(msg)) {
          BusValue value;
          switch (msg.kind) {
          case PooledMessage::Kind::Int:
            value = msg.value.i;
            break;
          case PooledMessage::Kind::Float:
            value = msg.value.f;
            break;
          }
          dispatch(std::string(msg.nameStorage), value);
        }
      }

      // Drain T_GUI publishes that were deferred from a non-control thread
      // (issue #193): the gMetro timer thread or a script thread parked them
      // here instead of dispatching inline, which would have raced the control
      // thread on the per-implementation SPSC message queues. Swap the inbox
      // out under the lock, then dispatch with the lock released so a
      // subscriber may re-enter publish() / subscribe() without deadlocking.
      std::vector<std::pair<std::string, BusValue>> deferred;
      {
        std::lock_guard<std::mutex> lock(pendingMutex_);
        deferred.swap(pendingControl_);
      }
      for (auto& entry : deferred) {
        dispatch(entry.first, entry.second);
      }
    }

    void NamedBus::dispatch(const std::string& name, const BusValue& value) {
      // Copy out the matching callbacks under the shared lock, then invoke
      // them with the lock released. This lets a subscriber call back into
      // subscribe()/unsubscribe() without self-deadlock. When nothing matches
      // (the common case for an unsubscribed name with no taps registered)
      // neither vector allocates — the no-subscriber publish path stays
      // allocation-free (pinned by the alloc-probe test).
      std::vector<Subscriber> callbacks;
      std::vector<TapSubscriber> tapCallbacks;
      {
        std::shared_lock lock(subsMutex_);
        auto it = subs_.find(name);
        if (it != subs_.end()) {
          callbacks.reserve(it->second.size());
          for (const auto& sub : it->second) {
            callbacks.push_back(sub.callback);
          }
        }
        // Prefix taps (issue #389). Linear scan — the tap count is a handful
        // of host-registered prefixes. dispatch() only ever runs on the
        // control thread, so this costs the audio path nothing.
        for (const auto& tap : taps_) {
          if (name.size() >= tap.prefix.size() &&
              name.compare(0, tap.prefix.size(), tap.prefix) == 0) {
            tapCallbacks.push_back(tap.callback);
          }
        }
      }
      for (auto& cb : callbacks) {
        cb(value);
      }
      for (auto& cb : tapCallbacks) {
        cb(name, value);
      }
    }

  } // namespace INTERNAL
} // namespace YSE
