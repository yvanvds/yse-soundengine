#include "stdafx.h"
#if defined(USE_PORTAUDIO)
#include "pa.h"
#ifdef WINDOWS
  #include "pa_asio.h"
#endif
#include "internal/channelimpl.h"
#include "system.hpp"
#include "internal/internalObjects.h"
#include "utils/error.hpp"
#include "device.hpp"

namespace YSE {
	UInt sampleRate = 44100;
};

Bool YSE::systemDevice::initialize() {
	init = open = started = false;
	_lastError = Pa_Initialize();
	if (_lastError != paNoError) {
    Error.emit(E_PORTAUDIO, getError());
		return false;
	}
	init = true;

	updateDevices();
	activeDevice = 0;
	for (Int i = 0; i < devices.size(); i++) {
		if (devices[i].defaultOut) {
			activeDevice = i;
			break;
		}
	}
	return true;
}

Bool YSE::systemDevice::openDevice(UInt ID, Int outChannels) {
	if (!init) return false;

	if (started) {
		_lastError = Pa_StopStream(_stream);
    if (_lastError != paNoError) Error.emit(E_PORTAUDIO, getError());
		started = false;
	}

	if (open) {
		_lastError = Pa_CloseStream(_stream);
    if (_lastError != paNoError) Error.emit(E_PORTAUDIO, getError());
		open = false;
	}

	if (ID >= devices.size()) return false;

	activeDevice = ID;

	PaStreamParameters params;
	params.device = activeDevice;
	params.channelCount = outChannels;
	params.sampleFormat = paFloat32 | paNonInterleaved;

	if (devices[activeDevice].type == paASIO) {
		params.suggestedLatency = devices[activeDevice].preferredLatency;
	} else {
		params.suggestedLatency = devices[activeDevice].highOutputLatency;
	}
	params.hostApiSpecificStreamInfo = NULL;

	sampleRate = devices[activeDevice].sampleRate;
  _lastError = Pa_OpenStream(&_stream, NULL, &params, sampleRate, paFramesPerBufferUnspecified, paNoFlag, callback, NULL);
	if (_lastError != paNoError) {
    Error.emit(E_PORTAUDIO, getError());
    return false;
  }
	open = true;

	_lastError = Pa_StartStream(_stream);
	if (_lastError != paNoError) {
    Error.emit(E_PORTAUDIO, getError());
    return false;
  }
  started = true;

	return true;
}


YSE::systemDevice::~systemDevice() {
	close();
}

void YSE::systemDevice::close() {
  lock Lock(MTX);

	if (started) {
		_lastError = Pa_StopStream(_stream);
    if (_lastError != paNoError) Error.emit(E_PORTAUDIO, getError());
		started = false;
	}
	if (open) {
		_lastError = Pa_CloseStream(_stream);
    if (_lastError != paNoError) Error.emit(E_PORTAUDIO, getError());
		open = false;
	}
	if (init) {
		_lastError = Pa_Terminate();
    if (_lastError != paNoError) Error.emit(E_PORTAUDIO, getError());
		init = false;
	}
}

Int bufferPos = BUFFERSIZE;

int YSE::systemDevice::callback(const void *in, void *out, unsigned long length, const PaStreamCallbackTimeInfo* time, PaStreamCallbackFlags flags, void *userData) {
	if (Sounds().size() == 0) return 0;

  lock l(MTX);

  LastBufferSize = (UInt)length;
  Int pos = 0;
	while (pos < length) {
		if (bufferPos == BUFFERSIZE) {
			ChannelP->dsp();
			bufferPos = 0;
		}

		UInt size = (length - pos) > (BUFFERSIZE - bufferPos) ? (BUFFERSIZE - bufferPos) : ((UInt)length - pos);
		for(UInt i = 0; i < ChannelP->out.size(); i++) {
      // this is not really a safe way to work with buffers, but it won't give any errors in here
      UInt l = size;
      Flt * ptr1 = ((Flt **)out)[i] + pos;
      Flt * ptr2 = ChannelP->out[i].getBuffer() + bufferPos;
      for (; l > 7; l -= 8, ptr1 += 8, ptr2 += 8) {
		    ptr1[0] = ptr2[0];
		    ptr1[1] = ptr2[1];
		    ptr1[2] = ptr2[2];
		    ptr1[3] = ptr2[3];
		    ptr1[4] = ptr2[4];
		    ptr1[5] = ptr2[5];
		    ptr1[6] = ptr2[6];
		    ptr1[7] = ptr2[7];

	    }
	    while(l--) *ptr1++ = *ptr2++;

      // old version, but we can't put Flt *  buffers into a sample anymore
			//currentBuffer(((Flt **)out)[i], length);
			//currentBuffer.copyFrom(ChannelP->out[i], pos, bufferPos, size);
		}
		bufferPos += size;
		pos += size;

	}
	return 0;
}

std::string YSE::systemDevice::getError() {
	std::string result = Pa_GetErrorText(_lastError);
  _lastError = paNoError;
  return result;
}

YSE::device::device() {
	defaultIn = defaultOut = false;
	minLatency = maxLatency = preferredLatency = 0;
	sampleRate = 0;
}

Bool YSE::systemDevice::updateDevices() {
	devices.clear();

	Int count = Pa_GetDeviceCount();
	if (count < 0) return false;

	for (UInt i = 0; i < count; i++) {
		const PaDeviceInfo * info = Pa_GetDeviceInfo(i);
		device d;
    d.ID = i;
		if (i == Pa_GetDefaultInputDevice()) d.defaultIn = true;
		if (i == Pa_GetDefaultOutputDevice()) d.defaultOut = true;
		const PaHostApiInfo *hostInfo = Pa_GetHostApiInfo(info->hostApi);
		d.host = hostInfo->name;
		d.name = info->name;
		d.inChannels = info->maxInputChannels;
		d.outChannels = info->maxOutputChannels;
		d.lowInputLatency = info->defaultLowInputLatency;
		d.highInputLatency = info->defaultHighInputLatency;
		d.lowOutputLatency = info->defaultLowOutputLatency;
		d.highOutputLatency = info->defaultHighOutputLatency;
		d.type = hostInfo->type;

#ifdef WINDOWS
		if (d.type == paASIO) {
			long min, max, pref;
			PaAsio_GetAvailableLatencyValues(i, &min, &max, &pref, NULL);
			d.minLatency = (Int)min;
			d.maxLatency = (Int)max;
			d.preferredLatency = (Int)pref;
		}
#endif

		d.sampleRate = info->defaultSampleRate;
		devices.push_back(d);
	}

  deviceList.clear();
  for (UInt i = 0; i < devices.size(); i++) {
    audioDevice a;
    a.pimpl = &devices[i];
    deviceList.push_back(a);
  }

	return true;
}

Flt YSE::systemDevice::cpuLoad() {
	return Pa_GetStreamCpuLoad(_stream);
}

#endif // defined USE_PORTAUDIO
