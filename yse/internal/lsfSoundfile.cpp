/*
  ==============================================================================

    lsfSoundfile.cpp
    Created: 28 Jul 2016 9:30:04pm
    Author:  yvan

  ==============================================================================
*/

#if LIBSOUNDFILE_BACKEND

#include "../internalHeaders.h"


YSE::INTERNAL::soundFile::soundFile(const std::string & fileName)
  : abstractSoundFile(fileName, true)
  , handle(nullptr)
{
}

YSE::INTERNAL::soundFile::soundFile(YSE::DSP::buffer * buffer)
  : abstractSoundFile(buffer)
  , handle(nullptr)
{
}

YSE::INTERNAL::soundFile::soundFile(MULTICHANNELBUFFER * buffer)
  : abstractSoundFile(buffer)
  , handle(nullptr)
{
}

YSE::INTERNAL::soundFile::~soundFile() {
  if (_iBuffer != nullptr) delete[] _iBuffer;
  if (handle != nullptr) delete handle;
}

void YSE::INTERNAL::soundFile::loadStreaming() {
  assert(handle == nullptr);
  if (IO().getActive()) {

  }
  else {
    handle = new SndfileHandle(fileName);
    if (*handle) {
      _sampleRateAdjustment = static_cast<Flt>(handle->samplerate()) / static_cast<Flt>(SAMPLERATE);
      _length = handle->frames();
      _channels = handle->channels();
      
      Int size = STREAM_BUFFERSIZE * _channels;
      _iBuffer = new Flt[size];
      _streamPos = 0;
      fillStream(false);
      state = READY;
    }
    else {
      LogImpl().emit(E_FILEREADER, "Unable to read " +fileName);
      state = INVALID;
    }
  }
}

void YSE::INTERNAL::soundFile::loadNonStreaming() {
  assert(handle == nullptr);
  if (IO().getActive()) {
    
  }
  else {
    handle = new SndfileHandle(fileName);
    if (*handle) {
      _sampleRateAdjustment = static_cast<Flt>(handle->samplerate()) / static_cast<Flt>(SAMPLERATE);
      _length = handle->frames();
      _channels = handle->channels();
      
      Int size = _length * _channels;
      _iBuffer = new Flt[size];
      Long read = handle->readf(_iBuffer, _length);
      if (read != _length) {
        LogImpl().emit(E_FILEREADER, handle->strError());
      }
      else {
        // file is read, but must be converted to non interleaved model
        state = READY;
      }
    }
    else {
      LogImpl().emit(E_FILEREADER, "Unable to read " + fileName);
      state = INVALID;
    }
  }
}

Bool YSE::INTERNAL::soundFile::fillStream(Bool loop) {
  Int framesToRead = STREAM_BUFFERSIZE;
  Flt * ptr = _iBuffer;

  while (framesToRead > 0) {
    U64 read = handle->readf(_iBuffer, framesToRead);
    _streamPos += (UInt)read;
    framesToRead -= (UInt)read;
    if (framesToRead > 0) {
      ptr += (read * _channels);
      if (loop) {
        handle->seek(0, SEEK_SET);
        _streamPos = 0;
      }
      else {
        framesToRead *= _channels;
        while (framesToRead--) *ptr++ = 0.0f;
        _streamPos = 0;
        return true;
      }
    }
    else {
      return true;
    }
    
  }
  return false;
}


#endif // LIBSOUNDFILE_BACKEND