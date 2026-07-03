/*
  ==============================================================================

    soundFile.cpp
    Created: 28 Jan 2014 11:49:20am
    Author:  yvan

  ==============================================================================
*/

#include "../internalHeaders.h"
#include <string.h>
#include <cassert>

YSE::INTERNAL::abstractSoundFile::abstractSoundFile(bool interleaved)
  : useInterleavedBuffer(interleaved)
  , _iBuffer(nullptr)
  , _iBufferBack(nullptr)
  , _audioBuffer(nullptr)
  , _multiChannelBuffer(nullptr)
  , state(NEW)
  , _sampleRateAdjustment(1.f)
  , _channels(0)
  , _needsReset(false)
  , _frontBufferBase(0)
  , _frontValidFrames(0)
  , _frontTerminal(false)
  , idleTime(0)
{
  _refillJob.owner = this;
}

YSE::INTERNAL::abstractSoundFile::abstractSoundFile(const std::string & fileName, bool interleaved)
  : abstractSoundFile(interleaved)
{
  this->fileName = fileName;
}

YSE::INTERNAL::abstractSoundFile::abstractSoundFile(YSE::DSP::buffer * buffer)
  : abstractSoundFile(false)
{
  _audioBuffer = buffer;
}

YSE::INTERNAL::abstractSoundFile::abstractSoundFile(MULTICHANNELBUFFER * buffer)
  : abstractSoundFile(false)
{
  _multiChannelBuffer = buffer;
}

Bool YSE::INTERNAL::abstractSoundFile::create(Bool stream) {
  if (_audioBuffer != nullptr) {
    // don't load anything when using an existing buffer
    _streaming = false;
    _endReached = false;
    _needsReset = false;
    state = READY;
    return true;
  }

  _streaming = stream;
  _endReached = false;
  
  // load sound into memory
  _needsReset = false;

  if (state == NEW) {
    state = LOADING;
    Global().addSlowJob(this);
  } 
  return true;
}

Bool YSE::INTERNAL::abstractSoundFile::read(std::vector<DSP::buffer> & filebuffer, Flt& pos, UInt length, Flt speed, Bool loop, SOUND_STATUS & intent, Flt & volume) {
  if (useInterleavedBuffer) return readInterleaved(this, filebuffer, pos, length, speed, loop, intent, volume);
  else return readNonInterleaved(this, filebuffer, pos, length, speed, loop, intent, volume);
}

void YSE::INTERNAL::abstractSoundFile::requestRefill(Bool loop) {
  // Called from the audio thread. Publishes the current loop flag and schedules
  // at most one back-buffer refill on the slow pool. Never touches the disk.
  _streamLoop.store(loop, std::memory_order_relaxed);
  // A filled back buffer is already waiting to be swapped in — nothing to do.
  if (_backReady.load(std::memory_order_acquire)) return;
  // Schedule exactly one refill; _refillInFlight is cleared by fillBackBuffer().
  Bool expected = false;
  if (_refillInFlight.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
    Global().addSlowJob(&_refillJob);
  }
}


