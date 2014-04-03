/*
  ==============================================================================

    soundFile.cpp
    Created: 28 Jan 2014 11:49:20am
    Author:  yvan

  ==============================================================================
*/

#include "../internalHeaders.h"

Bool YSE::INTERNAL::soundFile::create(Bool stream) {
  _streaming = stream;
  _endReached = false;
  
  // load sound into memory
  _needsReset = false;

  if (!_streaming && state == NEW) {
    state = LOADING;
    Global().addSlowJob(this);
  } 
  return true;
}

ThreadPoolJob::JobStatus YSE::INTERNAL::soundFile::runJob() {
  ScopedPointer<AudioFormatReader> reader;
  if (source == nullptr) {
    reader = SOUND::Manager().getReader(file);
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
    return jobHasFinished;
  }
  else {
    Global().getLog().emit(E_FILEREADER, "Unable to read " + file.getFullPathName().toStdString());
    state = INVALID;
    return jobHasFinished;
  }
}

Bool YSE::INTERNAL::soundFile::read(std::vector<DSP::sample> & filebuffer, Flt& pos, UInt length, Flt speed, Bool loop, SOUND_STATUS & intent, Flt & volume) {
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
    if (_needsReset) resetStream();
  }

  Flt ** ptr2 = _buffer.getArrayOfChannels();
  
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
    Flt * out = filebuffer[i].getBuffer();
    Flt * in = ptr2[i];
    
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
        else if ((speed > 0) && ((pos + speed * 16) >= _length)) {
          while (l--) {
            *out++ = in[(UInt)pos];
            pos += speed;
            // check if we're past the end and readjust position if so
            if (pos >= _length) goto calibrate;
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
            if (pos < 0 || pos >= _length) goto calibrate;
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
              if (pos < 0 || pos >= _length) goto calibrate;
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
          if (pos < 0 || pos >= _length) goto calibrate;
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
          if (pos < 0 || pos >= _length) goto calibrate;
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
          if (pos < 0 || pos >= _length) goto calibrate;
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
      /*if (_streaming) {
        while (pos >= STREAM_BUFFERSIZE) {
          if (!_endReached) {
            _endReached = fillStream(loop);
            pos -= STREAM_BUFFERSIZE;
            realPos += STREAM_BUFFERSIZE;
          }
          else {
            pos = 0;
            intent = SS_STOPPED;
            volume = 0.f;
            resetStream();
            continue;
          }
        }
      }
      else */
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
  return true;
}

Bool YSE::INTERNAL::soundFile::contains(const File & file) {
  return this->file == file;
}

Bool YSE::INTERNAL::soundFile::contains(juce::InputStream * source) {
  return this->source == source;
}

void YSE::INTERNAL::soundFile::resetStream() {

}

Bool YSE::INTERNAL::soundFile::fillStream(Bool loop) {
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
YSE::INTERNAL::soundFile::soundFile(const File & file) : ThreadPoolJob(file.getFullPathName())
  , _buffer(1, 1)
  , clients(0)
  , idleTime(0)
  , state(NEW)
  , file(file)
  , source(nullptr) 
{
  _sampleRateAdjustment = 1.0f;
}


YSE::INTERNAL::soundFile::soundFile(juce::InputStream * source) : ThreadPoolJob("BinaryDataReader")
, _buffer(1, 1)
, clients(0)
, idleTime(0)
, state(NEW)
, file()
, source(source) {
  _sampleRateAdjustment = 1.0f;
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
  if (clients < 1) {
    idleTime += Time().delta();
  }
  if (idleTime > 30) {
    return false;
  }
  return true;
}

void YSE::INTERNAL::soundFile::attach(YSE::SOUND::implementationObject * impl) {
  clientList.unique()
}