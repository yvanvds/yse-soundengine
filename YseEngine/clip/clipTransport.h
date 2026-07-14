/*
  ==============================================================================

    clipTransport.h
    Created for issue #250 — clip transport.

  ==============================================================================
*/

#ifndef YSE_CLIP_CLIPTRANSPORT_H
#define YSE_CLIP_CLIPTRANSPORT_H

#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <string>
#include <vector>

#include "clip.hpp"
#include "../headers/enums.hpp"
#include "../utils/lfQueue.hpp"
#include "../synth/synth.hpp"

namespace YSE {
  namespace CLOCK {
    class domainClock;
  }

  namespace CLIP {

    /** @brief Audio-thread-owned playback engine for one clip.
     *
     *  Mirrors the MIDI-file playback impl (``MIDI::fileImpl``): the control
     *  thread creates it through the manager and hands it over lock-free, the
     *  audio thread owns the working state and advances it every block, and a
     *  slow-pool job reaps it once the interface is destroyed. The transport
     *  never allocates, locks, or blocks on the audio thread.
     */
    class transport {
    public:
      using EventList = std::vector<clipEvent>;

      explicit transport(clip* head);
      ~transport();

      // ---- control thread --------------------------------------------------

      /** Bind to a domain clock by name. Returns false for an unknown name. */
      bool bind(const std::string& clockName);

      /** Publish a new event list; the audio thread swaps it in at the next
          block boundary. Reclaims buffers the audio thread has retired. */
      void setEvents(const EventList& events);

      void loopLength(double beats) {
        loopBeats.store(beats, std::memory_order_relaxed);
      }

      void connect(SYNTH::interfaceObject* target);
      void disconnect(SYNTH::interfaceObject* target);

      void play() {
        intent.store(SS_WANTSTOPLAY, std::memory_order_release);
      }
      void stop() {
        intent.store(SS_WANTSTOSTOP, std::memory_order_release);
      }
      bool isPlaying() const {
        const SOUND_STATUS s = intent.load(std::memory_order_acquire);
        return s == SS_PLAYING || s == SS_WANTSTOPLAY;
      }

      // ---- audio thread ----------------------------------------------------

      /** Advance one audio block: read the clock's beat window and fire the
          events (and note-offs) that fall inside it. */
      void advance();

      // ---- lifecycle (mirrors MIDI::fileImpl) ------------------------------

      void removeInterface() {
        head.store(nullptr);
      }
      bool hasInterface() const {
        return head.load() != nullptr;
      }
      void setStatus(OBJECT_IMPLEMENTATION_STATE value) {
        objectStatus.store(value);
      }
      static bool canBeDeleted(const transport& t) {
        return t.objectStatus.load() == OBJECT_DELETE;
      }

      // ---- timing core (audio thread; templated for unit tests) ------------

      /** Fire every note-off and note-on whose crossing falls in the half-open
          beat window ``(from, to]`` on ``sink``. Note-offs are emitted before
          note-ons so a same-block retrigger releases the old note first.
          ``sink`` must expose the YSE::synth note API (noteOn / noteOff /
          pitchWheel) — the real audio path passes a synth fan-out; tests pass a
          recording sink. Pure function of the audio-thread-owned state; no
          allocation, no locking. */
      template <class Sink> void evaluateWindow(Sink& sink, double from, double to) {
        const double loop = loopBeats.load(std::memory_order_relaxed);

        // 1) Release notes whose scheduled off-beat has passed. Driven by each
        //    note's own recorded off-beat, so a note dropped from a swapped-in
        //    list is still released on time.
        for (auto& n : sounding) {
          if (n.active && n.offBeat <= to) {
            sink.noteOff(n.channel, n.pitch, 0.f);
            n.active = false;
          }
        }

        // 2) Fire note-ons for every event occurrence in (from, to].
        if (current == nullptr) return;
        for (const clipEvent& e : *current) {
          if (loop > 0.0) {
            double s0 = std::fmod(e.startBeat, loop);
            if (s0 < 0.0) s0 += loop;
            // First occurrence strictly greater than `from`.
            double k = std::floor((from - s0) / loop) + 1.0;
            for (int guard = 0; guard < kMaxOccurrencesPerBlock; ++guard) {
              const double occ = s0 + k * loop;
              if (occ > to) break;
              fireNoteOn(sink, e, occ + e.durationBeats);
              k += 1.0;
            }
          } else {
            // No looping: fire once when its absolute start falls in the window.
            if (e.startBeat > from && e.startBeat <= to)
              fireNoteOn(sink, e, e.startBeat + e.durationBeats);
          }
        }
      }

