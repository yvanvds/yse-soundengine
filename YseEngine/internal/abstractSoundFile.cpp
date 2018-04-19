/*
  ==============================================================================

    soundFile.cpp
    Created: 28 Jan 2014 11:49:20am
    Author:  yvan

  ==============================================================================
*/

#include "../internalHeaders.h"
//#include <string.h>

YSE::INTERNAL::abstractSoundFile::abstractSoundFile(bool interleaved)
  : useInterleavedBuffer(interleaved)
  , _iBuffer(nullptr)
  , _audioBuffer(nullptr)
  , _multiChannelBuffer(nullptr)
  , state(NEW)
  , _sampleRateAdjustment(1.f)
  , _channels(0)
  , idleTime(0)
{
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


Bool YSE::INTERNAL::abstractSoundFile::readNonInterleaved(abstractSoundFile * file, std::vector<DSP::buffer> & filebuffer, Flt& pos, UInt length, Flt speed, Bool loop, SOUND_STATUS & intent, Flt & volume) {
  /** Yes, this function uses goto...
      It is highly optimized for speed and this is the best way I could find
      to ensure good performance. Suggestions are welcome.
  */

  if (file->state != READY) return false;

  // adjust speed for sample rate
  speed *= file->_sampleRateAdjustment;

  // don't play streaming sounds backwards
  if (file->_streaming && speed < 0) speed = 0;

  Flt realPos = 0;
  if (file->_streaming) {
    realPos = pos;
    while (pos >= STREAM_BUFFERSIZE) {
      pos -= STREAM_BUFFERSIZE;
    }
    realPos -= pos;
  }
  
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
      if (file->_streaming) {
        while (pos >= STREAM_BUFFERSIZE) {
          if(file->fillStream(loop)) {
            pos -= STREAM_BUFFERSIZE;
            realPos += STREAM_BUFFERSIZE;
            if (realPos >= file->_length) {
              realPos -= file->_length;
            }
          }
          else {
            pos = 0;
            intent = SS_STOPPED;
            volume = 0.f;
            file->_streamPos = 0;
            continue;
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
    }

    // This label creates a safe point to start processing the next channel when the 
    // current one is done
    nextBuffer: ;
  }

  if (file->_streaming) pos += realPos;
  // make sure position is reset to zero if playing has stopped during this read
  if (intent == SS_STOPPED) {
    pos = 0;
    if (file->_streaming) {
      file->_streamPos = 0;
      if (file->fillStream(loop)) {
        pos -= STREAM_BUFFERSIZE;
        realPos += STREAM_BUFFERSIZE;
        if (realPos >= file->_length) {
          realPos -= file->_length;
        }
      }
      pos += realPos;
    }
  }
  return true;
}


//////////////////////////////////////////////////////////////////////////////////
// interleaved read function
//////////////////////////////////////////////////////////////////////////////////

Bool YSE::INTERNAL::abstractSoundFile::readInterleaved(abstractSoundFile * file, std::vector<DSP::buffer> & filebuffer, Flt& pos, UInt length, Flt speed, Bool loop, SOUND_STATUS & intent, Flt & volume) {
  /** Yes, this function uses goto...
  It is highly optimized for speed and this is the best way I could find
  to ensure good performance. Suggestions are welcome.
  */

  if (file->state != READY) return false;

  // adjust speed for sample rate
  speed *= file->_sampleRateAdjustment;

  // don't play streaming sounds backwards
  if (file->_streaming && speed < 0) speed = 0;

  Flt realPos = 0;
  if (file->_streaming) {
    realPos = pos;
    while (pos >= STREAM_BUFFERSIZE) {
      pos -= STREAM_BUFFERSIZE;
    }
    realPos -= pos;
  }

  // this is for a smooth fade-in to avoid glitches
  if (intent == SS_WANTSTOPLAY) {
    intent = SS_PLAYING;
    volume = 0.f;
  }

  // set cursor to the output buffer start
  FOREACH(filebuffer) filebuffer[i].cursor = filebuffer[i].getPtr();

  for (UInt x = 0; x < length; ) { // x is updated within loop, when increasing cursor

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

        if (pos < 0 || pos >= (file->_streaming ? STREAM_BUFFERSIZE : file->_length)) goto calibrate;
      }
    }

    // Sound plays, but not at full volume. So we're fading in
    else if (intent == SS_PLAYING) {
      while (x < length) {
        FOREACH(filebuffer) *filebuffer[i].cursor++ = (in[((UInt)pos) * file->_channels + i]) * volume;
        pos += speed;
        volume += 0.005f;
        x++;

        if (pos < 0 || pos >= (file->_streaming ? STREAM_BUFFERSIZE : file->_length)) goto calibrate;

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

        if (pos < 0 || pos >= (file->_streaming ? STREAM_BUFFERSIZE : file->_length)) goto calibrate;

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

        if (pos < 0 || pos >= (file->_streaming ? STREAM_BUFFERSIZE : file->_length)) goto calibrate;

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
      while (pos >= STREAM_BUFFERSIZE) {
        if (file->fillStream(loop)) {
          pos -= STREAM_BUFFERSIZE;
          realPos += STREAM_BUFFERSIZE;
          if (realPos >= file->_length) {
            realPos -= file->_length;
          }
        }
        else {
          pos = 0;
          intent = SS_STOPPED;
          volume = 0.f;
          file->_streamPos = 0;
          continue;
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
  

  if (file->_streaming) pos += realPos;
  // make sure position is reset to zero if playing has stopped during this read
  if (intent == SS_STOPPED) {
    pos = 0;
    if (file->_streaming) {
      file->_streamPos = 0;
      if (file->fillStream(loop)) {
        pos -= STREAM_BUFFERSIZE;
        realPos += STREAM_BUFFERSIZE;
        if (realPos >= file->_length) {
          realPos -= file->_length;
        }
      }
      pos += realPos;
    }
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
  _needsReset = true;
  return *this;
}

bool YSE::INTERNAL::abstractSoundFile::inUse() {
  if (clientList.empty()) {
    idleTime += Time().delta();
  }
  if (idleTime > 30) {
    return false;
  }
  return true;
}

void YSE::INTERNAL::abstractSoundFile::attach(YSE::SOUND::implementationObject * impl) {
  for (auto i = clientList.begin(); i != clientList.end(); ++i) {
    if (*i == impl) return;
  }
  clientList.emplace_front(impl);
}

void YSE::INTERNAL::abstractSoundFile::release(SOUND::implementationObject *impl) {
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
