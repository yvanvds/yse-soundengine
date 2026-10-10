/*
  ==============================================================================

    midiInHub.h
    Created for issue #529 — the patcher's MIDI *input* plumbing.

    `midiOutSender` in reverse. The patcher could send MIDI and could not
    receive any: every `mMidi*` object was a formatter or a sender, so no
    keyboard, controller or external sequencer could reach a patch at all.

    The crux is threading. MIDI arrives on RtMidi's own input thread, one per
    open port, and patcher objects are driven from the audio callback, where
    allocating, locking or blocking is forbidden. This module is the hand-off:
    the RtMidi thread copies each incoming message into a bounded lock-free
    SPSC queue and the audio thread drains that queue in `Calculate()`. Neither
    side allocates and neither side takes a lock.

    ### Why the queues are laid out per port rather than in one flat table

    `lfQueue` is single-producer / single-consumer, and that is a hard
    requirement rather than a documentation nicety: two threads pushing into
    one queue corrupts it. A flat table of subscriptions that could be
    re-pointed at a different port would break exactly that, because the
    producer of a given queue would change from one RtMidi thread to another
    while a push was in flight.

    So a subscription slot belongs to a port for the whole life of the hub:
    `subscriptions[port][index]`. The producer of any one queue is therefore
    always the same RtMidi input thread, whatever objects come and go, and the
    consumer is always the audio thread of the patcher that owns the object.
    The cost is a fixed ceiling — `kMaxPorts` ports and
    `kMaxSubscriptionsPerPort` objects listening to each — which is what buys
    a table the RtMidi thread can walk without a lock.

    ### Messages longer than one event

    `inEvent` carries `inEvent::kMaxBytes` bytes, which is more than any
    channel-voice or system-real-time message needs, so those are never split.
    A SysEx dump is longer, and arrives as consecutive chunks in order — the
    byte stream a `.sysexin` (issue #531) wants to spool out one byte at a
    time. A subscriber that needs whole messages must reassemble.

    RtMidi hands the callback a dump *whole*, however long it took on the
    wire, so every chunk of it is pushed at once. That is what sizes the queue
    (issue #950): it holds `kMaxMessageBytes` — twice a DX7 32-voice bank —
    and a message is queued **whole or not at all**. One that does not fit in
    the space left is dropped and counted as one message, never cut, because
    a dump missing its tail and its 247 reaches a patch looking like a
    complete message that happens to be wrong. The drain is bounded
    separately (`kDrainPerBlock`), so a long dump is spread over a few blocks
    rather than landing in one.

  ==============================================================================
*/

#ifndef YSE_MIDI_MIDIINHUB_H
#define YSE_MIDI_MIDIINHUB_H

#include "../headers/defines.hpp"
#if YSE_ENABLE_MIDI_DEVICE

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>

#include "device.hpp"

namespace YSE {
  namespace MIDI {

    /** @brief One chunk of incoming MIDI, copied off RtMidi's buffer.
     *
     *  POD on purpose: it rides a lock-free queue by value, so it must be
     *  trivially copyable and must own nothing. A channel-voice or
     *  system-real-time message always fits whole; see the header comment for
     *  what happens to a longer one. */
    struct inEvent {
      static constexpr std::size_t kMaxBytes = 8;
      unsigned char len = 0;
      unsigned char bytes[kMaxBytes] = {0, 0, 0, 0, 0, 0, 0, 0};
    };

    /**
     *  @brief Engine-wide fan-out from RtMidi input ports to per-object
     *         lock-free queues (issue #529).
     *
     *  `Subscribe` / `Unsubscribe` / `Close` are **control thread** calls; they
     *  take the hub's mutex and may open or close a device. `Deliver` is the
     *  **RtMidi input thread**'s entry point and `TryPop` is the **audio
     *  thread**'s: both are wait-free and neither touches the mutex.
     *
     *  The hub opens its own `YSE::midiIn` per port. That is a second client on
     *  the same hardware port when a host has opened one itself; on backends
     *  that allow only one opener the second `create()` fails and the port
     *  simply produces nothing, which `Subscribe` reports through the log.
     */
    class inHub {
    public:
      /** @brief A subscription. 0 (`kNoHandle`) means "not subscribed" and is
       *         what `Subscribe` returns when it cannot give one out. */
      using Handle = unsigned int;
      static constexpr Handle kNoHandle = 0;

