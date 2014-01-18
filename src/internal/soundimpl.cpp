#include "stdafx.h"
#include <cmath>
#include <boost/ptr_container/ptr_map.hpp>
#include "internal/soundimpl.h"
#include "dsp/ramp.hpp"
#include "dsp/math.hpp"
#include "utils/misc.hpp"
#include "backend/ysetime.h"
#include "speakers.h"
#include "internal/settings.h"
#include "internal/internalObjects.h"
#include "internal/channelimpl.h"
#include "internal/listenerimpl.h"
#include "utils/error.hpp"
#include "backend/soundLoader.h"

YSE::soundimpl::soundimpl() {
	parent = NULL;
	looping = false;
  _fader.set(0.5f);
	_spread = 0;
	_loading = true;
	isVirtual = false;
	source_dsp = NULL;
	post_dsp = NULL;
  _setPostDSP = false;
  _postDspPtr = NULL;

  _release = false;
  _occlusion = 0.f;
  _occlusionActive = false;
  _streaming = false;
  _relative = false;
  _noDoppler = false;
  _pan2D = false;
  _fadeAndStop = false;
  link = NULL;
  Error.emit(E_SOUND_ADDED);
  _cCheck = 1.0;

  // set guard values
  _pos = Vec(0);
  _setVolume = false;
  _volumeValue = 0;
  _volumeTime = 0;
  _currentVolume = 0;
  _pitch = 1.0f;
  _size = 1.0f;
  _signalPlay = false;
  _signalPause = false;
  _signalStop = false;
  _signalToggle = false;
  newFilePos = 0;
  currentFilePos = 0;
  setFilePos = false;
  _length = 0;
  _setFadeAndStop = false;
  _fadeAndStopTime = 0;
  _latency = 0;
  bufferVolume = 0;
  bufferLatency = 0;
  startOffset = stopOffset = 0;
}

YSE::soundimpl::~soundimpl() {


  // only streams should delete their source. Other files are shared.
  if (_streaming && file != NULL) delete file;
  Error.emit(E_SOUND_DELETED);
  if (link) *link = NULL;
  if (post_dsp && post_dsp->calledfrom) post_dsp->calledfrom = NULL;
}

bool YSE::soundimpl::create(const std::string &fileName, Bool stream) {
	if (!stream) {
    // inserts file if it didn't exist yet, otherwise just return iterator to it
	  std::pair<boost::ptr_map<std::string, soundFile>::iterator, Bool> item = SoundFiles.insert(const_cast<std::string&>(fileName), new soundFile);
	  file = item.first->second;
	  _status = SS_STOPPED;

	  if (item.second == true) {
		  if (file->create(fileName)) {
			  filebuffer.resize(file->channels());
			  buffer = &filebuffer;
			  return true;
		  }
		  else {
			  SoundFiles.erase(item.first);
			  return false;
		  }
	  } else {
      filebuffer.resize(file->channels());
		  buffer = &filebuffer;
    }
	  return true;
  } else {
    // streams have their own soundfile
    _streaming = true;
    _status = SS_STOPPED;
    file = new soundFile;
    if (file->create(fileName, true)) {
      filebuffer.resize(file->channels());
      buffer = &filebuffer;
      _length = file->length();
      return true;
    } else {
      delete file;
      file = NULL;
      return false;
    }
  }
}

void YSE::soundimpl::initialize() {
	filePtr = 0;
	newPos.zero();
	lastPos.zero();
	vel.zero();
  velocity = 0.0f;
}


void YSE::AdjustLastGainBuffer() {
	Flt size = ChannelP->out.size();
	for (boost::ptr_list<soundimpl>::iterator i = Sounds().begin();  i != Sounds().end(); ++i) {

		// if a sound is still loading, it will be adjusted during initialize
		if (i->_loading) continue;

		Int j = i->lastGain.size(); // need to store previous size for deep resize
		i->lastGain.resize(ChannelP->out.size());
		for (; j < i->lastGain.size(); j++) {
			i->lastGain[j].resize(i->buffer->size(), 0.0f);
		}

	}
}

