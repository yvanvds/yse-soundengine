#include "headers/types.hpp"
#ifdef WINDOWS
  #include <Windows.h>
#endif
#include <vector>
#include "stdafx.h"
#include "soundfile.h"
#include "utils/error.hpp"
#include "ysetime.h"
#include "soundLoader.h"
#include "internal/internalObjects.h"

const Int StreamBufferSize = 88200;

Bool YSE::soundFile::create(const std::string &fileName, Bool stream) {
#if defined(USE_PORTAUDIO)
	_info.format = 0;
	memset(&_info, 0, sizeof(_info));
  _streaming = stream;
  _endReached = false;

  if (IOImpl._active) {
    _customFileData.filename = fileName;
    if (IOImpl.open(fileName.c_str(), &_customFileData.filesize, &_customFileData.handle, &_customFileData.userdata)) {
      _file = sf_open_virtual(&IOImpl.vio, SFM_READ, &_info, &_customFileData);
    } else {
      return false;
    }
  } else {
	  _file = sf_open(fileName.c_str(), SFM_READ, &_info);
  }

	if (sf_error(_file) != SF_ERR_NO_ERROR) {
    Error.emit(E_FILE_ERROR, sf_strerror(_file));
		sf_close(_file);
    _file = NULL;
		return false;
	}

	// load sound into memory
	_fileName = fileName;
  _usage = 0;
  _idleTime = 0.0f;
  _needsReset = false;

  // sample rate adjustment
  _sr_adjust = _info.samplerate / (Flt)sampleRate;


  if (!_streaming) {
    _state = LOADING;
    YSE::lock l(SFMTX);
    LoadList().push_back(this);
  } else {
    _streamSize = StreamBufferSize * _info.channels;
    _buffer.resize(_streamSize);
    _streamPos = 0;
    fillStream(false);
    _state = READY;
  }
#endif
  return true;
}

Bool YSE::soundFile::fillStream(Bool loop) {
#if defined(USE_PORTAUDIO)
  Int framesToRead = StreamBufferSize;
  Flt * ptr = _buffer.getBuffer();

  while (framesToRead > 0) {
    U64 read = sf_readf_float(_file, ptr, framesToRead);
    _streamPos += (UInt)read;
    framesToRead -= (UInt)read;
    if (framesToRead > 0) { // end of file reached
      ptr += (read * _info.channels);
      if (loop) {
        sf_seek(_file, 0, SEEK_SET); // rewind file
        _streamPos = 0;
      } else {
        // fill with zero's
        framesToRead *= _info.channels;
        while (framesToRead--) *ptr++ = 0.0f;
        _streamPos = 0; // reset
        return true; // return true because end is reached and we're not looping
      }
    }
  }
#endif
  return false;
}

void YSE::soundFile::resetStream() {
#if defined(USE_PORTAUDIO)
  sf_seek(_file, 0, SEEK_SET);
  _streamPos = 0;
  fillStream(true);
  _needsReset = false;
  _endReached = false;
#endif
}

