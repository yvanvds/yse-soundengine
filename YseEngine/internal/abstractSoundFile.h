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

      virtual ~abstractSoundFile();

      Bool create(Bool stream = false);
      void run(); // load from disk
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

      // set
      abstractSoundFile& reset(); // indicate that stream needs to reset (after stop)

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

      // Streaming refill is done off the audio callback thread (issue #185). The
      // audio thread never touches the disk: it plays the front buffer and, at a
      // buffer boundary, swaps in a back buffer that the slow pool prefilled. This
      // helper (called from the audio thread's read()) makes sure exactly one
      // refill of the back buffer is scheduled on the slow pool.
      void requestRefill(Bool loop);
      // Fill the back buffer from disk. Runs on the slow pool via _refillJob;
      // must never be called on the audio thread. Implemented by the backend.
      virtual void fillBackBuffer() = 0;

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
    };

  } // namespace INTERNAL
} // namespace YSE

#endif // ABSTRACTSOUNDFILE_H_INCLUDED
