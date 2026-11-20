/*
  ==============================================================================

    abstractSoundFile.h
    Created: 28 Jan 2014 11:49:20am
    Author:  yvan

  ==============================================================================
*/

#ifndef ABSTRACTSOUNDFILE_H_INCLUDED
#define ABSTRACTSOUNDFILE_H_INCLUDED

#include "../classes.hpp"
#include "../headers/types.hpp"
#include "../dsp/buffer.hpp"
#include "../headers/enums.hpp"
#include "customFileReader.h"
#include <forward_list>
#include <atomic>
#include <cstdint>
#include <mutex>
#include "threadPool.h"

namespace YSE {

  namespace INTERNAL {

    enum FILESTATE {
      NEW, // don't access from within the audio callback!
      LOADING, // don't access from within the audio callback!
      READY, // at this point a soundfile should only be accessed inside the audio callback
      INVALID, // don't access from within the audio callback!
    };

    class abstractSoundFile : public threadPoolJob {
    public:
      // abstractSoundFile(const File               & file    );
      abstractSoundFile(const std::string& fileName, bool interleaved);

      abstractSoundFile(YSE::DSP::buffer* buffer);
      abstractSoundFile(MULTICHANNELBUFFER* buffer);

      // will be called by constructor

      ~abstractSoundFile() override;

      Bool create(Bool stream = false);
      void run() override; // load from disk
      virtual void loadStreaming() = 0;
      virtual void loadNonStreaming() = 0;

      Bool read(std::vector<DSP::buffer>& filebuffer, Flt& pos, UInt length, Flt speed, Bool loop,
                SOUND_STATUS& intent, Flt& volume);

      // Bool contains(const File & file);
      virtual Bool contains(const std::string& fileName);
      virtual Bool contains(YSE::DSP::buffer* buffer);
      virtual Bool contains(MULTICHANNELBUFFER* buffer);

      // get
      Int channels(); // number of channels in source
      UInt length(); // length of the source in frames
      FILESTATE getState(); // current state of the file, (ready, invalid or loading)

      // Live-instance accounting for tests (issue #218). Returns the number of
      // abstractSoundFile objects currently alive, so a test can assert that a
      // streaming sound's per-impl soundFile is actually freed on teardown (the
      // leak was that ~implementationObject never deleted it). Incremented in
      // the base constructor and decremented in the destructor — both run off
      // the audio thread (main thread / slow pool), so this counter is never
      // touched on any real-time path.
      static long liveInstances();

      // set
      abstractSoundFile& reset(); // indicate that stream needs to reset (after stop)
      // Seek a streaming sound to an absolute frame (issue #217). Called on the
      // audio thread: it never touches the disk. It resets the audio-owned front
      // state and arms an async re-prime from `frame` on the slow pool, so the
      // next read() swaps in a buffer filled from the requested position. Mirrors
      // reset()/streamReprime but with an arbitrary target frame instead of 0.
      // With `backward` (a reversible() stream, issue #1288) the fill reads the
      // frames before `frame` instead, and the reader starts from a `pos` of -1:
      // the first frame it plays is `frame - 1`.
      void seek(Long frame, Bool loop, Bool backward = false);

      // ── Stream access for DSP::fileStreamPlayer (issue #1155) ──────────────
      //
      // The player reads a stream through read(), as a streaming sound does,
      // and opens and positions it with these. Disk work stays where it was:
      // loadStream() and prime() block on the calling thread, which is never
      // the audio thread; seek() and reset() only arm a refill.

      // Open the file as a stream on the calling thread, now: what create(true)
      // has the slow pool do. Only on a new object; true when it is READY.
      Bool loadStream();
      // The file's own sample rate in Hz, 0 before it is loaded.
      Int sampleRate() const {
        return _sourceRate;
      }
      // The speed factor read() applies, fixed when the file was loaded.
      Flt sampleRateAdjustment() const {
        return _sampleRateAdjustment;
      }
      // The frames [start, end) a stream plays: a fill stops, or with loop on
      // goes on from `start`, at `end` instead of at the end of the file, and
      // reset() returns to `start`. An `end` below 0 is the end of the file.
      // The whole file until this is called. Takes effect from the next disk
      // fill, so a caller seeks after it.
      void region(Long start, Long end);
      // Fill the front buffer from `frame` on the calling thread, before any
      // read(): a stream that starts at once from there. With `loop` the fill
      // wraps at the region's end, so a region shorter than a block loops from
      // the first block on; a reader that does not loop then ends the stream
      // at the region's end itself. Never the audio thread.
      // With `backward` (issue #1288) it fills the frames before `frame`, read
      // backward and wrapping at the region's start, as seek(frame, loop, true)
      // has the slow pool do: the reader starts from a `pos` of -1.
      void prime(Long frame, Bool loop, Bool backward = false);
      // Whether a negative speed plays the stream backward (issue #1288). Off,
      // read() holds a stream at a negative speed, as a streaming YSE::sound
      // always has. Set before the audio thread reads.
      void reversible(Bool on) {
        _reversible = on;
      }
      // Audio thread: whether the stream reads backward now. A read() at a
      // speed of the other sign turns it; seek() and prime() set it.
      Bool streamBackward() const {
        return _streamBackward.load(std::memory_order_relaxed);
      }
      // Audio thread: the stream's frame for the buffer-local `pos` read()
      // keeps, counted on from the frame the last seek, reset or prime started
      // at (a looping stream's count runs past its region's end).
      Dbl streamPosition(Flt pos) const {
        return static_cast<Dbl>(_frontBufferBase) + static_cast<Dbl>(pos);
      }

