/*
  ==============================================================================

    portaudioDeviceManager.cpp
    Created: 27 Jul 2016 5:20:20pm
    Author:  yvan

  ==============================================================================
*/

#ifdef PORTAUDIO_BACKEND

#include "portaudioDeviceManager.h"
#include "../internalHeaders.h"

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
      while (l--) *ptr1++ = *ptr2++;
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

  abstractDeviceManager::init();

  

  return true;
}

void YSE::DEVICE::managerObject::addCallback() {
  err = Pa_OpenDefaultStream(
    &stream
    , 0
    , 2
    , paFloat32
    , SAMPLERATE
    , 512
    , paCallback
    , this
  );

  if (err != paNoError) {
    audioDeviceError(err);
    return;
  }

  open = true;

  err = Pa_StartStream(stream);
  if (err != paNoError) {
    audioDeviceError(err);
    return;
  }
  started = true;
}

void YSE::DEVICE::managerObject::close() {
  if (stream != nullptr) {
    err = Pa_StopStream(stream);
    if (err != paNoError) {
      audioDeviceError(err);
    }

    err = Pa_CloseStream(stream);
    if (err != paNoError) {
      audioDeviceError(err);
    }
  }
  err = Pa_Terminate();
  if (err != paNoError) {
    audioDeviceError(err);
  }
}

void YSE::DEVICE::managerObject::updateDeviceList() {
  devices.clear();

  Int count = Pa_GetDeviceCount();
  if (count < 0) {
    audioDeviceError(count);
    return;
  }

  for (UInt i = 0; i < count; i++) {
    const PaDeviceInfo * info = Pa_GetDeviceInfo(i);

  }
}

void YSE::DEVICE::managerObject::openDevice(const YSE::DEVICE::setupObject & object) {

}

void YSE::DEVICE::managerObject::audioDeviceError(PaError error) {
  INTERNAL::LogImpl().emit(E_AUDIODEVICE, Pa_GetErrorText(err));
}

Flt YSE::DEVICE::managerObject::cpuLoad() {
  if (stream != nullptr) {
    return Pa_GetStreamCpuLoad(stream);
  }
  return 0.f;
}

#endif // PORTAUDIO_BACKEND