      /** Ports this hub can listen to at once. Above this, `Subscribe` refuses.
          A machine with more than eight MIDI inputs in use by one patch at the
          same time is well past what the object family was built for. */
      static constexpr unsigned int kMaxPorts = 8;

      /** Objects that may listen to one port. Every `.notein`, `.ctlin`, ...
          on a port takes one slot. */
      static constexpr unsigned int kMaxSubscriptionsPerPort = 8;

      /** Events one subscription may hold before a message is dropped. Sized
          for a SysEx dump rather than for controller traffic (issue #950):
          RtMidi delivers a dump in one callback, so the whole of it has to fit
          at once. A power of two, so the ring index is a mask. */
      static constexpr std::size_t kQueueCapacity = 1024;

      /** The longest message a subscriber can receive: 8192 bytes, twice a
          DX7 32-voice bulk dump (4104). A longer one is dropped whole and
          reported, never delivered truncated. */
      static constexpr std::size_t kMaxMessageBytes = kQueueCapacity * inEvent::kMaxBytes;

      /** Events one subscriber drains per audio block. The bound on what the
          audio callback spends here, independent of the queue's size: 64
          events is 512 bytes, so a DX7 bank reaches the patch over nine
          blocks instead of as 4104 sends in one. A controller does not produce
          64 messages in a block, so ordinary traffic is never held back. */
      static constexpr std::size_t kDrainPerBlock = 64;

      inHub() = default;
      ~inHub();

      inHub(const inHub&) = delete;
      inHub& operator=(const inHub&) = delete;
      inHub(inHub&&) = delete;
      inHub& operator=(inHub&&) = delete;

      /** @brief Control thread. Claim a queue on `port`, opening the device the
       *         first time anything asks for it.
       *
       *  Returns `kNoHandle` when `port` is past `kMaxPorts` or the port's
       *  slots are all taken — both logged. A port that exists in the table but
       *  could not be *opened* still yields a handle: the subscription is
       *  valid, it just never receives anything until something injects into
       *  it, which is what lets the object family be tested without hardware. */
      Handle Subscribe(unsigned int port);

      /** @brief Control thread. Give a slot back. Safe on `kNoHandle`. */
      void Unsubscribe(Handle handle);

      /** @brief Audio thread. Wait-free pop; false when nothing is waiting. */
      bool TryPop(Handle handle, inEvent& event);

      /** @brief RtMidi input thread (and tests). Copy one incoming message to
       *         every subscription on `port`, splitting it into `inEvent`
       *         chunks if it does not fit one.
       *
       *  Wait-free: a bounded walk of the port's slots and a bounded copy
       *  each. A message goes into a queue whole or not at all: one that does
       *  not fit the space left, or is longer than `kMaxMessageBytes`, is
       *  dropped, counted and reported once per overflow episode — see
       *  `Dropped`. */
      void Deliver(unsigned int port, const unsigned char* bytes, std::size_t len);

      /** @brief Control thread. Close every open port and forget every
       *         subscription. Called from the destructor; safe to call twice. */
      void Close();

      /** @brief Messages this subscription has lost to a full queue or to
       *         `kMaxMessageBytes`, ever. 0 for an invalid handle. Diagnostic /
       *         test surface. */
      std::uint64_t Dropped(Handle handle) const;

      /** @brief Whether the hub holds an open device for `port`. False in a
       *         test rig with no MIDI hardware, where `Deliver` is the only
       *         producer. */
      bool PortIsOpen(unsigned int port) const;