Bool YSE::INTERNAL::abstractSoundFile::readNonInterleaved(abstractSoundFile * file, std::vector<DSP::buffer> & filebuffer, Flt& pos, UInt length, Flt speed, Bool loop, SOUND_STATUS & intent, Flt & volume) {
  /** Yes, this function uses goto...
      It is highly optimized for speed and this is the best way I could find
      to ensure good performance. Suggestions are welcome.
  */

  if (file->state != READY) return false;

  // adjust speed for sample rate
  speed *= file->_sampleRateAdjustment;

  // Non-interleaved sources (audio-buffer / multichannel) are never streaming;
  // streaming always uses the interleaved path, so no disk refill happens here
  // (issue #185). Guard it so a future change can't reintroduce a blocking read.
  assert(!file->_streaming);

  // this is for a smooth fade-in to avoid glitches
  if (intent == SS_WANTSTOPLAY) {
    intent = SS_PLAYING;
    volume = 0.f;
  }

  Flt startPos = pos;
  YSE::SOUND_STATUS startIntent = intent;
  Flt startVolume = volume;
  
  // Now start filling the channels.
  // Most sounds will probably only have one channel. At any rate, it is 
  // assumed that soundFile and soundImplementation have the same amount
  // of channels.
  FOREACH (filebuffer) {
    // these are to reset to starting point in case of multi channel sounds
    pos = startPos;
    volume = startVolume;
    intent = startIntent;

    // this assumes the output and file have the same number of channels
    Flt * out = filebuffer[i].getPtr();
    const Flt * in;
    
    if (file->_audioBuffer) {
      in = file->_audioBuffer->getPtr();
      file->_length = file->_audioBuffer->getLength();
    }
    else if (file->_multiChannelBuffer) {
      in = file->_multiChannelBuffer->at(i).getPtr();
      file->_length = file->_multiChannelBuffer->at(i).getLength();
    }
    else  {
      in = file->_buffer[i];
    }

    UInt l = length;

  startAgain:
    // evaluate intent and see what course to take
    while (l > 0) {
      
      // if intent is stopped or paused, we can just fill the rest of the 
      // buffer with zeroes. Normally, stopped or paused sounds are not
      // even processed, but we get here when a sounds starts as 'wants_to_pause',
      // fades out to zero and still has to fill the rest of the buffer.
      if (intent == SS_STOPPED || intent == SS_PAUSED) {
        // fill the rest with zero's
        for (; l > 7; l -= 8, out += 8) {
          out[0] = 0.f;     out[1] = 0.f;
          out[2] = 0.f;     out[3] = 0.f;
          out[4] = 0.f;     out[5] = 0.f;
          out[6] = 0.f;     out[7] = 0.f;
        }
        while (l--) *out++ = 0.f;
        goto nextBuffer; // this output channel buffer is full if we get here
      } 

      // Most of the time a sound will just play at full volume. This has nothing
      // to do with the sound volume itself (which will be applied afterwards) but
      // with sounds fading in and out at start, stop and pause (to avoid glitches).
      // Therefore, this is the most important part to optimize for speed.
      else if (intent == SS_PLAYING_FULL_VOLUME) {
      mainLoop:

        // if playing backwards and we're past the beginning of the soundFile in
        // less than 16 steps, move one by one
        if ((speed < 0) && ((pos + speed * 16) < 0)) {
          while (l--) {
            *out++ = in[(UInt)pos];
            pos += speed;
            // check if we're past the beginning and readjust position if so
            if (pos < 0) goto calibrate;
          }
          goto nextBuffer; // this output channel buffer is full if we get here
        }

        // if playing forward and we're past the end of the soundFile in 
        // less than 16 steps, move one by one
        else if ((speed > 0) && ((pos + speed * 16) >= (file->_streaming ? STREAM_BUFFERSIZE : file->_length))) {
          while (l--) {
            *out++ = in[(UInt)pos];
            pos += speed;
            // check if we're past the end and readjust position if so
            if (pos >= (file->_streaming ? STREAM_BUFFERSIZE : file->_length)) goto calibrate;
          }
          goto nextBuffer; // this output channel buffer is full if we get here
        }

        // at this point, we can be sure our current position allows for 
        // 16 more frames without recalibration
        else {
          // if the output still needs more than 16 frames, add 16 at once
          if (l > 15) {
            out[ 0] = in[(UInt)pos]; pos += speed;
            out[ 1] = in[(UInt)pos]; pos += speed;
            out[ 2] = in[(UInt)pos]; pos += speed;
            out[ 3] = in[(UInt)pos]; pos += speed;
            out[ 4] = in[(UInt)pos]; pos += speed;
            out[ 5] = in[(UInt)pos]; pos += speed;
            out[ 6] = in[(UInt)pos]; pos += speed;
            out[ 7] = in[(UInt)pos]; pos += speed;
            out[ 8] = in[(UInt)pos]; pos += speed;
            out[ 9] = in[(UInt)pos]; pos += speed;
            out[10] = in[(UInt)pos]; pos += speed;
            out[11] = in[(UInt)pos]; pos += speed;
            out[12] = in[(UInt)pos]; pos += speed;
            out[13] = in[(UInt)pos]; pos += speed;
            out[14] = in[(UInt)pos]; pos += speed;
            out[15] = in[(UInt)pos]; pos += speed;
            
            l -= 16; out += 16;
            
            // check if we're past the end and readjust position if so
            if (pos < 0 || pos >= (file->_streaming ? STREAM_BUFFERSIZE : file->_length)) goto calibrate;
            
            // since l > 15, the output buffer is not full yet
            goto mainLoop;
          }
          // almost at the end of the output buffer, handle one
          // frame at a time now
          else {
            while (l-- > 0) {
              *out++ = in[(UInt)pos]; pos += speed;
              
              // check if we're past the end and readjust position if so
              if (pos < 0 || pos >= (file->_streaming ? STREAM_BUFFERSIZE : file->_length)) goto calibrate;
            }
          }
          goto nextBuffer; // this output channel buffer is full if we get here
        }
      }

      // Sound plays, but not at full volume. So we're fading in
      else if (intent == SS_PLAYING) {
        while (l--) {
          *out++ = in[(UInt)pos] * volume;
          pos += speed;
          volume += 0.005f;
          if (pos < 0 || pos >= (file->_streaming ? STREAM_BUFFERSIZE : file->_length)) goto calibrate;
          if ((volume >= 1.f)) {
            // full volume is reached. Move to the optimized version
            volume = 1.f;
            intent = SS_PLAYING_FULL_VOLUME;
            goto startAgain;
          }
        }
        goto nextBuffer; // this output channel buffer is full if we get here
      }

      // Still playing, but fading out to pause
      else if (intent == SS_WANTSTOPAUSE) {
        while (l--) {
          *out++ = in[(UInt)pos] * volume;
          pos += speed;
          volume -= 0.005f;
          if (pos < 0 || pos >= (file->_streaming ? STREAM_BUFFERSIZE : file->_length)) goto calibrate;
          if ((volume <= 0.f)) {
            // fade out complete, switch intent to paused and restart
            // The rest of the buffer will be filled with zeroes
            volume = 0.f;
            intent = SS_PAUSED;
            goto startAgain;
          }
        }
        goto nextBuffer; // this output channel buffer is full if we get here
      }

      // Still playing, but fading out to stop
      else if (intent == SS_WANTSTOSTOP) {
        while (l--) {
          *out++ = in[(UInt)pos] * volume;
          pos += speed;
          volume -= 0.005f;
          if (pos < 0 || pos >= (file->_streaming ? STREAM_BUFFERSIZE : file->_length)) goto calibrate;
          if ((volume <= 0.f)) {
            // fade out complete, switch intent to stopped and restart
            // The rest of the buffer will be filled with zeroes
            volume = 0.f;
            intent = SS_STOPPED;
            goto startAgain;
          }
        }
        goto nextBuffer; // this output channel buffer is full if we get here
      }

calibrate:
      {
        // if we get here, pos is past the end or before the beginning
        // recalibrate position now. We can't simply set it to the end
        // or the beginning because this won't work with speed <> 1
        while (pos < 0) pos += file->_length; // looping backwards
        if (pos >= file->_length) {
          if (loop) while (pos >= file->_length) pos -= file->_length;
          else {
            // if no loop, fill the rest of the buffer with zero's
            pos = 0;
            intent = SS_STOPPED;
          }
        }
      }
    }

    // This label creates a safe point to start processing the next channel when the
    // current one is done
    nextBuffer: ;
  }

  // make sure position is reset to zero if playing has stopped during this read
  if (intent == SS_STOPPED) {
    pos = 0;
  }
  return true;
}