Bool YSE::soundFile::read(std::vector<DSP::sample> & filebuffer, Flt& pos, UInt length, Flt speed, Bool loop, SOUND_STATUS &intent, Int & latency, Flt & volume) {
#if defined(USE_PORTAUDIO)
  // adjust speed for sample rate
  speed *= _sr_adjust;


  // don't play streaming sounds backwards
  if (_streaming && speed < 0) speed = 0;

  Flt realPos;
  if (_streaming) {
    realPos  = pos;
    while (pos >= StreamBufferSize) pos -= StreamBufferSize;
    realPos -= pos;
    if (_needsReset) resetStream();
  }


  Flt * ptr2 = _buffer.getBuffer();
  //UInt bLength = buffer.getLength();

  // set cursor to the output buffer start
  FOR(filebuffer) filebuffer[i].cursor = filebuffer[i].getBuffer();

  for (UInt x = 0; x < length; ) { // x is updated within loop, when increasing cursor

    // set position in filebuffer, according to nr of channels
    UInt channelPos = ((UInt)pos) * _info.channels;

    // check intents

    //--------------------------- WANTSTOPLAY ---------------------------------//
    // fill with zero's if wants to play
    if (intent == SS_WANTSTOPLAY) {
      while (latency > 0 && x < length) {
        FOR (filebuffer) *filebuffer[i].cursor++ = 0;
        latency--;
        x++;

      }

      // if x == length, latency counter is still not zero
      // if not, start playing
      if (latency <= 0) {
        intent = SS_PLAYING;
        volume = 0.0f;
      }

      continue;
    }

    //--------------------------- STOPPED || PAUSED ---------------------------------//
    if (intent == SS_STOPPED || intent == SS_PAUSED) {
      // fill the rest of the buffer with zeros
      while (x < length) {
        FOR (filebuffer) *filebuffer[i].cursor++ = 0;
        x++;
      }

      continue;
    }

    //--------------------------- PLAYING ---------------------------------//
    // normal fill while playing
    if (intent == SS_PLAYING || intent == SS_WANTSTOPAUSE || intent == SS_WANTSTOSTOP || intent == SS_WANTSTORESTART) {
      FOR(filebuffer) *filebuffer[i].cursor++ = (ptr2[channelPos + i] * volume);
      x++;

      pos += speed;

      if (intent == SS_PLAYING)  { if (volume < 1.0f) volume += 0.005f; }

      // we are still playing during these intents
      //--------------------------- WANTSTOPAUSE ---------------------------------//
      else if (intent == SS_WANTSTOPAUSE) {
        if (latency) latency--; // countdown latency
        if (latency < 200 && volume > 0.0f) volume -= 0.005f; // fade out
        if (latency <= 0 && volume <= 0.0f) {
          intent = SS_PAUSED;
          volume = 0.0f;
          latency = 0;
        }
      }
      //--------------------------- WANTSTOSTOP ---------------------------------//
      else if (intent == SS_WANTSTOSTOP) {
        if (latency) latency--; // countdown latency
        if (latency < 200 && volume > 0.0f) volume -= 0.005f; // fade out
        if (latency <= 0 && volume <= 0.0f) {
          if (_streaming) resetStream();
          intent = SS_STOPPED;
          pos = 0;
          volume = 0.0f;
          latency = 0;
        }
      }
      //--------------------------- WANTSTORESTART ---------------------------------//
      else if (intent == SS_WANTSTORESTART) {
        latency--;
        if (latency < 200 && volume > 0.0f) volume -= 0.005f; // fade out
        if (latency <= 0 && volume <= 0.0f) {
          volume = 0.0f;
          latency = 0;
          pos = 0;
          if (_streaming) resetStream();
          intent = SS_PLAYING;
        }
      }



      // reposition if needed

      if (_streaming) {
        while (pos >= StreamBufferSize) {
          if (!_endReached) {
            _endReached = fillStream(loop);
            pos -= StreamBufferSize;
            realPos += StreamBufferSize;
          } else {
            pos = 0;
            if (_streaming) resetStream();
            intent = SS_STOPPED;
            continue;
          }
        }
      } else {
        while (pos < 0) pos += (_info.frames); // looping backwards
		    if (pos >= _info.frames) {
		      if (loop) while (pos >= _info.frames) 	pos -= _info.frames;
		      else {
            pos = 0; // reset dataptr, just in case
            intent = SS_STOPPED;
			      continue; // not looping and end is reached
		      }
	      }
      }
    } // end intent == SS_PLAYING


  }
  if (_streaming) pos += realPos;
#endif
  return true;
}