    private:
      // Single-producer / single-consumer ring of `kQueueCapacity` events.
      // Purpose-built rather than `lfQueue` because the producer must ask how
      // much room is left *before* pushing, so that a message is queued whole
      // or not at all (issue #950); `lfQueue` can only say whether one more
      // element fits. The storage is allocated once, on the control thread,
      // when the hub is built; nothing here allocates afterwards. The indices
      // count up forever and are masked on use, so `write - read` is the fill
      // level with no wasted slot.
      class eventRing {
      public:
        eventRing() : slots(std::make_unique<inEvent[]>(kQueueCapacity)) {}

        // Producer. Room left, never over-reported: the consumer can only
        // make it larger between this call and the pushes that follow.
        std::size_t Free() const {
          const std::size_t w = write.load(std::memory_order_acquire);
          const std::size_t r = read.load(std::memory_order_acquire);
          return kQueueCapacity - (w - r);
        }

        // Producer. The caller has checked `Free()`.
        void Push(const inEvent& event) {
          const std::size_t w = write.load(std::memory_order_acquire);
          slots[w & kMask] = event;
          write.store(w + 1, std::memory_order_release);
        }

        // Consumer. False when empty.
        bool TryPop(inEvent& event) {
          const std::size_t r = read.load(std::memory_order_acquire);
          if (r == write.load(std::memory_order_acquire)) return false;
          event = slots[r & kMask];
          read.store(r + 1, std::memory_order_release);
          return true;
        }

      private:
        static_assert((kQueueCapacity & (kQueueCapacity - 1)) == 0,
                      "kQueueCapacity must be a power of two");
        static constexpr std::size_t kMask = kQueueCapacity - 1;

        std::unique_ptr<inEvent[]> slots;
        // Own cache lines: the producer hammers one and the consumer the other.
        alignas(64) std::atomic<std::size_t> write{0};
        alignas(64) std::atomic<std::size_t> read{0};
      };

      // One object's queue. Never destroyed and never moved between ports —
      // see the header for why that is what makes the SPSC contract hold.
      struct subscription {
        std::atomic<bool> inUse{false};
        // Set to true only once the queue has been drained of whatever the
        // previous owner left behind, so a producer never hands a fresh
        // subscriber a stale event.
        std::atomic<bool> live{false};
        std::atomic<std::uint64_t> dropped{0};
        // Whether the current overflow episode has already been logged. Reset
        // when a push succeeds again, so a stall reports once rather than once
        // per lost message.
        std::atomic<bool> overflowReported{false};
        eventRing queue;
      };

      // What `midiIn::setRawCallback` carries as its user pointer: the hub and
      // which port the message arrived on. One per port, allocated once and
      // never moved, so the pointer the RtMidi thread holds cannot dangle while
      // the port is open.
      struct portContext {
        inHub* hub = nullptr;
        unsigned int port = 0;
      };

      struct portSlot {
        std::unique_ptr<YSE::midiIn> device;
        portContext context;
        unsigned int subscribers = 0;
      };

      static void RawTrampoline(double timestampSec, const unsigned char* bytes, std::size_t len,
                                void* user);

      static Handle MakeHandle(unsigned int port, unsigned int index) {
        return port * kMaxSubscriptionsPerPort + index + 1;
      }
      // False when `handle` names no slot, which covers kNoHandle.
      static bool SplitHandle(Handle handle, unsigned int& port, unsigned int& index);

      // Control thread, mutex held.
      void OpenPortIfNeeded(unsigned int port);
      void ClosePortIfUnused(unsigned int port);

      mutable std::mutex mtx;
      portSlot ports[kMaxPorts];
      subscription subscriptions[kMaxPorts][kMaxSubscriptionsPerPort];
    };

    /** @brief The engine-wide hub. Built on first use by a patcher MIDI-input
     *         object and torn down with the process. */
    inHub& InHub();

  } // namespace MIDI
} // namespace YSE

#endif // YSE_ENABLE_MIDI_DEVICE
#endif // YSE_MIDI_MIDIINHUB_H
