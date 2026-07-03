/*
  ==============================================================================

    mpmcQueue.hpp
    Created: 3 Jul 2026

    Bounded lock-free multi-producer / multi-consumer queue.

  ==============================================================================
*/

#ifndef YSE_UTILS_MPMCQUEUE_HPP_INCLUDED
#define YSE_UTILS_MPMCQUEUE_HPP_INCLUDED

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>

namespace YSE {

  // Bounded lock-free multi-producer / multi-consumer queue — Dmitry Vyukov's
  // array-of-cells algorithm. Fixed capacity, allocated once at construction;
  // try_push / try_pop never lock, never allocate, and are wait-free bar the
  // bounded CAS retry loop under producer/consumer contention.
  //
  // Introduced for INTERNAL::threadPool (issue #188): the audio callback hands
  // DSP fan-out jobs (render pool) and manager/setup jobs (background pool) to
  // the worker threads without ever touching a mutex, allocating, or issuing a
  // wakeup syscall. Both pools push from the callback; the render pool also has
  // multiple worker producers (nested child dispatch) and multiple consumers,
  // so a full MPMC queue — not the SPSC lfQueue — is required.
  //
  // T must be trivially copyable (this queue stores pointers). Capacity is
  // rounded up to the next power of two so the index wrap is a single mask.
  template <typename T> class mpmcQueue {
  public:
    explicit mpmcQueue(std::size_t capacity)
      : buffer_(new cell[roundUpPow2(capacity)]), capacityMask_(roundUpPow2(capacity) - 1) {
      for (std::size_t i = 0; i <= capacityMask_; ++i) {
        buffer_[i].sequence.store(i, std::memory_order_relaxed);
      }
      enqueuePos_.store(0, std::memory_order_relaxed);
      dequeuePos_.store(0, std::memory_order_relaxed);
    }

    // Enqueue a value. Returns false (without blocking or allocating) when the
    // queue is full.
    bool try_push(T const& value) {
      cell* c;
      std::size_t pos = enqueuePos_.load(std::memory_order_relaxed);
      for (;;) {
        c = &buffer_[pos & capacityMask_];
        std::size_t seq = c->sequence.load(std::memory_order_acquire);
        std::intptr_t diff = (std::intptr_t)seq - (std::intptr_t)pos;
        if (diff == 0) {
          if (enqueuePos_.compare_exchange_weak(pos, pos + 1, std::memory_order_relaxed)) {
            break;
          }
        } else if (diff < 0) {
          return false; // full
        } else {
          pos = enqueuePos_.load(std::memory_order_relaxed);
        }
      }
      c->data = value;
      c->sequence.store(pos + 1, std::memory_order_release);
      return true;
    }

    // Dequeue into value. Returns false when the queue is empty.
    bool try_pop(T& value) {
      cell* c;
      std::size_t pos = dequeuePos_.load(std::memory_order_relaxed);
      for (;;) {
        c = &buffer_[pos & capacityMask_];
        std::size_t seq = c->sequence.load(std::memory_order_acquire);
        std::intptr_t diff = (std::intptr_t)seq - (std::intptr_t)(pos + 1);
        if (diff == 0) {
          if (dequeuePos_.compare_exchange_weak(pos, pos + 1, std::memory_order_relaxed)) {
            break;
          }
        } else if (diff < 0) {
          return false; // empty
        } else {
          pos = dequeuePos_.load(std::memory_order_relaxed);
        }
      }
      value = c->data;
      c->sequence.store(pos + capacityMask_ + 1, std::memory_order_release);
      return true;
    }

    std::size_t capacity() const {
      return capacityMask_ + 1;
    }

  private:
    struct cell {
      std::atomic<std::size_t> sequence;
      T data;
    };

    static std::size_t roundUpPow2(std::size_t x) {
      if (x < 2) return 2;
      --x;
      x |= x >> 1;
      x |= x >> 2;
      x |= x >> 4;
      x |= x >> 8;
      x |= x >> 16;
      if (sizeof(std::size_t) > 4) x |= x >> 32;
      return x + 1;
    }

    // Producer and consumer cursors live on separate cache lines so the two
    // sides don't ping-pong a shared line under contention.
    static constexpr std::size_t CACHE_LINE = 64;

    std::unique_ptr<cell[]> buffer_;
    const std::size_t capacityMask_;
    alignas(CACHE_LINE) std::atomic<std::size_t> enqueuePos_;
    alignas(CACHE_LINE) std::atomic<std::size_t> dequeuePos_;

    mpmcQueue(mpmcQueue const&) = delete;
    mpmcQueue& operator=(mpmcQueue const&) = delete;
  };

} // namespace YSE

#endif // YSE_UTILS_MPMCQUEUE_HPP_INCLUDED
