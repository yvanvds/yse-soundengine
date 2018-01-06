/*
  ==============================================================================

    portaudioDeviceManager.cpp
    Created: 27 Jul 2016 5:20:20pm
    Author:  yvan

  ==============================================================================
*/

#if PORTAUDIO_BACKEND

#include "portaudioDeviceManager.h"
#include "../internalHeaders.h"
#include "pa_asio.h"

UInt YSE::SAMPLERATE = 44100;

YSE::DEVICE::managerObject & YSE::DEVICE::Manager() {
  static managerObject d;
  return d;
}

YSE::DEVICE::managerObject::managerObject()
  : stream(nullptr)
  , bufferPos(STANDARD_BUFFERSIZE)
  , initDone(false)
  , open(false)
  , started(false)
{}

YSE::DEVICE::managerObject::~managerObject() {
  close();
  terminate();
}

int YSE::DEVICE::managerObject::paCallback(
    const void *input
  , void *output
  , unsigned long numSamples
  , const PaStreamCallbackTimeInfo* timeInfo
  , PaStreamCallbackFlags statusFlags
  , void * userData) {
  YSE::DEVICE::managerObject * manager = (YSE::DEVICE::managerObject *)userData;
 
  if (!manager->doOnCallback(numSamples)) return 0;

  UInt pos = 0;
  while (pos < static_cast<UInt>(numSamples)) {
    if (manager->bufferPos == STANDARD_BUFFERSIZE) {
      manager->master->dsp();
      manager->master->buffersToParent();
      manager->bufferPos = 0;
    }
    
    UInt size = (numSamples - pos) >(STANDARD_BUFFERSIZE - manager->bufferPos) ? (STANDARD_BUFFERSIZE - manager->bufferPos) : ((UInt)numSamples - pos);
    
    for (UInt i = 0; i < manager->master->out.size(); i++) {
      UInt l = size;
      Flt * ptr1 = ((Flt **)output)[i] + pos;
      Flt * ptr2 = manager->master->out[i].getPtr() + manager->bufferPos;

      for (; l > 7; l -= 8, ptr1 += 8, ptr2 += 8) {
        ptr1[0] = ptr2[0] < -1.f ? -1.f : ptr2[0] > 1.f ? 1.f : ptr2[0];
        ptr1[1] = ptr2[1] < -1.f ? -1.f : ptr2[1] > 1.f ? 1.f : ptr2[1];
        ptr1[2] = ptr2[2] < -1.f ? -1.f : ptr2[2] > 1.f ? 1.f : ptr2[2];
        ptr1[3] = ptr2[3] < -1.f ? -1.f : ptr2[3] > 1.f ? 1.f : ptr2[3];
        ptr1[4] = ptr2[4] < -1.f ? -1.f : ptr2[4] > 1.f ? 1.f : ptr2[4];
        ptr1[5] = ptr2[5] < -1.f ? -1.f : ptr2[5] > 1.f ? 1.f : ptr2[5];
        ptr1[6] = ptr2[6] < -1.f ? -1.f : ptr2[6] > 1.f ? 1.f : ptr2[6];
        ptr1[7] = ptr2[7] < -1.f ? -1.f : ptr2[7] > 1.f ? 1.f : ptr2[7];
      }
      while (l--) {
        *ptr1++ = *ptr2 < -1.f ? -1.f : *ptr2 > 1.f ? 1.f : *ptr2;
        ptr2++;
      }
    }
    manager->bufferPos += size;
    pos += size;

  }
  return 0;
}


Bool YSE::DEVICE::managerObject::init() {
  if (!initDone) {
    err = Pa_Initialize();
    if (err != paNoError) {
      audioDeviceError(err);
      return false;
    }
    initDone = true;
  }

  deviceManager::init();
  return true;
}

