/*
  ==============================================================================

    midiOutSender.h
    Created for issue #350 — clip transport external MIDI-out sink.

    The clip transport decides event timing block-accurately on the audio
    thread, but an RtMidi send cannot happen on the audio callback without
    reintroducing the jitter the transport exists to remove. This module is
    the hand-off: the audio thread pushes absolute-time-stamped MIDI messages
    onto a bounded lock-free SPSC queue (wait-free, no allocation), and a
    dedicated sender thread drains the queue and performs the RtMidi sends
    when each message comes due.

    Producer/consumer contract: every clip transport advances on the single
    audio thread (CLIP::Manager().update()), so the audio thread is the sole
    producer; the sender thread is the sole consumer. That satisfies the SPSC
    requirement of lfQueue with any number of concurrent clips.

    Immediate lane (issue #949): the patcher's `.midiout` sends a message the
    moment its inlet receives one, on whichever thread dispatched it —
    routinely the audio callback, but not only. Those messages have no
    deadline and may be any length up to a system-exclusive dump, and there is
    more than one possible producer thread, so they travel on a second,
    multi-producer queue of full-length messages that the same worker drains
    first on every pass. Sends from one producer keep their order.

  ==============================================================================
*/

#ifndef YSE_MIDI_MIDIOUTSENDER_H
#define YSE_MIDI_MIDIOUTSENDER_H

#include "../headers/defines.hpp"
#if YSE_ENABLE_MIDI_DEVICE

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <thread>

#include "../utils/lfQueue.hpp"
#include "../utils/mpmcQueue.hpp"

/// @cond INTERNAL
class RtMidiOut;
/// @endcond

namespace YSE {
  namespace MIDI {

    /** @brief steady_clock now, in nanoseconds — the timebase for outEvent::dueNs. */
    inline std::int64_t nowNs() {
      return std::chrono::duration_cast<std::chrono::nanoseconds>(
                 std::chrono::steady_clock::now().time_since_epoch())
          .count();
    }

    /** @brief One scheduled outbound MIDI message.
     *
     *  ``port`` is a device-manager-owned RtMidi port (see
     *  ``MIDI::deviceManager::getMidiOutPort``), which stays open until process
     *  exit — so a queued pointer cannot dangle even if the caller destroys the
     *  ``YSE::midiOut`` wrapper that resolved it. ``dueNs`` is an absolute
     *  steady_clock deadline (see ``nowNs()``); the sender never sends early. */
    struct outEvent {
      std::int64_t dueNs = 0;
      RtMidiOut* port = nullptr;
      unsigned char bytes[3] = {0, 0, 0};
      unsigned char len = 0;
    };

    /** @brief Longest message the immediate lane carries — the patcher's
     *         byte-list limit, so anything `.midiout` can read fits whole. */
    constexpr std::size_t kRawEventMax = 256;

    /** @brief One outbound MIDI message of any length up to ``kRawEventMax``,
     *         sent as soon as the sender reaches it (issue #949).
     *
     *  ``port`` carries the same lifetime guarantee as ``outEvent::port``: a
     *  device-manager-owned port, open until process exit. */
    struct rawEvent {
      RtMidiOut* port = nullptr;
      std::uint16_t len = 0;
      unsigned char bytes[kRawEventMax] = {};
    };

    // ---- message encoders (pure functions; RT-safe) --------------------------
    // `channel` is 1..16 (the YSE::clipEvent convention), clamped; `pitch` is
    // clamped to 0..127; `velocity` is normalized [0, 1] and mapped to 0..127;
    // `bend` is normalized [-1, 1] and mapped to the 14-bit pitch-wheel range
    // (0 -> center 8192).
    outEvent makeNoteOn(RtMidiOut* port, std::int64_t dueNs, int channel, int pitch,
                        float velocity);
    outEvent makeNoteOff(RtMidiOut* port, std::int64_t dueNs, int channel, int pitch,
                         float velocity);
    outEvent makePitchWheel(RtMidiOut* port, std::int64_t dueNs, int channel, float bend);

