/*
  ==============================================================================

    lsfSoundfile.cpp
    Created: 28 Jul 2016 9:30:04pm
    Author:  yvan

  ==============================================================================
*/

#if LIBSOUNDFILE_BACKEND

#include "../internalHeaders.h"
#include <sndfile.hh>

YSE::INTERNAL::soundFile::soundFile(const std::string& fileName)
  : abstractSoundFile(fileName, true), handle(nullptr) {
  Log().sendMessage("sound added");
}

YSE::INTERNAL::soundFile::soundFile(YSE::DSP::buffer* buffer)
  : abstractSoundFile(buffer), handle(nullptr) {}

YSE::INTERNAL::soundFile::soundFile(MULTICHANNELBUFFER* buffer)
  : abstractSoundFile(buffer), handle(nullptr) {}

YSE::INTERNAL::soundFile::~soundFile() {
  // A back-buffer refill may still be queued or running on the slow pool. Let it
  // finish before we free the handle and buffers it writes to, or it would touch
  // freed memory (issue #185). join() returns immediately if nothing is in flight.
  _refillJob.join();
  if (_iBuffer != nullptr) delete[] _iBuffer;
  if (_iBufferBack != nullptr) delete[] _iBufferBack;
  if (handle != nullptr) delete handle;
}

void YSE::INTERNAL::soundFile::loadStreaming() {
  assert(handle == nullptr);
  if (IO().getActive()) {
    // Custom-IO streaming has never been implemented; the empty body leaves
    // state at LOADING. The path is unused in the supported builds.
  } else {
    handle = new SndfileHandle(fileName);
    if (*handle) {
      _sampleRateAdjustment = static_cast<Flt>(handle->samplerate()) / static_cast<Flt>(SAMPLERATE);
      _length = (Int)handle->frames();
      _channels = handle->channels();

      Int size = STREAM_BUFFERSIZE * _channels;
      _iBuffer = new Flt[size]; // front buffer (audio thread plays)
      _iBufferBack = new Flt[size]; // back buffer (slow pool prefills) — issue #185
      _streamPos = 0;

      // Prime the front buffer (buffer 0). The back-buffer prefetch is scheduled
      // by the first read() once the real loop flag is known, so we don't guess
      // it here. Mark the front buffer's real-frame count; it is never treated as
      // terminal (stop is decided by the back-buffer fills that know `loop`).
      UInt valid = fillBuffer(_iBuffer, false);
      _frontValidFrames = (Long)valid;
      _frontTerminal = false;
      _frontBufferBase = 0;
      state = READY;
    } else {
      LogImpl().emit(E_FILEREADER, "Unable to read " + fileName);
      state = INVALID;
    }
  }
}

void YSE::INTERNAL::soundFile::loadNonStreaming() {
  assert(handle == nullptr);
  void* ptr = nullptr;

  if (IO().getActive()) {
    long long size;
    bool result = INTERNAL::customFileReader::Open(fileName.c_str(), &size, &ptr);
    if (!result) {
      LogImpl().emit(E_FILEREADER, "Unable to read " + fileName);
      state = INVALID;
      return;
    }

    std::ostringstream message;
    message << "SoundFile: loading buffer ";
    message << fileName << " with size " << size;
    YSE::Log().sendMessage(message.str().c_str());

    handle = new SndfileHandle(INTERNAL::customFileReader::GetVIO(), ptr);
  } else {
    handle = new SndfileHandle(fileName);
  }

  if (*handle) {
    _sampleRateAdjustment = static_cast<Flt>(handle->samplerate()) / static_cast<Flt>(SAMPLERATE);
    _length = (Int)handle->frames();
    _channels = handle->channels();

    Int size = _length * _channels;
    _iBuffer = new Flt[size];
    Long read = handle->readf(_iBuffer, _length);

    std::ostringstream message;
    message << "SoundFile: reading from buffer ";
    message << fileName << " with size " << size << " (length " << _length << " * channels "
            << _channels << " for " << read << " bytes.";
    YSE::Log().sendMessage(message.str().c_str());

    if (read != _length) {
      LogImpl().emit(E_FILEREADER, handle->strError());
    } else {
      // file is read, but must be converted to non interleaved model
      state = READY;
    }
  } else {
    LogImpl().emit(E_FILEREADER, "Unable to read " + fileName);
    state = INVALID;
  }

  if (ptr != nullptr) INTERNAL::customFileReader::Close(ptr);
}

UInt YSE::INTERNAL::soundFile::fillBuffer(Flt* dest, Bool loop) {
  if (_needsReset.exchange(false, std::memory_order_relaxed)) {
    // Seek to the frame armed by the audio thread: 0 after a stop/reset, or the
    // requested absolute frame after a setFilePos seek (issue #217).
    Long target = _seekTarget.load(std::memory_order_relaxed);
    handle->seek(target, SEEK_SET);
    _streamPos = (Int)target;
  }
  Int framesToRead = STREAM_BUFFERSIZE;
  Flt* ptr = dest;

  while (framesToRead > 0) {
    U64 read = handle->readf(ptr, framesToRead);
    _streamPos += (UInt)read;
    framesToRead -= (UInt)read;
    if (framesToRead > 0) {
      ptr += (read * _channels);
      if (loop) {
        handle->seek(0, SEEK_SET);
        _streamPos = 0;
      } else {
        // non-loop EOF: zero-pad the remainder and report the real frame count
        UInt valid = STREAM_BUFFERSIZE - (UInt)framesToRead;
        Int zeros = framesToRead * _channels;
        while (zeros-- > 0)
          *ptr++ = 0.0f;
        _streamPos = 0;
        return valid;
      }
    } else {
      return STREAM_BUFFERSIZE;
    }
  }
  return STREAM_BUFFERSIZE;
}

#endif // LIBSOUNDFILE_BACKEND