// Try to bring the prefetched back buffer in as the new front buffer. Runs on
// the audio thread; never touches the disk. See issue #185.
YSE::INTERNAL::abstractSoundFile::swapResult
YSE::INTERNAL::abstractSoundFile::streamSwap(abstractSoundFile * file, Flt & pos, Flt & streamEnd, Bool loop) {
  if (file->_frontTerminal) return SWAP_TERMINAL;

  // Drop a back buffer prefetched before a stop (its stream position is stale)
  // so it is refilled from the current position instead of played on restart.
  if (file->_backReady.load(std::memory_order_acquire)
      && file->_backGen.load(std::memory_order_relaxed) != file->_fillGen.load(std::memory_order_relaxed)) {
    file->_backReady.store(false, std::memory_order_relaxed);
  }

  if (!file->_backReady.load(std::memory_order_acquire)) {
    file->requestRefill(loop); // prefetch not ready yet — caller emits silence
    return SWAP_UNDERRUN;
  }

  // Publish-safe: _backValidFrames/_backTerminal were written before the
  // _backReady release store we just acquired above.
  Long consumed = file->_frontValidFrames;
  Flt * tmp = file->_iBuffer;
  file->_iBuffer = file->_iBufferBack;
  file->_iBufferBack = tmp;
  file->_frontValidFrames = file->_backValidFrames.load(std::memory_order_relaxed);
  file->_frontTerminal = file->_backTerminal.load(std::memory_order_relaxed);
  file->_frontBufferBase += consumed;
  file->_backReady.store(false, std::memory_order_relaxed);
  pos -= (Flt)consumed;
  streamEnd = (Flt)file->_frontValidFrames;
  file->requestRefill(loop); // prefetch the next buffer
  return SWAP_DONE;
}

