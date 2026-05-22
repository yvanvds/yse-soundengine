/*
  ==============================================================================

    namedBus.cpp
    Created: 2026-05-22

  ==============================================================================
*/

#include "../internalHeaders.h"
#include "namedBus.h"

#include <algorithm>

namespace YSE {
  namespace INTERNAL {

    NamedBus::NamedBus() : audioQueue_(kQueueCapacity) {}

    NamedBus::~NamedBus() = default;

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

        const std::size_t n = name.size() < kNameCapacity ? name.size() : kNameCapacity;
        std::memcpy(msg.nameStorage, name.data(), n);
        msg.nameStorage[n] = '\0';

        // try_push never allocates; if the queue is saturated the message is
        // dropped rather than blocking the audio callback.
        audioQueue_.try_push(msg);
        return;
      }

      // Main-thread / GUI path: synchronous dispatch.
      dispatch(name, value);
    }

    SubHandle NamedBus::subscribe(const std::string& name, Subscriber callback) {
      const SubHandle handle = nextHandle_.fetch_add(1, std::memory_order_relaxed);
      std::unique_lock lock(subsMutex_);
      subs_[name].push_back(Subscription{ handle, std::move(callback) });
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
        list.erase(
          std::remove_if(list.begin(), list.end(),
            [handle](const Subscription& s) { return s.handle == handle; }),
          list.end());
        if (list.empty()) {
          subs_.erase(subsIt);
        }
      }
      handleIndex_.erase(idxIt);
    }

    void NamedBus::drainPending() {
      PooledMessage msg;
      while (audioQueue_.try_pop(msg)) {
        BusValue value;
        switch (msg.kind) {
          case PooledMessage::Kind::Int:   value = msg.value.i; break;
          case PooledMessage::Kind::Float: value = msg.value.f; break;
        }
        dispatch(std::string(msg.nameStorage), value);
      }
    }

    void NamedBus::dispatch(const std::string& name, const BusValue& value) {
      // Copy out the matching callbacks under the shared lock, then invoke
      // them with the lock released. This lets a subscriber call back into
      // subscribe()/unsubscribe() without self-deadlock.
      std::vector<Subscriber> callbacks;
      {
        std::shared_lock lock(subsMutex_);
        auto it = subs_.find(name);
        if (it == subs_.end()) return;
        callbacks.reserve(it->second.size());
        for (const auto& sub : it->second) {
          callbacks.push_back(sub.callback);
        }
      }
      for (auto& cb : callbacks) {
        cb(value);
      }
    }

  } // namespace INTERNAL
} // namespace YSE