      // to keep track of clients
      void attach(SOUND::implementationObject* impl);
      void release(SOUND::implementationObject* impl);
      // Advance the idle timer by `dt` seconds and report whether this file is
      // still in use (has clients, or hasn't been idle long enough to GC).
      // Runs on the slow-pool GC job (issue #186), never the audio thread.
      bool inUse(Flt dt);

    protected:
      static Bool readNonInterleaved(abstractSoundFile* file, std::vector<DSP::buffer>& filebuffer,
                                     Flt& pos, UInt length, Flt speed, Bool loop,
                                     SOUND_STATUS& intent, Flt& volume);
      static Bool readInterleaved(abstractSoundFile* file, std::vector<DSP::buffer>& filebuffer,
                                  Flt& pos, UInt length, Flt speed, Bool loop, SOUND_STATUS& intent,
                                  Flt& volume);

      // Streaming buffer-boundary handling for readInterleaved (issue #185).
      enum swapResult { SWAP_DONE, SWAP_TERMINAL, SWAP_UNDERRUN };
      // Try to swap the prefetched back buffer in as the new front buffer. On
      // SWAP_DONE, pos/streamEnd are advanced into the new buffer. SWAP_TERMINAL
      // means the current front buffer was the final one (EOF, no loop).
      // SWAP_UNDERRUN means the prefetch has not landed yet (caller emits silence).
      static swapResult streamSwap(abstractSoundFile* file, Flt& pos, Flt& streamEnd, Bool loop);
      // Arm an async re-prime from frame 0 for the next play (called on stop).
      static void streamReprime(abstractSoundFile* file, Bool loop);
      // Turn a reversible stream around (issue #1288), on the audio thread: the
      // front buffer stays, the back buffer prefetched the old way is dropped,
      // and the next fill reads on from the front buffer's other edge.
      static void streamTurn(abstractSoundFile* file, Flt& pos, Bool backward, Bool loop);
      // A frame of the stream's count (which runs past the region's end
      // forward and below its start backward) as a frame of the file, inside
      // the region; a frame already inside, its ends included, is kept.
      Long streamFold(Long frame) const;
      // The resolved region: `last` the end of the file when region() left it
      // open.
      void regionBounds(Long& first, Long& last) const;

      // Streaming refill is done off the audio callback thread (issue #185). The
      // audio thread never touches the disk: it plays the front buffer and, at a
      // buffer boundary, swaps in a back buffer that the slow pool prefilled. This
      // helper (called from the audio thread's read()) makes sure exactly one
      // refill of the back buffer is scheduled on the slow pool.
      void requestRefill(Bool loop);
      // Fill the back buffer from disk and publish it to the audio thread. Runs
      // on the slow pool via _refillJob; must never be called on the audio
      // thread. The generation-tagged publication protocol (issue #185) and the
      // reset re-arm (issue #283) live here in the base; the actual disk fill is
      // the backend's fillBuffer().
      void fillBackBuffer();
      // Backend disk fill: write up to STREAM_BUFFERSIZE frames into `dest` from
      // the current stream position, honoring _needsReset/_seekTarget, the
      // region (issue #1155) and the loop flag, zero-padding the tail at a
      // non-loop end of the region (the file's end unless region() narrowed it). Returns the number
      // of real (non-padded) frames written. Does blocking disk I/O — slow pool / load only, never
      // the audio thread (issue #185).
      // With `backward` (issue #1288) the frames are the ones before the stream
      // position, in file order, wrapping from the region's start to its end:
      // read from its last frame down, `dest` plays on backward from the stream
      // position. A non-loop start of the region moves the real frames to the
      // front of `dest` and pads after them, as the forward fill does.
      virtual UInt fillBuffer(Flt* dest, Bool loop, Bool backward) = 0;