// Called on stop (audio thread): reset to frame 0 and arm an async re-prime so
// the next play starts at the beginning. reset() empties the front state and
// bumps the fill generation; requestRefill schedules the from-0 refill now so a
// prompt restart finds it ready.
void YSE::INTERNAL::abstractSoundFile::streamReprime(abstractSoundFile * file, Bool loop) {
  file->reset();
  file->requestRefill(loop);
}

//////////////////////////////////////////////////////////////////////////////////
// interleaved read function
//////////////////////////////////////////////////////////////////////////////////

Bool YSE::INTERNAL::abstractSoundFile::readInterleaved(abstractSoundFile * file, std::vector<DSP::buffer> & filebuffer, Flt& pos, UInt length, Flt speed, Bool loop, SOUND_STATUS & intent, Flt & volume) { // NOSONAR S3776: audio-thread inner-loop state machine, profiled — splitting it inserts function-call overhead per sample
  /** Yes, this function uses goto...
  It is highly optimized for speed and this is the best way I could find
  to ensure good performance. Suggestions are welcome.
  */

  if (file->state != READY) return false;

  // adjust speed for sample rate
  speed *= file->_sampleRateAdjustment;

  // don't play streaming sounds backwards
  if (file->_streaming && speed < 0) speed = 0;

  // For streaming, `pos` is buffer-local in [0, streamEnd) where streamEnd is the
  // count of real frames in the current front buffer (issue #185). For other
  // sources the per-sample checks below use file->_length directly.
  Flt streamEnd = (Flt)file->_frontValidFrames;

  if (file->_streaming) {
    // Keep the refill pipeline primed (schedules at most one slow-pool fill).
    file->requestRefill(loop);
    // Resolve any buffer boundary reached on a previous block *before* indexing
    // into the front buffer in the state machine below. Never blocks on disk.
    while (pos >= streamEnd) {
      swapResult r = streamSwap(file, pos, streamEnd, loop);
      if (r == SWAP_DONE) continue;
      // Terminal (EOF) or underrun: emit a silent block and retry next callback.
      FOREACH(filebuffer) {
        Flt * o = filebuffer[i].getPtr();
        for (UInt k = 0; k < length; ++k) o[k] = 0.f;
      }
      if (r == SWAP_TERMINAL) {
        intent = SS_STOPPED;
        pos = 0;
        streamReprime(file, loop);
      }
      return true;
    }
  }

  // this is for a smooth fade-in to avoid glitches
  if (intent == SS_WANTSTOPLAY) {
    intent = SS_PLAYING;
    volume = 0.f;
  }

  // set cursor to the output buffer start
  FOREACH(filebuffer) filebuffer[i].cursor = filebuffer[i].getPtr();

  for (UInt x = 0; x < length; ) { // x is updated within loop, when increasing cursor // NOSONAR S1751: loop body iterates via internal cursor advance; goto-based state machine — see function-level S3776 justification

    // set position in filebuffer, according to nr of channels
    //UInt channelPos = ((UInt)pos) * file->_channels;

    const Flt * in;

    if (file->_audioBuffer) {
      in = file->_audioBuffer->getPtr();
      file->_length = file->_audioBuffer->getLength();
    }
    else {
      in = file->_iBuffer;
    }

    startAgain:

    if (intent == SS_STOPPED || intent == SS_PAUSED) {
      // fill the rest with zero's
      while (x < length) {
        FOREACH(filebuffer) *filebuffer[i].cursor++ = 0;
        x++;
      }
      break;
    }

    // Most of the time a sound will just play at full volume. This has nothing
    // to do with the sound volume itself (which will be applied afterwards) but
    // with sounds fading in and out at start, stop and pause (to avoid glitches).
    // Therefore, this is the most important part to optimize for speed.
    else if (intent == SS_PLAYING_FULL_VOLUME) {
    //mainLoop:

      while (x < length) {
        FOREACH(filebuffer) *filebuffer[i].cursor++ = (in[((UInt)pos) * file->_channels + i]);
        pos += speed;
        x++;

				if (pos < 0 || pos >= (file->_streaming ? streamEnd : file->_length)) { // NOSONAR S134: nesting follows the state-machine structure documented at function level
					goto calibrate;
				}
      }
    }

    // Sound plays, but not at full volume. So we're fading in
    else if (intent == SS_PLAYING) {
      while (x < length) {
        FOREACH(filebuffer) *filebuffer[i].cursor++ = (in[((UInt)pos) * file->_channels + i]) * volume;
        pos += speed;
        volume += 0.005f;
        x++;

        if (pos < 0 || pos >= (file->_streaming ? streamEnd : file->_length)) goto calibrate;

        if ((volume >= 1.f)) {
          // full volume is reached. Move to the optimized version
          volume = 1.f;
          intent = SS_PLAYING_FULL_VOLUME;
          goto startAgain;
        }
      }
    }

    // Still playing, but fading out to pause
    else if (intent == SS_WANTSTOPAUSE) {
      while (x < length) {
        FOREACH(filebuffer) *filebuffer[i].cursor++ = (in[((UInt)pos) * file->_channels + i]) * volume;
        pos += speed;
        volume -= 0.005f;
        x++;

        if (pos < 0 || pos >= (file->_streaming ? streamEnd : file->_length)) goto calibrate;

        if ((volume <= 0.f)) {
          // fade out complete, switch intent to paused and restart
          // The rest of the buffer will be filled with zeroes
          volume = 0.f;
          intent = SS_PAUSED;
          goto startAgain;
        }
      }
    }

    // Still playing, but fading out to stop
    else if (intent == SS_WANTSTOSTOP) {
      while (x < length) {
        FOREACH(filebuffer) *filebuffer[i].cursor++ = (in[((UInt)pos) * file->_channels + i]) * volume;
        pos += speed;
        volume -= 0.005f;
        x++;

        if (pos < 0 || pos >= (file->_streaming ? streamEnd : file->_length)) goto calibrate;

        if ((volume <= 0.f)) {
          // fade out complete, switch intent to paused and restart
          // The rest of the buffer will be filled with zeroes
          volume = 0.f;
          intent = SS_STOPPED;
          goto startAgain;
        }
      }
    }

calibrate:
    if (file->_streaming) {
      // Boundary reached mid-block. Swap in prefetched buffers (never blocks on
      // disk — see issue #185). On underrun, zero-fill the rest of this block and
      // resume next callback. On EOF, stop.
      Bool silence = false;
      while (pos >= streamEnd) {
        swapResult r = streamSwap(file, pos, streamEnd, loop);
        if (r == SWAP_DONE) {
          in = file->_iBuffer; // the front buffer pointer changed
          continue;
        }
        if (r == SWAP_TERMINAL) {
          pos = 0;
          intent = SS_STOPPED;
          volume = 0.f;
        }
        else {
          silence = true; // underrun
        }
        break;
      }
      if (silence) {
        while (x < length) {
          FOREACH(filebuffer) *filebuffer[i].cursor++ = 0;
          x++;
        }
      }
    }
    else
    {
      // if we get here, pos is past the end or before the beginning
      // recalibrate position now. We can't simply set it to the end
      // or the beginning because this won't work with speed <> 1
      while (pos < 0) pos += file->_length; // looping backwards
      if (pos >= file->_length) {
        if (loop) while (pos >= file->_length) pos -= file->_length;
        else {
          // if no loop, fill the rest of the buffer with zero's
          pos = 0;
          intent = SS_STOPPED;
        }
      }
    }

    if (x >= length) {
      break;
    }

    goto startAgain;
  }

  // make sure position is reset to zero if playing has stopped during this read
  if (intent == SS_STOPPED) {
    pos = 0;
    // For a stopped stream, arm an async re-prime from frame 0 so the next play
    // starts at the beginning. No disk I/O on the audio thread (issue #185).
    if (file->_streaming) streamReprime(file, loop);
  }
  return true;
}

