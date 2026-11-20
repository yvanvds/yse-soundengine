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
  // Only a custom-IO stream holds a reader handle open past its loader; it backs
  // the SndfileHandle's virtual IO, so it is closed after that handle is deleted
  // (issue #823).
  if (_ioHandle != nullptr) INTERNAL::customFileReader::Close(_ioHandle);
}

void YSE::INTERNAL::soundFile::loadStreaming() {
  assert(handle == nullptr);
  if (IO().getActive()) {
    // Custom-IO streaming (issue #823). The user's reader already supplies every
    // primitive sndfile's virtual IO needs — open, read, seek, tell, length — so
    // a stream is opened exactly like a disk file, just through the VIO.
    //
    // The one difference from loadNonStreaming() is ownership: that path reads
    // the whole source once and closes the reader handle before returning, while
    // a stream keeps reading from the slow pool for as long as the sound lives.
    // The handle is sndfile's user data for every later readf()/seek(), so it is
    // kept in _ioHandle and closed by ~soundFile, after the SndfileHandle it
    // backs is gone.
    long long size = 0;
    if (!INTERNAL::customFileReader::Open(fileName.c_str(), &size, &_ioHandle)) {
      LogImpl().emit(E_FILEREADER, "Unable to read " + fileName);
      state = INVALID;
      return;
    }
    handle = new SndfileHandle(INTERNAL::customFileReader::GetVIO(), _ioHandle);
  } else {
    handle = new SndfileHandle(fileName);
  }

  if (*handle) {
    _sampleRateAdjustment = static_cast<Flt>(handle->samplerate()) / static_cast<Flt>(SAMPLERATE);
    _sourceRate = handle->samplerate();
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
    _sourceRate = handle->samplerate();
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
    // Seek to the frame armed by the audio thread: the region's start after a
    // stop/reset, or the requested absolute frame after a setFilePos seek
    // (issue #217).
    Long target = _seekTarget.load(std::memory_order_relaxed);
    handle->seek(target, SEEK_SET);
    _streamPos = (Int)target;
  }
  // The frames this stream plays (issue #1155): the whole file unless
  // region() narrowed it. Reaching `last` is what reaching the end of the file
  // was: wrap to `first` when looping, else zero-pad and report.
  Long first = _regionStart.load(std::memory_order_relaxed);
  Long last = _regionEnd.load(std::memory_order_relaxed);
  if (last < 0 || last > _length) last = _length;
  if (first > last) first = last;

  Int framesToRead = STREAM_BUFFERSIZE;
  Flt* ptr = dest;
  Bool wrapped = false;

  while (framesToRead > 0) {
    const Long room = last - (Long)_streamPos;
    const Long want = room < framesToRead ? room : framesToRead;
    sf_count_t read = want > 0 ? handle->readf(ptr, want) : 0;
    if (read < 0) read = 0;
    _streamPos += (Int)read;
    framesToRead -= (Int)read;
    ptr += (read * _channels);
    if (framesToRead == 0) break;
    if (read > 0) wrapped = false;
    // At the end of the region, or of the file before it. A wrap that reads
    // nothing at all would spin, so the second one in a row ends the fill.
    if (loop && last > first && !wrapped) {
      handle->seek(first, SEEK_SET);
      _streamPos = (Int)first;
      wrapped = true;
    } else {
      // non-loop end: zero-pad the remainder and report the real frame count
      UInt valid = STREAM_BUFFERSIZE - (UInt)framesToRead;
      Int zeros = framesToRead * _channels;
      while (zeros-- > 0)
        *ptr++ = 0.0f;
      return valid;
    }
  }
  return STREAM_BUFFERSIZE;
}

#endif // LIBSOUNDFILE_BACKEND
