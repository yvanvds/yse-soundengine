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

#include "../utils/lfQueue.hpp"
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

      /** Events one subscription may hold before the next one is dropped. An
          audio block is a few milliseconds; 63 messages inside one is far past
          anything a controller produces, so a full queue means the audio thread
          has stalled rather than that MIDI came in fast. */
      static constexpr std::size_t kQueueCapacity = 63;

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
       *  Wait-free: a bounded walk of the port's slots and one `try_push` each.
       *  A push that fails drops the chunk and is reported once per overflow
       *  episode — see `Dropped`. */
      void Deliver(unsigned int port, const unsigned char* bytes, std::size_t len);

      /** @brief Control thread. Close every open port and forget every
       *         subscription. Called from the destructor; safe to call twice. */
      void Close();

      /** @brief Chunks this subscription has lost to a full queue, ever. 0 for
       *         an invalid handle. Diagnostic / test surface. */
      std::uint64_t Dropped(Handle handle) const;

      /** @brief Whether the hub holds an open device for `port`. False in a
       *         test rig with no MIDI hardware, where `Deliver` is the only
       *         producer. */
      bool PortIsOpen(unsigned int port) const;

    private:
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
        lfQueue<inEvent> queue{kQueueCapacity};
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