void YSE::DEVICE::managerObject::addCallback() {
  // setup with default device
  PaStreamParameters params;
  params.device = Pa_GetDefaultOutputDevice();
  const PaDeviceInfo * info = Pa_GetDeviceInfo(params.device);
  params.channelCount = info->maxOutputChannels;
  params.sampleFormat = paFloat32 | paNonInterleaved;
  if (Pa_GetHostApiInfo(info->hostApi)->type == paASIO) {
    long min, max, pref;
    PaAsio_GetAvailableLatencyValues(params.device, &min, &max, &pref, NULL);
    params.suggestedLatency = pref;
  }
  else {
    params.suggestedLatency = info->defaultHighOutputLatency;
  }
  params.hostApiSpecificStreamInfo = nullptr;
  SAMPLERATE = (UInt)info->defaultSampleRate;

  err = Pa_OpenStream(
    &stream
    , NULL
    , &params
    , SAMPLERATE
    , paFramesPerBufferUnspecified
    , paNoFlag
    , paCallback
    , this
  );

  if (err != paNoError) {
    audioDeviceError(err);
    return;
  } else open = true;

  err = Pa_StartStream(stream);
  if (err != paNoError) {
    audioDeviceError(err);
    return;
  } else started = true;
}

void YSE::DEVICE::managerObject::close() {
  if (started) {
    err = Pa_StopStream(stream);
    if (err != paNoError) {
      audioDeviceError(err);
    }
    started = false;
  }

  if(open) {
    err = Pa_CloseStream(stream);
    if (err != paNoError) {
      audioDeviceError(err);
    }
    open = false;
  }
}

void YSE::DEVICE::managerObject::terminate() {
  if (initDone) {
    err = Pa_Terminate();
    if (err != paNoError) {
      audioDeviceError(err);
    }
    initDone = false;
  }
}

void YSE::DEVICE::managerObject::updateDeviceList() {
  devices.clear();

  Int count = Pa_GetDeviceCount();
  if (count < 0) {
    audioDeviceError(count);
    return;
  }

  for (Int i = 0; i < count; i++) {
    const PaDeviceInfo * info = Pa_GetDeviceInfo(i);
    const PaHostApiInfo * hostInfo = Pa_GetHostApiInfo(info->hostApi);
    YSE::device d;

    d.setID(i);
    d.setName(info->name);
    d.setTypeName(hostInfo->name);

    for (Int j = 0; j < info->maxInputChannels; j++) {
      d.addInputChannelName("in " + (j + 1));
    }

    for (Int j = 0; j < info->maxOutputChannels; j++) {
      d.addOutputChannelName("out " + (j + 1));
    }

    d.setInputLatency((Int)info->defaultLowInputLatency);
    d.setOutputLatency((Int)info->defaultLowOutputLatency);
    d.addAvailableSampleRate((Int)info->defaultSampleRate);
    
    devices.push_back(d);
  }
}

void YSE::DEVICE::managerObject::openDevice(const YSE::deviceSetup & object) {
  if (!initDone) return;
  close();

  PaStreamParameters params;
  params.device = object.out->getID();
  const PaDeviceInfo * info = Pa_GetDeviceInfo(params.device);
  params.channelCount = object.getOutputChannels();
  params.sampleFormat = paFloat32 | paNonInterleaved;
  if (Pa_GetHostApiInfo(info->hostApi)->type == paASIO) {
    long min, max, pref;
    PaAsio_GetAvailableLatencyValues(params.device, &min, &max, &pref, NULL);
    params.suggestedLatency = pref;
  }
  else {
    params.suggestedLatency = info->defaultHighOutputLatency;
  }
  params.hostApiSpecificStreamInfo = nullptr;
  SAMPLERATE = (UInt)info->defaultSampleRate;

  err = Pa_OpenStream(
    &stream
    , NULL
    , &params
    , SAMPLERATE
    , object.bufferSize == 0 ? paFramesPerBufferUnspecified : object.bufferSize
    , paNoFlag
    , paCallback
    , this
  );

  if (err != paNoError) {
    audioDeviceError(err);
    return;
  }
  else open = true;

  err = Pa_StartStream(stream);
  if (err != paNoError) {
    audioDeviceError(err);
    return;
  }
  else started = true;
}

void YSE::DEVICE::managerObject::audioDeviceError(PaError error) {
  INTERNAL::LogImpl().emit(E_AUDIODEVICE, Pa_GetErrorText(err));
}

Flt YSE::DEVICE::managerObject::cpuLoad() {
  if (stream != nullptr) {
    return (Flt)Pa_GetStreamCpuLoad(stream);
  }
  return 0.f;
}

#endif // PORTAUDIO_BACKEND