Bool YSE::soundFile::read(std::vector<DSP::sample> & filebuffer, Flt& pos, UInt length, Flt speed, Bool loop) {
#if defined(USE_PORTAUDIO)
	if (_state != READY) return false;

  // adjust speed for sample rate
  speed *= _sr_adjust;

  // don't play streaming sounds backwards
  if (_streaming && speed < 0) speed = 0;

  Flt realPos;
  if (_streaming) {
    realPos  = pos;
    while (pos >= StreamBufferSize) {
      pos -= StreamBufferSize;
    }
    realPos -= pos;
  }

  if (_needsReset) resetStream();

  Flt * ptr2 = _buffer.getBuffer();
  UInt bLength = _buffer.getLength();

  if (filebuffer.size() == 1) {
    // short, faster version because many buffers are probably mono
    filebuffer[0].cursor = filebuffer[0].getBuffer();

    for (UInt i = 0; i < length; i++) {
      *filebuffer[0].cursor++ = ptr2[(UInt)pos];
      pos += speed;

      if (_streaming) {
        while (pos >= StreamBufferSize) {
          if (!_endReached) {
            _endReached = fillStream(loop);
            pos -= StreamBufferSize;
            realPos += StreamBufferSize;
          } else {
            i++;
            for (;i < length; i++) {
              *filebuffer[0].cursor++ = 0;
            }
            pos = 0;
            return false;
          }
        }
      } else {
        while (pos < 0) pos += bLength;
        if (pos >= bLength) {
          if (loop) while (pos >= bLength) pos -= bLength;
          else {
            i++;
            for (;i < length;i++) {
              *filebuffer[0].cursor++ = 0;
            }
            pos = 0;
            return false;
          }
        }
      }
    }

  } else {
    for (UInt i = 0; i < filebuffer.size(); i++) {
      filebuffer[i].cursor = filebuffer[i].getBuffer();
    }

    for (UInt i = 0; i < length; i++) {
      UInt channelPos = ((UInt)pos) * _info.channels;

      for (UInt ch = 0; ch < filebuffer.size(); ch++) {
        *filebuffer[ch].cursor++ = ptr2[channelPos + ch];
      }
		  pos += speed;

      if (_streaming) {
        while (pos >= StreamBufferSize) {
          if (!_endReached) {
            _endReached = fillStream(loop);
            pos -= StreamBufferSize;
            realPos += StreamBufferSize;
          } else {
            i++;
            for (;i < length; i++) {
              for (UInt ch = 0; ch < filebuffer.size(); ch++) {
                *filebuffer[ch].cursor++ = 0;
              }
            }
            pos = 0;
            return false;
          }
        }
      } else {

        while (pos < 0) pos += (_info.frames); // looping backwards
		    if (pos >= _info.frames) {
		      if (loop) while (pos >= _info.frames) 	pos -= _info.frames;
		      else {
			      // if no loop, fill the rest of the buffer with zero's
            i++;
            for (;i < length; i++) {
              for (UInt ch = 0; ch < filebuffer.size(); ch++) {
                *filebuffer[ch].cursor++ = 0;
              }
            }
            pos = 0; // reset dataptr, just in case
			      return false; // not looping and end is reached
		      }
	      }
      }
    }
  }
  if (_streaming) pos += realPos;
#endif
	return true;
}

YSE::soundFile::~soundFile() {
#if defined(USE_PORTAUDIO)
  // if not streaming, file should be closed already
  if (_streaming) {
    if (_file) {
      sf_close(_file);
    }
  }
  if (IOImpl._active) IOImpl.close(&_customFileData.handle, &_customFileData.userdata);
#endif
}

YSE::soundFile::soundFile() {
#if defined(USE_PORTAUDIO)
  _file = NULL;
  _sr_adjust = 1.0f;
#endif
}

Int YSE::soundFile::channels() {
#if defined(USE_PORTAUDIO)
  return _info.channels;
#endif
}

UInt YSE::soundFile::length() {
#if defined(USE_PORTAUDIO)
  return _info.frames;
#endif
}

YSE::FILESTATE YSE::soundFile::state() {
#if defined(USE_PORTAUDIO)
  return _state;
#endif
}

YSE::soundFile & YSE::soundFile::reset() {
  _needsReset = true;
  return *this;
}

YSE::soundFile & YSE::soundFile::claim() {
  _usage++;
  return *this;
}

YSE::soundFile & YSE::soundFile::release() {
  _usage--;
  return *this;
}

Bool YSE::soundFile::active() {
  if (_usage > 0) _idleTime = 0;
  else _idleTime += Time.delta;
  // unload files after 8.33 minutes of inactivity
  return (_idleTime > 500 ? false : true);
}