void YSE::soundimpl::update() {
	if (_loading) {
		if (file->state() == READY) {
			lastGain.resize(ChannelP->out.size());
	    for (Int i = 0; i < lastGain.size(); i++) {
		    lastGain[i].resize(buffer->size(), 0.0f);
	    }
			_loading = false;
		} else {
			return; // do not update as long as file is loading
		}
	}

	// position
	newPos = _pos * Settings.distanceFactor;

	// distance to listener
  if (_relative) {
    distance = 0;
  } else {
    distance = Dist(newPos, ListenerImpl._newPos);
  }
  virtualDist = (distance - _size) * (1 / (_fader.getValue() > 0.000001f ? _fader.getValue() : 0.000001f));
	if (parent->allowVirtual()) isVirtual = true;
	else isVirtual = false;

	// doppler effect
	if (_noDoppler) velocity = 0;
  else {
    vel = (newPos - lastPos) * (1 / Time.delta);
	  if (vel == Vec(0) && ListenerImpl._vel.load() == Vec(0)) velocity = 0;
	  else {
		  Vec dist = _relative ? newPos : newPos - ListenerImpl._newPos;
		  if (dist != Vec(0)) {
			  Flt rSound = Dot(vel, dist) / dist.length();
			  Flt rList = Dot(ListenerImpl._vel, dist) / dist.length();
			  velocity = 1 - (440 / (((344.0f + rList) / (344.0f + rSound)) * 440));
			  velocity *= Settings.dopplerScale;
		  }
	  }
  }
	lastPos = newPos;
  // disregard rounding errors
  if (abs(velocity) < 0.01f) velocity = 0.0f;

	// angle
	Vec dir = _relative ? newPos : newPos - ListenerImpl._newPos;
  if (_relative) angle = atan2(dir.z, dir.x);
  else angle = (atan2(dir.z, dir.x) - atan2(ListenerImpl._forward.load().z, ListenerImpl._forward.load().x));
	while (angle > Pi) angle -= Pi2;
	while (angle < -Pi) angle += Pi2;
  angle = -angle;

  // occlusion
  if (occlusionPtr != NULL && _occlusionActive) {
    _occlusion = occlusionPtr(newPos, ListenerImpl._newPos);
    Clamp(_occlusion, 0.f, 1.f);
  }

  // post dsp
  if (_setPostDSP) {
    addDSP(*_postDspPtr);
    _setPostDSP = false;
  }
}

Bool YSE::soundimpl::dsp() {
  if (_cCheck != 1.0) return false;
	if (_loading) return false;

  // this is probably not needed
  /*if (filebuffer[0].getLength() > BUFFERSIZE) {
    Error.emit(E_SOUND_WRONG);
    _status = SS_STOPPED;
    _release = true;
    return false;
  }*/

  // handle signals
  if (_signalRestart) {
    //filePtr = 0;
    //if (_streaming) file->needsReset = true;
    _status = SS_WANTSTORESTART;
    _signalRestart = false;
  }

  if (_signalPlay) {
    if (_status != SS_PLAYING) _status = SS_WANTSTOPLAY;
    _signalPlay = false;
  }

  if (_signalPause) {
    if (_status != SS_STOPPED && _status != SS_PAUSED) _status = SS_WANTSTOPAUSE;
    _signalPause = false;
  }

  if (_signalStop) {
	  if (_status != SS_STOPPED && _status != SS_PAUSED) {
      _status = SS_WANTSTOSTOP;
      //if (_streaming) file->needsReset = true;
    } else if (_status == SS_PAUSED) {
		  _status = SS_STOPPED;
		  filePtr = 0;
      if (_streaming) file->reset();
	  }
    _signalStop = false;
  }

  if (_signalToggle) {
    if (_status == SS_PLAYING || _status == SS_WANTSTOPLAY) _status = SS_WANTSTOPAUSE;
	  else _status = SS_WANTSTOPLAY;
    _signalToggle = false;
  }

	if (_status == SS_STOPPED || _status == SS_PAUSED) return false;
	if (isVirtual) return false;



  if (_latency > 0) {
    bufferLatency = _latency * (sampleRate / 1000.0f);
    _latency = 0;
  }

  // volume update
  if (_setVolume) {
    _fader.set(_volumeValue, _volumeTime);
    _setVolume = false;
  }
  if (_setFadeAndStop) {
    _fader.set(0, _fadeAndStopTime);
    _fadeAndStop = true;
    _setFadeAndStop = false;
  }

  _currentVolume = _fader.getValue();

  // change position if filePtrguard  is changed
  if (setFilePos) {
    Clamp(newFilePos, 0, file->length());
    filePtr = newFilePos;
    setFilePos = false;
  }

	if (source_dsp != NULL) {
    source_dsp->frequency(_pitch);
    source_dsp->process(_status, bufferLatency);

  } else if (file->read(filebuffer, filePtr, BUFFERSIZE, _pitch + velocity, looping, _status, bufferLatency, bufferVolume) == false) {
  	// non looping sound has reached end of file
    /*filePtr = 0;
		_status = SS_STOPPED;
    if (_streaming) file->needsReset = true;*/
	}

  // update file position for query by frontend
  currentFilePos = filePtr;

  // shorthand function to stop after fade out
  if (_fadeAndStop && (_fader.getValue() == 0)) {
    _signalStop = true;
    _fadeAndStop = false;
  }

  // update fader
  _fader.update();

	// apply dsp's if needed
	if (post_dsp != NULL) {
    DSP::dsp * ptr = post_dsp;
    while (ptr) {
      if (!ptr->bypass()) ptr->process(*buffer);
      ptr = ptr->link();
    }
  }
	return true;
}