      /** Release every sounding note (stop / teardown). */
      template <class Sink> void releaseAll(Sink& sink) {
        for (auto& n : sounding) {
          if (n.active) {
            sink.noteOff(n.channel, n.pitch, 0.f);
            n.active = false;
          }
        }
      }

      // Test seam: drive the audio-thread window directly.
      void setLoopForTest(double beats) {
        loopBeats.store(beats, std::memory_order_relaxed);
      }
      void adoptEventsForTest(const EventList& events) {
        delete current;
        current = new EventList(events);
      }

    private:
      struct soundingNote {
        bool active = false;
        int channel = 0;
        int pitch = 0;
        double offBeat = 0.0;
      };

      // Bounded — no allocation on the audio thread. 256 simultaneous notes is
      // far beyond any musical clip; extra note-ons past capacity are dropped.
      static constexpr std::size_t kMaxSounding = 256;
      std::array<soundingNote, kMaxSounding> sounding;
      // Caps the per-event occurrence loop so a pathological tiny loop length /
      // huge tempo can't spin unbounded on the audio thread.
      static constexpr int kMaxOccurrencesPerBlock = 64;

      template <class Sink> void fireNoteOn(Sink& sink, const clipEvent& e, double offBeat) {
        soundingNote* slot = nullptr;
        for (auto& n : sounding) {
          if (!n.active) {
            slot = &n;
            break;
          }
        }
        if (slot == nullptr) return; // table full — drop rather than allocate
        if (e.pitchBend != 0.f) sink.pitchWheel(e.channel, e.pitchBend);
        sink.noteOn(e.channel, e.pitch, e.velocity);
        slot->active = true;
        slot->channel = e.channel;
        slot->pitch = e.pitch;
        slot->offBeat = offBeat;
      }

      // Drain the audio-thread-retired buffers and delete them (control thread).
      void reclaimRetired();

      // Interface backpointer. Nulled by the control thread in removeInterface()
      // while the audio thread reads it in hasInterface() (mirrors #190).
      std::atomic<clip*> head;
      std::atomic<OBJECT_IMPLEMENTATION_STATE> objectStatus;

      // Bound domain clock. Resolved on the control thread in bind(); read
      // (beatPosition) on the audio thread. Caller guarantees it outlives us.
      CLOCK::domainClock* clock = nullptr;

      // Play intent (control -> audio), same handshake as MIDI::fileImpl.
      std::atomic<SOUND_STATUS> intent;
      bool started = false; // audio-thread: has playback been seeded this run?
      double lastBeat = 0.0; // audio-thread: window start (previous block end)

      std::atomic<double> loopBeats;

      // ---- event-list double buffer ----------------------------------------
      // Control thread publishes into `incoming`; the audio thread swaps it into
      // `current` at a block boundary and pushes the displaced buffer into
      // `retired` for the control thread to delete. No free ever on the audio
      // thread.
      std::atomic<const EventList*> incoming;
      const EventList* current = nullptr; // audio-thread owned
      lfQueue<const EventList*> retired;

      // Connected synths (caller-owned lifetime; fixed atomic table so
      // connect/disconnect never lock and advance() never allocates).
      static constexpr std::size_t kMaxSynths = 8;
      std::array<std::atomic<SYNTH::interfaceObject*>, kMaxSynths> synths;
    };

  } // namespace CLIP
} // namespace YSE

#endif // YSE_CLIP_CLIPTRANSPORT_H