Bool YSE::INTERNAL::abstractSoundFile::contains(const std::string & fileName) {
  return strcmp(this->fileName.c_str(), fileName.c_str()) == 0;
}

Bool YSE::INTERNAL::abstractSoundFile::contains(YSE::DSP::buffer * buffer) {
  return this->_audioBuffer == buffer;
}

Bool YSE::INTERNAL::abstractSoundFile::contains(MULTICHANNELBUFFER * buffer) {
  return this->_multiChannelBuffer == buffer;
}

YSE::INTERNAL::abstractSoundFile::~abstractSoundFile() {
}

Int YSE::INTERNAL::abstractSoundFile::channels() {
  return _channels;
}

UInt YSE::INTERNAL::abstractSoundFile::length() {
  return _length;
}

YSE::INTERNAL::FILESTATE YSE::INTERNAL::abstractSoundFile::getState() {
  return state;
}

YSE::INTERNAL::abstractSoundFile & YSE::INTERNAL::abstractSoundFile::reset() {
  // Called on stop (audio thread only — from read() and dspFunc_parseIntent).
  // Resets to frame 0 for the next play: the audio-owned front state is emptied
  // so the next read() swaps in a freshly-filled buffer, the next disk fill seeks
  // back to 0, and the generation is bumped so any fill that started before this
  // stop (stale position) is discarded rather than played on restart. The
  // prefetched back buffer is dropped for the same reason. (issue #185)
  _frontValidFrames = 0;
  _frontTerminal = false;
  _frontBufferBase = 0;
  _needsReset.store(true, std::memory_order_relaxed);
  _fillGen.fetch_add(1, std::memory_order_release);
  _backReady.store(false, std::memory_order_relaxed);
  return *this;
}