void YSE::soundimpl::calculateGain(Int channel, Int source) {
  if (lastGain[channel][source] == parent->outConf[channel].finalGain) {
		channelBuffer *= (parent->outConf[channel].finalGain);
		return;
	}
	Flt length = 50;
  Clamp(length, 1, channelBuffer.getLength());
	Flt step = (parent->outConf[channel].finalGain - lastGain[channel][source]) / (Flt)length;
	Flt multiplier = lastGain[channel][source];
  Flt * ptr = channelBuffer.getBuffer();
	for (UInt i = 0; i < length; i++) {
		*ptr++ *= (multiplier);
		multiplier += step;
	}
  UInt leftOvers = channelBuffer.getLength() - length;
	for (; leftOvers > 7; leftOvers -= 8, ptr += 8) {
		ptr[0] *= (parent->outConf[channel].finalGain);
		ptr[1] *= (parent->outConf[channel].finalGain);
		ptr[2] *= (parent->outConf[channel].finalGain);
		ptr[3] *= (parent->outConf[channel].finalGain);
		ptr[4] *= (parent->outConf[channel].finalGain);
		ptr[5] *= (parent->outConf[channel].finalGain);
		ptr[6] *= (parent->outConf[channel].finalGain);
		ptr[7] *= (parent->outConf[channel].finalGain);
	}
	while (leftOvers--) *ptr++ *= (parent->outConf[channel].finalGain);
	lastGain[channel][source] = (parent->outConf[channel].finalGain);
}

void YSE::soundimpl::toChannels() {
#pragma warning ( disable : 4258 )
	for (Int i = 0; i < buffer->size(); i++) {
		// calculate spread value for multichannel sounds
		Flt spreadAdjust = 0;
    if (buffer->size() > 1) spreadAdjust = (((2*Pi / buffer->size()) * i) + (Pi/buffer->size()) - Pi) * _spread;

		// initial panning
		for (Int i = 0; i < parent->out.size(); i++) {
			parent->outConf[i].initPan = (1 + cos(parent->outConf[i].angle - (angle + spreadAdjust))) * 0.5;
			parent->outConf[i].effective = 0;
			// effective speakers
			for (Int j = 0; j < parent->out.size(); j++) {
				parent->outConf[i].effective += (1 + cos(parent->outConf[i].angle - parent->outConf[j].angle) * 0.5);
			}
			// initial gain
			parent->outConf[i].initGain = parent->outConf[i].initPan / parent->outConf[i].effective;
		}
		// emitted power
		Flt power = 0;
		for (Int i = 0; i < parent->out.size(); i++) {
			power += pow(parent->outConf[i].initGain, 2);
		}
		// calculated power
		Flt dist = distance - _size;
		if (dist < 0) dist = 0;
		Flt correctPower = 1 / pow(dist, (2 * Settings.rolloffScale));
		if (correctPower > 1) correctPower = 1;

		// final gain assignment
		for (Int j = 0; j < parent->out.size(); j++) {
			parent->outConf[j].ratio = pow(parent->outConf[j].initGain, 2) / power;
			channelBuffer = (*buffer)[i];
			parent->outConf[j].finalGain = sqrt(correctPower * parent->outConf[j].ratio);

			// add volume control now
			//parent->outConf[j].finalGain *= _volume;
      if (_occlusionActive) parent->outConf[j].finalGain *= 1 - _occlusion;
			calculateGain(j, i);
      channelBuffer *= _fader();
			parent->out[j] += channelBuffer;
		}
	}
}

void YSE::soundimpl::addDSP(DSP::dsp & ptr) {
  if (post_dsp) {
    if (post_dsp->calledfrom) {
      *(post_dsp->calledfrom) = NULL;
    }
  }
	post_dsp = &ptr;
  post_dsp->calledfrom = &post_dsp;
}

void YSE::soundimpl::addSourceDSP(DSP::dspSource &ptr) {
	source_dsp = &ptr;
	_status = SS_STOPPED;
	buffer = &source_dsp->buffer;
	_loading = false; // dsp source does not have to load
  lastGain.resize(ChannelP->out.size());
	for (Int i = 0; i < lastGain.size(); i++) {
	   lastGain[i].resize(buffer->size(), 0.0f);
	}
}





