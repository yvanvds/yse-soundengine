/*
  ==============================================================================

    soundFile.cpp
    Created: 28 Jan 2014 11:49:20am
    Author:  yvan

  ==============================================================================
*/

#include "../internalHeaders.h"

Bool YSE::INTERNAL::soundFile::create(Bool stream) {
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

void YSE::INTERNAL::soundFile::run() {
  if (_streaming) {
    if (source == nullptr) {
      if (IO().getActive()) {
        // will be deleted by AudioFormatReader
        customFileReader * cfr = new customFileReader;
        cfr->create(fileName.c_str());
        streamReader = SOUND::Manager().getReader(cfr);
      }
      else {
        streamReader = SOUND::Manager().getReader(file);
      }
    }
    else {
      streamReader = SOUND::Manager().getReader(source);
    }
    if (streamReader != nullptr) {
      _buffer.setSize(streamReader->numChannels, STREAM_BUFFERSIZE);
      // sample rate adjustment
      _sampleRateAdjustment = static_cast<Flt>(streamReader->sampleRate) / static_cast<Flt>(SAMPLERATE);
      _length = (Int)streamReader->lengthInSamples;
      _streamPos = 0;
      fillStream(false);
      // file is ready for use now
      state = READY;
      return;
    }
    else {
      LogImpl().emit(E_FILEREADER, "Unable to read " + file.getFullPathName().toStdString());
      state = INVALID;
      return;
    }
  }

  // load non streaming sounds in one go
  ScopedPointer<AudioFormatReader> reader;
  if (source == nullptr) {
    if (IO().getActive()) {
      // will be deleted by AudioFormatReader
      customFileReader * cfr = new customFileReader;
      cfr->create(fileName.c_str());
      reader = SOUND::Manager().getReader(cfr);
    }
    else {
      reader = SOUND::Manager().getReader(file);
    }
    
  }
  else {
    reader = SOUND::Manager().getReader(source);
  }
  
  if (reader != nullptr) {
    _buffer.setSize(reader->numChannels, (Int)reader->lengthInSamples);
    reader->read(&_buffer, 0, (Int)reader->lengthInSamples, 0, true, true);
    // sample rate adjustment
    _sampleRateAdjustment = static_cast<Flt>(reader->sampleRate) / static_cast<Flt>(SAMPLERATE);
    _length = _buffer.getNumSamples();
    // file is ready for use now
    state = READY;
    return;
  }
  else {
    LogImpl().emit(E_FILEREADER, "Unable to read " + file.getFullPathName().toStdString());
    state = INVALID;
    return;
  }
}

Bool YSE::INTERNAL::soundFile::read(std::vector<DSP::buffer> & filebuffer, Flt& pos, UInt length, Flt speed, Bool loop, SOUND_STATUS & intent, Flt & volume) {
  /** Yes, this function uses goto...
      It is highly optimized for speed and this is the best way I could find
      to ensure good performance. Suggestions are welcome.
  */

  if (state != READY) return false;

  // adjust speed for sample rate
  speed *= _sampleRateAdjustment;

  // don't play streaming sounds backwards
  if (_streaming && speed < 0) speed = 0;

  Flt realPos = 0;
  if (_streaming) {
    realPos = pos;
    while (pos >= STREAM_BUFFERSIZE) {
      pos -= STREAM_BUFFERSIZE;
    }
    realPos -= pos;
  }

  const Flt ** ptr2;
  if (!_audioBuffer) ptr2 = _buffer.getArrayOfReadPointers();
  
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
    
    if (_audioBuffer) {
      in = _audioBuffer->getPtr();
      _length = _audioBuffer->getLength();
    }
    else if (_multiChannelBuffer) {
      in = _multiChannelBuffer->at(i).getPtr();
      _length = _multiChannelBuffer->at(i).getLength();
    }
    else  {
      in = ptr2[i];
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
        else if ((speed > 0) && ((pos + speed * 16) >= (_streaming ? STREAM_BUFFERSIZE : _length))) {
          while (l--) {
            *out++ = in[(UInt)pos];
            pos += speed;
            // check if we're past the end and readjust position if so
            if (pos >= (_streaming ? STREAM_BUFFERSIZE : _length)) goto calibrate;
          }
          goto nextBuffer; // this output channel buffer is full if we get here
        }

        // at this point, we can be sure our current position allows for 
        // 16 more frames without recalibration
        else {
          // if the output still needs more than 16 frames, add 16 at once
          if (l > 15) {
            out[0] = in[(UInt)pos]; pos += speed;
            out[1] = in[(UInt)pos]; pos += speed;
            out[2] = in[(UInt)pos]; pos += speed;
            out[3] = in[(UInt)pos]; pos += speed;
            out[4] = in[(UInt)pos]; pos += speed;
            out[5] = in[(UInt)pos]; pos += speed;
            out[6] = in[(UInt)pos]; pos += speed;
            out[7] = in[(UInt)pos]; pos += speed;
            out[8] = in[(UInt)pos]; pos += speed;
            out[9] = in[(UInt)pos]; pos += speed;
            out[10] = in[(UInt)pos]; pos += speed;
            out[11] = in[(UInt)pos]; pos += speed;
            out[12] = in[(UInt)pos]; pos += speed;
            out[13] = in[(UInt)pos]; pos += speed;
            out[14] = in[(UInt)pos]; pos += speed;
            out[15] = in[(UInt)pos]; pos += speed;
            l -= 16;
            out += 16;
            // check if we're past the end and readjust position if so
            if (pos < 0 || pos >= (_streaming ? STREAM_BUFFERSIZE : _length)) goto calibrate;
            // since l > 15, the output buffer is not full yet
            goto mainLoop;
          }
          // almost at the end of the output buffer, handle one
          // frame at a time now
          else {
            while (l-- > 0) {
              *out++ = in[(UInt)pos];
              pos += speed;
              // check if we're past the end and readjust position if so
              if (pos < 0 || pos >= (_streaming ? STREAM_BUFFERSIZE : _length)) goto calibrate;
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
          if (pos < 0 || pos >= (_streaming ? STREAM_BUFFERSIZE : _length)) goto calibrate;
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
          if (pos < 0 || pos >= (_streaming ? STREAM_BUFFERSIZE : _length)) goto calibrate;
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
          if (pos < 0 || pos >= (_streaming ? STREAM_BUFFERSIZE : _length)) goto calibrate;
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
      if (_streaming) {
        while (pos >= STREAM_BUFFERSIZE) {
          if(fillStream(loop)) {
            pos -= STREAM_BUFFERSIZE;
            realPos += STREAM_BUFFERSIZE;
            if (realPos >= streamReader->lengthInSamples) {
              realPos -= streamReader->lengthInSamples;
            }
          }
          else {
            pos = 0;
            intent = SS_STOPPED;
            volume = 0.f;
            _streamPos = 0;
            continue;
          }
        }
      }
      else 
      {
        // if we get here, pos is past the end or before the beginning
        // recalibrate position now. We can't simply set it to the end
        // or the beginning because this won't work with speed <> 1
        while (pos < 0) pos += _length; // looping backwards
        if (pos >= _length) {
          if (loop) while (pos >= _length) pos -= _length;
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

  if (_streaming) pos += realPos;
  // make sure position is reset to zero if playing has stopped during this read
  if (intent == SS_STOPPED) {
    pos = 0;
    if (_streaming) {
      _streamPos = 0;
      if (fillStream(loop)) {
        pos -= STREAM_BUFFERSIZE;
        realPos += STREAM_BUFFERSIZE;
        if (realPos >= streamReader->lengthInSamples) {
          realPos -= streamReader->lengthInSamples;
        }
      }
      pos += realPos;
    }
  }
  return true;
}

Bool YSE::INTERNAL::soundFile::contains(const File & file) {
  return this->file == file;
}

Bool YSE::INTERNAL::soundFile::contains(juce::InputStream * source) {
  return this->source == source;
}

Bool YSE::INTERNAL::soundFile::contains(const char * fileName) {
  return strcmp(this->fileName.c_str(), fileName) == 0;
}

Bool YSE::INTERNAL::soundFile::contains(YSE::DSP::buffer * buffer) {
  return this->_audioBuffer == buffer;
}

Bool YSE::INTERNAL::soundFile::contains(MULTICHANNELBUFFER * buffer) {
  return this->_multiChannelBuffer == buffer;
}

void YSE::INTERNAL::soundFile::resetStream() {

}

Bool YSE::INTERNAL::soundFile::fillStream(Bool loop) {
  if (!loop) {
    streamReader->read(&_buffer, 0, (Int)_buffer.getNumSamples(), _streamPos, true, true);
    _streamPos += (Int)_buffer.getNumSamples();
    if (_streamPos >= (Int)streamReader->lengthInSamples) {
      // end of file reached
      return false;
    }
    else {
      // file is not done yet
      return true;
    }
  }
  else {
    // looping sound, so we have to keep refilling the buffer
    Int bufferPos = 0;
    while (bufferPos < STREAM_BUFFERSIZE) {
      // determine how many samples we can get before _streamPos needs to reset
      Int samplesToGet;
      if (_streamPos + (STREAM_BUFFERSIZE - bufferPos) > (Int)streamReader->lengthInSamples) {
        samplesToGet = (Int)streamReader->lengthInSamples - _streamPos;
      }
      else {
        samplesToGet = STREAM_BUFFERSIZE - bufferPos;
      }

      streamReader->read(&_buffer, bufferPos, samplesToGet, _streamPos, true, true);
      bufferPos += samplesToGet;
      _streamPos += samplesToGet;
      if (_streamPos >= (Int)streamReader->lengthInSamples) {
          // restart file
        _streamPos -= (Int)streamReader->lengthInSamples;
      }
    }
  }
  
  return true;
}

YSE::INTERNAL::soundFile::~soundFile() {
  // if not streaming, file should be closed already (after loading it)
  if (_streaming) {
    //if (_file) {
    //  sf_close(_file);
    //}
  }
  //if (IOImpl._active) IOImpl.close(&_customFileData.handle, &_customFileData.userdata);
}


// the real _buffer size is set while loading the sound, but JUCE does not allow for
// audio bufers of zero length. This is why it is set to one here.
YSE::INTERNAL::soundFile::soundFile(const File & file) :  _buffer(1, 1)
  , idleTime(0)
  , state(NEW)
  , file(file)
  , source(nullptr)
  , _audioBuffer(nullptr)
  , _multiChannelBuffer(nullptr)
  , _sampleRateAdjustment(1.f)
{
}

YSE::INTERNAL::soundFile::soundFile(const char * fileName) :  _buffer(1, 1)
  , idleTime(0)
  , state(NEW)
  , fileName(fileName)
  , source(nullptr)
  , _audioBuffer(nullptr)
  , _multiChannelBuffer(nullptr)
  , _sampleRateAdjustment(1.f)
{
}

YSE::INTERNAL::soundFile::soundFile(juce::InputStream * source) :  _buffer(1, 1)
  , idleTime(0)
  , state(NEW)
  , file()
  , source(source)
  , _audioBuffer(nullptr)
  , _multiChannelBuffer(nullptr)
  , _sampleRateAdjustment(1.f)
{
}

YSE::INTERNAL::soundFile::soundFile(YSE::DSP::buffer * buffer) : _buffer(1, 1)
  , idleTime(0)
  , state(NEW)
  , file()
  , source(nullptr)
  , _audioBuffer(buffer)
  , _multiChannelBuffer(nullptr)
  , _sampleRateAdjustment(1.f)
{
}

YSE::INTERNAL::soundFile::soundFile(MULTICHANNELBUFFER * buffer) : _buffer(1, 1)
, idleTime(0)
, state(NEW)
, file()
, source(nullptr)
, _audioBuffer(nullptr)
, _multiChannelBuffer(buffer)
, _sampleRateAdjustment(1.f)
{
}

Int YSE::INTERNAL::soundFile::channels() {
  return _buffer.getNumChannels();
}

UInt YSE::INTERNAL::soundFile::length() {
  return _length;
}

YSE::INTERNAL::FILESTATE YSE::INTERNAL::soundFile::getState() {
  return state;
}

YSE::INTERNAL::soundFile & YSE::INTERNAL::soundFile::reset() {
  _needsReset = true;
  return *this;
}

bool YSE::INTERNAL::soundFile::inUse() {
  if (clientList.empty()) {
    idleTime += Time().delta();
  }
  if (idleTime > 30) {
    return false;
  }
  return true;
}

void YSE::INTERNAL::soundFile::attach(YSE::SOUND::implementationObject * impl) {
    for (auto i = clientList.begin(); i != clientList.end(); ++i) {
        if (*i == impl) return;
    }
    clientList.emplace_front(impl);
}

void YSE::INTERNAL::soundFile::release(SOUND::implementationObject *impl) {
    auto previous = clientList.before_begin();
    for (auto i = clientList.begin(); i != clientList.end(); ++i) {
        if(*i == impl) {
            clientList.erase_after(previous);
            return;
        }
        previous++;
    }
    // this point should not be reached
    jassertfalse;
}