bool YSE::INTERNAL::abstractSoundFile::inUse(Flt dt) {
  // Runs on the slow-pool GC job (issue #186), never the audio thread. The idle
  // timer is advanced by the elapsed time the caller measured between GC passes
  // (previously Time().delta() accumulated per audio callback). clientList is
  // guarded because attach/release run on other threads.
  std::scoped_lock lk(clientListMutex);
  if (clientList.empty()) {
    idleTime += dt;
  }
  if (idleTime > 30) {
    return false;
  }
  return true;
}

void YSE::INTERNAL::abstractSoundFile::attach(YSE::SOUND::implementationObject * impl) {
  std::scoped_lock lk(clientListMutex);
  for (auto i = clientList.begin(); i != clientList.end(); ++i) {
    if (*i == impl) return;
  }
  clientList.emplace_front(impl);
}

void YSE::INTERNAL::abstractSoundFile::release(SOUND::implementationObject *impl) {
  std::scoped_lock lk(clientListMutex);
  auto previous = clientList.before_begin();
  for (auto i = clientList.begin(); i != clientList.end(); ++i) {
    if(*i == impl) {
      clientList.erase_after(previous);
      return;
    }
    previous++;
  }
  // this point should not be reached
  assert(false);
}

void YSE::INTERNAL::abstractSoundFile::run() {
  if (_streaming) loadStreaming();
  else loadNonStreaming();
}