      // Slow-pool job that drives fillBackBuffer() for this file. A dedicated job
      // (rather than reusing the abstractSoundFile load job) is required: the load
      // job's run() reopens the handle and reallocates the buffers.
      class streamRefillJob : public threadPoolJob {
      public:
        abstractSoundFile* owner = nullptr;
        void run() override {
          owner->fillBackBuffer();
        }
      };
      streamRefillJob _refillJob;

      std::string fileName;

      // pointer to a (multichannel) soundfile
      Bool useInterleavedBuffer;
      const Flt** _buffer; // this version is used when a non interleaved buffer is used
      Flt* _iBuffer; // front buffer (audio thread reads); interleaved-buffer path
      Flt* _iBufferBack; // back buffer (slow pool fills) for streaming double buffering

      // used for passing an audio buffer as a sound source
      YSE::DSP::buffer* _audioBuffer;
      MULTICHANNELBUFFER* _multiChannelBuffer;

      std::atomic<FILESTATE> state;

      Flt _sampleRateAdjustment;
      int _length;
      int _channels;

      // for streaming
      Bool _streaming;
      UInt _streamSize;
      Bool _endReached;
      Int _streamPos;
      // Written by the audio thread (reset() on stop), read/cleared by the slow
      // pool (fillBackBuffer) — must be atomic now that the fill is off-thread.
      std::atomic<Bool> _needsReset;
      // Absolute frame the next disk fill should seek to when _needsReset is
      // observed. Written by the audio thread (reset() -> 0, seek() -> target),
      // read by the slow pool in fillBuffer(). Atomic for the cross-thread hand-
      // off. (issue #217)
      std::atomic<Long> _seekTarget{0};
      // The frames a stream plays (issue #1155): set by region(), read by the
      // disk fill and by reset(). End below 0: the end of the file.
      std::atomic<Long> _regionStart{0};
      std::atomic<Long> _regionEnd{-1};
      // The file's sample rate, published by the loader.
      Int _sourceRate = 0;
      // Backward streaming (issue #1288). _reversible is set before the audio
      // thread reads. _streamBackward is written by the audio thread (read(),
      // seek()) and by prime() before it reads, and read by the disk fill after
      // its acquire load of _fillGen: every change of direction bumps the
      // generation after storing it.
      Bool _reversible = false;
      std::atomic<Bool> _streamBackward{false};

      // Double-buffer publication (issue #185). Writer = slow pool, reader = audio.
      // _backValidFrames/_backTerminal/_backGen are written before the _backReady
      // release store and read after the audio thread's acquire load of it.
      std::atomic<Bool> _backReady{false}; // back buffer filled, ready to swap in
      std::atomic<Bool> _backTerminal{false}; // back fill hit non-loop EOF (last buffer)
      std::atomic<Bool> _refillInFlight{false}; // a refill is queued/running (dedup)
      std::atomic<Bool> _streamLoop{false}; // loop flag published to the slow pool
      std::atomic<Long> _backValidFrames{0}; // real (non-padded) frames in back buffer
      // Generation guard for stop/restart: reset() bumps _fillGen; each fill tags
      // its result with the gen it observed. The audio thread accepts a swap only
      // when the tag still matches, so a fill that started before a stop (stale
      // position) is discarded instead of played on restart.
      std::atomic<uint32_t> _fillGen{0};
      std::atomic<uint32_t> _backGen{0};
      // Audio-thread-owned: absolute frame index of the front buffer's first
      // sample, the real frame count in the front buffer, and whether the front
      // buffer is the final (EOF) one.
      Long _frontBufferBase;
      Long _frontValidFrames;
      Bool _frontTerminal;

      // for keeping track of objects using this file
      // clientList is crossed by three roles: attach (main thread, from
      // implementationObject::create), release (slow pool, from
      // ~implementationObject) and the idle check in inUse() (slow-pool GC job,
      // issue #186). None run on the audio thread, so this mutex is RT-safe.
      std::forward_list<SOUND::implementationObject*> clientList;
      std::mutex clientListMutex;
      Flt idleTime;

    private:
      // default constructor should only be used internally
      abstractSoundFile(bool interleaved);

      // Count of live abstractSoundFile instances (issue #218 test oracle).
      // Never mutated on the audio thread — see liveInstances() above.
      static std::atomic<long> s_liveInstances;
    };

  } // namespace INTERNAL
} // namespace YSE

#endif // ABSTRACTSOUNDFILE_H_INCLUDED
