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
	Log().sendMessage("sound added");
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
      _length = (Int)handle->frames();
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
  void * ptr = nullptr;
  
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
  }
  else {
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
    message << fileName << " with size " << size << " (length " << _length
      << " * channels " << _channels << " for " << read << " bytes.";
    YSE::Log().sendMessage(message.str().c_str());

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


  if (ptr != nullptr) INTERNAL::customFileReader::Close(ptr);
 
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