    /** @brief Dedicated MIDI-out sender thread + bounded hand-off queue.
     *
     *  ``tryEnqueue`` is the audio-thread side: wait-free, never allocates,
     *  returns false (dropping the message) when the queue is full. The worker
     *  thread pops messages, waits until each one's ``dueNs``, and sends it on
     *  its RtMidi port. Messages with equal deadlines are sent in queue (FIFO)
     *  order, which preserves the transport's note-off-before-note-on ordering
     *  within a block.
     *
     *  ``start`` / ``stop`` are control-thread calls. ``stop`` joins the worker
     *  and then flushes everything still queued immediately (chiefly the
     *  note-offs a stopping clip released), so shutdown never leaves notes
     *  hanging on external hardware.
     */
    class outSender {
    public:
      /** Test seam: when installed, messages are delivered to the hook (on the
          sender thread, or on the flushing thread during stop()) instead of
          being sent to RtMidi — the event's port is never dereferenced. */
      using SendHook = void (*)(const outEvent&, void* user);

      outSender() = default;
      ~outSender();

      outSender(const outSender&) = delete;
      outSender& operator=(const outSender&) = delete;

      /** Control thread. Spawn the worker; idempotent while running. */
      void start();

      /** Control thread. Stop + join the worker, then flush the queue. Safe to
          call when never started; a later start() spawns a fresh worker. */
      void stop();

      bool isRunning() const {
        return running.load(std::memory_order_acquire);
      }

      /** Audio thread. Wait-free enqueue; false = queue full, message dropped. */
      bool tryEnqueue(const outEvent& e) {
        return queue.try_push(e);
      }

      /** Any thread, including the audio callback (issue #949). Queue
          @p length bytes for @p port to be sent as soon as the worker reaches
          them. Lock-free, never allocates. Returns false — nothing queued — for
          a null port, an empty message, one longer than ``kRawEventMax``, or a
          full queue; the caller has already refused the first three. */
      bool tryEnqueueNow(RtMidiOut* port, const unsigned char* data, std::size_t length);

      /** Test seam for the immediate lane, like ``setSendHookForTest``. */
      using RawSendHook = void (*)(const rawEvent&, void* user);

      void setSendHookForTest(SendHook h, void* user) {
        hookUser.store(user, std::memory_order_release);
        hook.store(h, std::memory_order_release);
      }

      void setRawSendHookForTest(RawSendHook h, void* user) {
        rawHookUser.store(user, std::memory_order_release);
        rawHook.store(h, std::memory_order_release);
      }

    private:
      void run();
      void send(const outEvent& e);
      void sendRaw(const rawEvent& e);
      // Pop and send everything on the immediate lane. Sole consumer: the
      // worker, or stop() once the worker is joined.
      void drainImmediate();

      // Bounded: the audio thread only ever try_pushes. 1024 in-flight messages
      // is far beyond a block's worth of clip events; overflow drops.
      static constexpr std::size_t kQueueCapacity = 1024;
      lfQueue<outEvent> queue{kQueueCapacity};

      // The immediate lane: multi-producer because a patcher handler runs on
      // whichever thread dispatched it. Drained on every worker pass (at most
      // ~1 ms apart), so 512 messages is several hundred per millisecond of
      // headroom; overflow drops and the producer counts it.
      static constexpr std::size_t kImmediateCapacity = 512;
      mpmcQueue<rawEvent> immediate{kImmediateCapacity};

      std::thread worker;
      std::atomic<bool> running{false};

      std::atomic<SendHook> hook{nullptr};
      std::atomic<void*> hookUser{nullptr};
      std::atomic<RawSendHook> rawHook{nullptr};
      std::atomic<void*> rawHookUser{nullptr};
    };

    /** @brief The engine-wide sender instance. Lazily started by the first
     *         clip-transport MIDI-out connect; stopped from global::close(). */
    outSender& OutSender();

  } // namespace MIDI
} // namespace YSE

#endif // YSE_ENABLE_MIDI_DEVICE
#endif // YSE_MIDI_MIDIOUTSENDER_H
