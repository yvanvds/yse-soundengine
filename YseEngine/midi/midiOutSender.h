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

  ==============================================================================
*/

#ifndef YSE_MIDI_MIDIOUTSENDER_H
#define YSE_MIDI_MIDIOUTSENDER_H

#include "../headers/defines.hpp"
#if YSE_ENABLE_MIDI_DEVICE

#include <atomic>
#include <chrono>
#include <cstdint>
#include <thread>

#include "../utils/lfQueue.hpp"

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

      void setSendHookForTest(SendHook h, void* user) {
        hookUser.store(user, std::memory_order_release);
        hook.store(h, std::memory_order_release);
      }

    private:
      void run();
      void send(const outEvent& e);

      // Bounded: the audio thread only ever try_pushes. 1024 in-flight messages
      // is far beyond a block's worth of clip events; overflow drops.
      static constexpr std::size_t kQueueCapacity = 1024;
      lfQueue<outEvent> queue{kQueueCapacity};

      std::thread worker;
      std::atomic<bool> running{false};

      std::atomic<SendHook> hook{nullptr};
      std::atomic<void*> hookUser{nullptr};
    };

    /** @brief The engine-wide sender instance. Lazily started by the first
     *         clip-transport MIDI-out connect; stopped from global::close(). */
    outSender& OutSender();

  } // namespace MIDI
} // namespace YSE

#endif // YSE_ENABLE_MIDI_DEVICE
#endif // YSE_MIDI_MIDIOUTSENDER_H
